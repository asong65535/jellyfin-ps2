#include "text.h"

int text_put_cp(unsigned int cp, char *out)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}

	switch (cp) {
	case 0xA0:                                   /* no-break space */
	case 0x2007: case 0x2009: case 0x200A: case 0x202F:
		out[0] = ' ';
		return 1;

	case 0xC6: out[0] = 'A'; out[1] = 'E'; return 2;   /* not in the atlas */
	case 0xE6: out[0] = 'a'; out[1] = 'e'; return 2;
	case 0x152: out[0] = 'O'; out[1] = 'E'; return 2;
	case 0x153: out[0] = 'o'; out[1] = 'e'; return 2;
	case 0x178: out[0] = 'Y'; return 1;

	case 0x2018: case 0x2019: case 0x201A: case 0x2032: case 0x2039:
	case 0x203A: case 0x02BC:
		out[0] = '\'';
		return 1;

	case 0x201C: case 0x201D: case 0x201E: case 0x2033:
		out[0] = '"';
		return 1;

	case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
	case 0x2015: case 0x2212:
		out[0] = '-';
		return 1;

	/* Macrons, as in romanised Japanese names: the plain vowel. */
	case 0x100: case 0x102: case 0x104: out[0] = 'A'; return 1;
	case 0x101: case 0x103: case 0x105: out[0] = 'a'; return 1;
	case 0x112: case 0x118: case 0x11A: out[0] = 'E'; return 1;
	case 0x113: case 0x119: case 0x11B: out[0] = 'e'; return 1;
	case 0x12A: out[0] = 'I'; return 1;
	case 0x12B: out[0] = 'i'; return 1;
	case 0x14C: out[0] = 'O'; return 1;
	case 0x14D: out[0] = 'o'; return 1;
	case 0x16A: out[0] = 'U'; return 1;
	case 0x16B: out[0] = 'u'; return 1;

	case 0x2026: out[0] = '.'; out[1] = '.'; out[2] = '.'; return 3;
	case 0x2022: out[0] = '*'; return 1;
	case 0x266A: case 0x266B: out[0] = '#'; return 1;  /* sung lines */
	case 0x20AC: out[0] = 'E'; out[1] = 'U'; out[2] = 'R'; return 3;
	}

	if (cp >= 0xA0 && cp <= 0xFF) {
		out[0] = (char)cp;
		return 1;
	}

	out[0] = '?';
	return 1;
}

int text_utf8_to_latin1(const char *in, int len, char *out)
{
	const unsigned char *s = (const unsigned char *)in;
	int i = 0, o = 0;

	while (i < len) {
		unsigned int c = s[i];
		unsigned int cp;
		int need;

		if (c < 0x80) {
			out[o++] = (char)c;
			i++;
			continue;
		}

		if      ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 3; }
		else                         { need = -1; cp = 0; }

		int ok = need > 0;

		for (int k = 1; ok && k <= need; k++) {
			if (i + k >= len || (s[i + k] & 0xC0) != 0x80)
				ok = 0;
			else
				cp = (cp << 6) | (s[i + k] & 0x3F);
		}

		if (!ok) {
			/* Not UTF-8: take the byte as Latin-1. */
			out[o++] = (char)(c >= 0xA0 ? c : '?');
			i++;
			continue;
		}

		/*
		 * The Latin-1 spelling is at most 3 bytes, and a sequence that
		 * produces 2 or 3 is itself at least that long (U+0152 and up
		 * take 2 bytes or more, U+2026 takes 3), so out never overtakes
		 * in and in-place conversion is safe.
		 */
		o += text_put_cp(cp, out + o);
		i += need + 1;
	}

	return o;
}
