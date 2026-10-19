/*
 * Text subtitles: downloaded once per film as SRT, parsed into timed
 * lines, converted to Latin-1, and looked up by position for drawing.
 */

#include "subs.h"
#include "config.h"
#include "jf.h"
#include "net.h"
#include "text.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int start, end;        /* ms */
	int off;               /* into g_buf: Latin-1, '\n' between lines */
} Cue;

static char *g_buf;        /* the file, then the cue text in place */
static Cue  *g_cue;
static int   g_n;          /* cues loaded, 0 for none */
static int   g_cur;        /* where the last lookup ended: a hint */

static char  g_item[40];
static int   g_index = -1;

/* Free the loaded track and forget which one it was. */
void subs_clear(void)
{
	free(g_buf);
	free(g_cue);
	g_buf = NULL;
	g_cue = NULL;
	g_n   = 0;
	g_cur = 0;
	g_item[0] = '\0';
	g_index   = -1;
}


/* Whether a track is loaded and it belongs to item_id. */
int subs_active_for(const char *item_id)
{
	return g_n > 0 && item_id && strcmp(g_item, item_id) == 0;
}

/* ------------------------------------------------------------------ */
/* Download                                                           */
/* ------------------------------------------------------------------ */

/*
 * The whole file into g_buf. HTTP/1.0, so the server closes when done and
 * there is no length to trust. Returns bytes, or negative.
 */
static int fetch(const char *item_id, int index)
{
	char path[256];

	/*
	 * The media source is the item itself for anything with one version,
	 * which is what the rest of the client assumes too.
	 */
	snprintf(path, sizeof(path),
	         "/Videos/%s/%s/Subtitles/%d/0/Stream.srt?api_key=%s",
	         item_id, item_id, index, jf_key());

	/*
	 * Up to 20 s for the reply to start: the first request for a track
	 * inside a large file makes Jellyfin extract it before answering.
	 * It keeps the result, so the next time is quick.
	 */
	int s = net_stream_open_wait(jf_host(), jf_port(), path, NULL, 20000);

	if (s < 0)
		return s;

	net_stream_set_timeout(s, 5000);

	g_buf = malloc(SUBS_MAX_BYTES + 1);

	if (!g_buf) {
		net_stream_close(s);
		return -20;
	}

	int used = 0;

	while (used < SUBS_MAX_BYTES) {
		int n = net_stream_read(s, g_buf + used, SUBS_MAX_BYTES - used);

		if (n <= 0)
			break;               /* closed: the end. Or a stall. */

		used += n;
	}

	net_stream_close(s);

	g_buf[used] = '\0';
	return used;
}

/* ------------------------------------------------------------------ */
/* Parsing                                                            */
/* ------------------------------------------------------------------ */

/* "01:02:03,456" or "02:03.456" (WebVTT drops the hours): ms, or -1. */
static int parse_time(const char *p, const char **end)
{
	int part[3] = { 0, 0, 0 };
	int n = 0;

	while (*p == ' ')
		p++;

	for (;;) {
		if (*p < '0' || *p > '9')
			return -1;

		int v = 0;

		while (*p >= '0' && *p <= '9')
			v = v * 10 + (*p++ - '0');

		if (n < 3)
			part[n++] = v;

		if (*p == ':') { p++; continue; }
		break;
	}

	int ms = 0;

	if (*p == ',' || *p == '.') {
		int digits = 0;

		p++;
		while (*p >= '0' && *p <= '9') {
			if (digits < 3) { ms = ms * 10 + (*p - '0'); digits++; }
			p++;
		}
		while (digits++ < 3)
			ms *= 10;
	}

	int h = 0, m, s;

	if (n == 3)      { h = part[0]; m = part[1]; s = part[2]; }
	else if (n == 2) { m = part[0]; s = part[1]; }
	else             return -1;

	if (end)
		*end = p;

	return ((h * 60 + m) * 60 + s) * 1000 + ms;
}

/*
 * Clean one cue's text where it lies: styling tags out (<i>, {\an8},
 * <font ...>), ASS's \N to a line break, UTF-8 to Latin-1, blank lines
 * and stray spaces trimmed. The result is never longer, so it is written
 * over the original.
 */
static int clean(char *t, int len)
{
	int o = 0;

	for (int i = 0; i < len; i++) {
		char c = t[i];

		if (c == '<') {                      /* <i>, </font> ... */
			int j = i;
			while (j < len && t[j] != '>')
				j++;
			if (j < len) { i = j; continue; }
		}

		if (c == '{') {                      /* {\an8}, {\i1} ... */
			int j = i;
			while (j < len && t[j] != '}')
				j++;
			if (j < len) { i = j; continue; }
		}

		if (c == '\\' && i + 1 < len && (t[i + 1] == 'N' || t[i + 1] == 'n')) {
			t[o++] = '\n';
			i++;
			continue;
		}

		if (c == '\r')
			continue;

		t[o++] = c;
	}

	o = text_utf8_to_latin1(t, o, t);

	/* Trim spaces around each line and drop empty lines. */
	int w = 0, line_start = 0;

	for (int i = 0; i <= o; i++) {
		if (i == o || t[i] == '\n') {
			int a = line_start, b = i;

			while (a < b && t[a] == ' ') a++;
			while (b > a && t[b - 1] == ' ') b--;

			if (b > a) {
				if (w > 0)
					t[w++] = '\n';
				memmove(t + w, t + a, b - a);
				w += b - a;
			}
			line_start = i + 1;
		}
	}

	t[w] = '\0';
	return w;
}

