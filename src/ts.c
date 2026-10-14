#include "ts.h"

#include <string.h>

#define TS_PACKET 188
#define TS_SYNC   0x47

void ts_init(TsDemux *d, int pid)
{
	d->pid     = pid;
	d->have    = 0;
	d->synced  = 0;
	d->pts_ms  = 0;
	d->has_pts = 0;
	d->nmark   = 0;
}

/*
 * One whole packet.
 *
 * Layout, as far as we care:
 *
 *   byte 0      0x47, the sync byte
 *   byte 1      bit 6 payload-start, bits 0-4 the top of the PID
 *   byte 2      the rest of the PID
 *   byte 3      bit 5 adaptation field present, bit 4 payload present
 *   byte 4      adaptation field length, if there is one
 *
 * A packet whose payload starts a PES also carries a PES header, whose
 * length is in its ninth byte. The elementary stream begins after that.
 */
static int packet(TsDemux *d, const unsigned char *p,
                  unsigned char *out, int out_max, int at)
{
	int pid = ((p[1] & 0x1F) << 8) | p[2];

	if (pid != d->pid)
		return 0;

	if (!(p[3] & 0x10))
		return 0;                      /* no payload, just timing */

	int off = 4;

	if (p[3] & 0x20)
		off += 1 + p[4];               /* skip the adaptation field */

	if (off >= TS_PACKET)
		return 0;                      /* adaptation filled the packet */

	if (p[1] & 0x40) {
		/* A PES packet starts here; the stream is past its header. */
		const unsigned char *q = p + off;

		if (q[0] == 0x00 && q[1] == 0x00 && q[2] == 0x01) {
			int hdr = q[8];

			/*
			 * The presentation timestamp, if this header carries
			 * one. Bit 7 of the flags byte says so.
			 *
			 * 33 bits at 90 kHz, spread across five bytes with a
			 * marker bit after every group - the layout is
			 * awkward rather than difficult:
			 *
			 *   q[9]  0011 PPP1     bits 32..30
			 *   q[10] PPPPPPPP      bits 29..22
			 *   q[11] PPPPPPP1      bits 21..15
			 *   q[12] PPPPPPPP      bits 14..7
			 *   q[13] PPPPPPP1      bits 6..0
			 *
			 * pts_ms keeps the latest one in milliseconds, as
			 * before; the marks keep every one at 90 kHz.
			 */
			/*
			 * Bounds first.
			 *
			 * q points into a 188-byte packet at `off`, and the
			 * timestamp lives at q[9] through q[13]. A PES header
			 * starting near the end of a packet puts those past
			 * the end of part[], reading into the rest of the
			 * struct - and the values found there produce a
			 * nonsense timestamp at best.
			 */
			if (off + 14 <= TS_PACKET && (q[7] & 0x80) && hdr >= 5) {
				unsigned int hi  = (q[9] >> 1) & 0x07;
				unsigned int mid = ((unsigned)q[10] << 7) |
				                   ((unsigned)q[11] >> 1);
				unsigned int lo  = ((unsigned)q[12] << 7) |
				                   ((unsigned)q[13] >> 1);

				/*
				 * All 33 bits, in 64-bit arithmetic. The top
				 * three used to be dropped to keep this in an
				 * int, which wraps after 3.3 hours of PTS -
				 * and a seek can start a stream that far in.
				 */
				long long ticks = ((long long)hi << 30) |
				                  ((long long)mid << 15) | lo;

				d->pts_ms = (int)(ticks / 90);

				/*
				 * The payload after this header starts at `at`
				 * bytes into this feed's output for the stream.
				 */
				if (d->nmark < TS_MARKS) {
					d->mark_off[d->nmark] = at;
					d->mark_pts[d->nmark] = ticks;
					d->nmark++;
				}

				d->has_pts = 1;
			}

			off += 9 + hdr;

			if (off >= TS_PACKET)
				return 0;
		}
	}

	int n = TS_PACKET - off;

	/*
	 * Refuse rather than truncate.
	 *
	 * Copying only what fits and dropping the rest loses part of the
	 * bitstream, which decodes into macroblock rubbish instead of failing
	 * - so the mistake is invisible until someone looks at the picture.
	 * The caller checks for room before each packet, so this should never
	 * trigger; if it does, losing the whole packet is at least detectable.
	 */
	if (n > out_max)
		return 0;

	memcpy(out, p + off, n);
	return n;
}

