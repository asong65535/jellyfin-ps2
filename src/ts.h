#ifndef TS_H
#define TS_H

/*
 * Pulling one elementary stream out of an MPEG transport stream.
 *
 * Jellyfin transcodes to a transport stream, so what arrives is not the raw
 * MPEG-2 the decoder wants: it is 188-byte packets carrying video, audio and
 * tables interleaved. Confirmed against a real transcode from this server -
 * video on PID 0x0100, audio on 0x0101 as MPEG-1 layer II.
 *
 * The extraction is small: keep packets on the wanted PID, skip the
 * adaptation field, and strip the PES header from packets that start one.
 * What is left, concatenated, is the elementary stream.
 *
 * The awkward part is that packets arrive split across reads, so the state
 * has to survive between calls. That is what this holds.
 */

/* An 8 KB read holds at most 44 packets, so at most 44 PES starts. */
#define TS_MARKS 64

typedef struct {
	int  pid;                 /* the stream to keep */
	unsigned char part[188];  /* a packet still being assembled */
	int  have;                /* bytes of it so far */
	int  synced;              /* found the packet boundary yet */

	/*
	 * The most recent presentation timestamp seen, in milliseconds.
	 *
	 * PES headers carry one every few packets, on the same clock for
	 * both streams - which is what makes it possible to play audio and
	 * video in step. has_pts stays set once the first one arrives.
	 */
	int  pts_ms;
	int  has_pts;

	/*
	 * Where timestamps started, within the output of the last feed.
	 *
	 * The latest PTS alone is not enough to pace by: by the time a
	 * picture is decoded the demuxer has moved on several packets. So each
	 * feed records, for every PES header that carried a PTS, how many
	 * bytes of this stream had been written before it. The caller turns
	 * that into a position in its ring, and the decoder side can then say
	 * which timestamp the bytes it is consuming belong to.
	 *
	 * Full 33 bits at 90 kHz. Reset at the start of every feed.
	 */
	int       nmark;
	int       mark_off[TS_MARKS];
	long long mark_pts[TS_MARKS];
} TsDemux;

/* Start, or start again for a new stream. */
void ts_init(TsDemux *d, int pid);

/*
 * Feed bytes in, take elementary stream out.
 *
 * Writes at most out_max bytes and returns how many.
 *
 * out_max MUST be at least in_len. Everything not written when the output
 * fills is discarded, because the caller reads afresh next time - and a
 * stream missing a large part of itself decodes into macroblock rubbish
 * rather than failing outright, so the mistake is not obvious.
 *
 * An equal-sized buffer is always enough: 188 bytes in yields at most 184
 * out, so the output can never fill before the input runs out.
 */
int  ts_feed(TsDemux *d, const unsigned char *in, int in_len,
             unsigned char *out, int out_max);

/*
 * Both streams in one pass.
 *
 * Feeding the same bytes to two demuxers walks every packet twice, and that
 * work lands on the decoder's thread - it was enough to make the picture
 * stutter. This splits by PID as it goes, so each packet is examined once.
 *
 * Returns nothing; the byte counts are written to *vn and *an.
 *
 * BOTH buffers must be at least in_len PLUS 188. Whatever has not been
 * written when either fills is discarded.
 *
 * The margin is not decoration. A read of 8192 holds 43 whole packets and
 * part of a 44th, and a packet that carries no PES header emits its full
 * 184 bytes - so a read can yield 8096 bytes, past the point where a
 * same-sized buffer stops. The bytes still unread are then lost, which
 * decodes as blocks of corruption rather than as any kind of error.
 */
void ts_feed2(TsDemux *v, TsDemux *a,
              const unsigned char *in, int in_len,
              unsigned char *vout, int vmax, int *vn,
              unsigned char *aout, int amax, int *an);

#endif