/* qsort order for cues: by start time. */
static int by_start(const void *a, const void *b)
{
	return ((const Cue *)a)->start - ((const Cue *)b)->start;
}

/*
 * Blocks of: a number, a "start --> end" line, text lines, a blank line.
 * Only the arrow line is relied on, so the numbers may be missing or
 * wrong and WebVTT's header is skipped as noise.
 */
static int parse(int len)
{
	g_cue = malloc(sizeof(Cue) * SUBS_MAX_CUES);
	if (!g_cue)
		return -21;

	char *p   = g_buf;
	char *end = g_buf + len;

	/* A UTF-8 byte order mark. */
	if (len >= 3 && (unsigned char)p[0] == 0xEF &&
	    (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
		p += 3;

	g_n = 0;

	while (p < end && g_n < SUBS_MAX_CUES) {
		char *nl = memchr(p, '\n', end - p);
		char *le = nl ? nl : end;

		char *arrow = NULL;

		for (char *q = p; q + 2 < le; q++) {
			if (q[0] == '-' && q[1] == '-' && q[2] == '>') {
				arrow = q;
				break;
			}
		}

		if (!arrow) {
			p = nl ? nl + 1 : end;
			continue;
		}

		int st = parse_time(p, NULL);
		int en = parse_time(arrow + 3, NULL);

		p = nl ? nl + 1 : end;

		/* The text: every line up to a blank one. */
		char *ts = p;

		while (p < end) {
			char *n2 = memchr(p, '\n', end - p);
			char *e2 = n2 ? n2 : end;
			char *q  = p;

			while (q < e2 && (*q == ' ' || *q == '\r' || *q == '\t'))
				q++;

			if (q == e2) {               /* blank: the cue ends */
				p = n2 ? n2 + 1 : end;
				break;
			}

			p = n2 ? n2 + 1 : end;
		}

		char *te = p;

		while (te > ts && (te[-1] == '\n' || te[-1] == '\r' ||
		                   te[-1] == ' '))
			te--;

		if (st < 0 || en <= st || te <= ts)
			continue;

		int n = clean(ts, (int)(te - ts));

		if (n == 0)
			continue;

		g_cue[g_n].start = st;
		g_cue[g_n].end   = en;
		g_cue[g_n].off   = (int)(ts - g_buf);
		g_n++;
	}

	/* Usually in order already; converted ASS often is not. */
	qsort(g_cue, g_n, sizeof(Cue), by_start);

	return g_n;
}

/*
 * Fetch and parse subtitle track index of item_id; see subs.h. A track
 * already loaded is kept without downloading again.
 */
int subs_load(const char *item_id, int index)
{
	if (g_n > 0 && g_index == index && strcmp(g_item, item_id) == 0)
		return g_n;                  /* this one is loaded already */

	subs_clear();

	int len = fetch(item_id, index);

	if (len <= 0) {
		subs_clear();
		return len < 0 ? len : -22;
	}

	int n = parse(len);

	if (n <= 0) {
		subs_clear();
		return n < 0 ? n : -23;
	}

	snprintf(g_item, sizeof(g_item), "%s", item_id);
	g_index = index;
	g_cur   = 0;

	return n;
}

/* ------------------------------------------------------------------ */
/* Lookup                                                             */
/* ------------------------------------------------------------------ */

const char *subs_at(int ms)
{
	static char two[1024];

	if (g_n <= 0 || ms < 0)
		return NULL;

	/*
	 * The last cue starting at or before ms. Playing forward that is the
	 * hint or just after it; after a seek, a binary search.
	 */
	int i = g_cur;

	if (i >= g_n)
		i = 0;

#define HOLDS(k) (g_cue[k].start <= ms && \
                  ((k) + 1 >= g_n || g_cue[(k) + 1].start > ms))

	if (HOLDS(i)) {
		/* still the same */
	} else if (i + 1 < g_n && HOLDS(i + 1)) {
		i++;
	} else {
		int lo = 0, hi = g_n - 1;

		i = -1;
		while (lo <= hi) {
			int mid = (lo + hi) / 2;

			if (g_cue[mid].start <= ms) { i = mid; lo = mid + 1; }
			else                         hi = mid - 1;
		}

		if (i < 0)
			return NULL;
	}

#undef HOLDS

	g_cur = i;

	/*
	 * Overlapping cues - two speakers at once - are shown together, the
	 * earlier on top. Look back a few for one still running.
	 */
	const char *a = NULL, *b = NULL;

	for (int k = i; k >= 0 && k > i - 8; k--) {
		if (g_cue[k].start <= ms && ms < g_cue[k].end) {
			if (!b)      b = g_buf + g_cue[k].off;
			else if (!a) a = g_buf + g_cue[k].off;
		}
	}

	if (!b)
		return NULL;

	if (!a)
		return b;

	snprintf(two, sizeof(two), "%s\n%s", a, b);
	return two;
}