void ts_feed2(TsDemux *v, TsDemux *a,
              const unsigned char *in, int in_len,
              unsigned char *vout, int vmax, int *vn,
              unsigned char *aout, int amax, int *an)
{
	*vn = 0;
	*an = 0;

	v->nmark = 0;
	a->nmark = 0;

	/*
	 * One demuxer holds the packet assembly, because the two streams are
	 * interleaved in the same 188-byte grid - there is only one sync to
	 * find and one partial packet to carry between reads. The other is
	 * used purely for its PID and its timestamp.
	 */
	int i = 0;

	while (i < in_len) {
		if (!v->synced && v->have == 0) {
			if (in[i] != TS_SYNC) {
				i++;
				continue;
			}

			if (i + TS_PACKET < in_len && in[i + TS_PACKET] != TS_SYNC) {
				i++;
				continue;
			}

			v->synced = 1;
		}

		int want = TS_PACKET - v->have;
		int give = in_len - i;

		if (give > want)
			give = want;

		memcpy(v->part + v->have, in + i, give);
		v->have += give;
		i       += give;

		if (v->have < TS_PACKET)
			break;

		v->have = 0;

		if (v->part[0] != TS_SYNC) {
			v->synced = 0;
			continue;
		}

		/* One look at the PID decides which stream it belongs to. */
		int pid = ((v->part[1] & 0x1F) << 8) | v->part[2];

		if (pid == v->pid)
			*vn += packet(v, v->part, vout + *vn, vmax - *vn, *vn);
		else if (pid == a->pid)
			*an += packet(a, v->part, aout + *an, amax - *an, *an);

		/*
		 * Stop before either output could overflow.
		 *
		 * Both buffers must therefore be at least as large as the
		 * input, because whatever is left in `in` is discarded when
		 * this returns. Giving audio a small buffer cut every read
		 * short as soon as it filled, losing video with it - 94 KB out
		 * of 1.6 MB in testing.
		 */
		if (vmax - *vn < TS_PACKET || amax - *an < TS_PACKET)
			break;
	}
}

int ts_feed(TsDemux *d, const unsigned char *in, int in_len,
            unsigned char *out, int out_max)
{
	int wrote = 0;
	int i     = 0;

	d->nmark = 0;

	while (i < in_len) {
		/*
		 * Find the first packet boundary.
		 *
		 * A stream can be joined anywhere, and a 0x47 can occur inside
		 * a payload by chance, so require a second one 188 bytes later
		 * before believing it.
		 */
		if (!d->synced && d->have == 0) {
			if (in[i] != TS_SYNC) {
				i++;
				continue;
			}

			if (i + TS_PACKET < in_len && in[i + TS_PACKET] != TS_SYNC) {
				i++;
				continue;              /* coincidence, not a packet */
			}

			d->synced = 1;
		}

		/* Gather a packet, however many reads it takes. */
		int want = TS_PACKET - d->have;
		int give = in_len - i;

		if (give > want)
			give = want;

		memcpy(d->part + d->have, in + i, give);
		d->have += give;
		i       += give;

		if (d->have < TS_PACKET)
			break;                     /* the rest is in the next read */

		d->have = 0;

		if (d->part[0] != TS_SYNC) {
			/* Lost alignment: hunt for it again rather than emitting
			 * rubbish the decoder would choke on. */
			d->synced = 0;
			continue;
		}

		wrote += packet(d, d->part, out + wrote, out_max - wrote, wrote);

		/*
		 * Stop before the output could overflow.
		 *
		 * Anything still in `in` is lost when this returns, since the
		 * caller reads afresh next time. That is why the contract says
		 * out_max must be at least in_len - a transport stream is
		 * always larger than what it carries, so an equal-sized output
		 * buffer can never fill first. Passing a smaller one silently
		 * discarded a large part of the stream and corrupted every
		 * picture.
		 */
		if (out_max - wrote < TS_PACKET)
			break;
	}

	return wrote;
}