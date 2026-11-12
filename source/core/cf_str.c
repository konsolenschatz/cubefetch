#include "core/cf_str.h"

#include <stdio.h>
#include <string.h>

size_t cf_span_copy(char *dst, size_t dstsz, cf_span s)
{
	size_t n;

	if (dstsz == 0)
		return 0;
	if (!s.p) {
		dst[0] = '\0';
		return 0;
	}

	n = s.len;
	if (n > dstsz - 1)
		n = dstsz - 1;
	memcpy(dst, s.p, n);
	dst[n] = '\0';
	return n;
}

int cf_span_eq_cstr(cf_span s, const char *cstr)
{
	size_t n = strlen(cstr);
	return s.p && s.len == n && memcmp(s.p, cstr, n) == 0;
}

char cf_lower(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)(c - 'A' + 'a');
	return c;
}

/* Bildet ein UTF-8-Zeichen auf einen ASCII-Grundbuchstaben ab und ruekt
 * src um die verbrauchten Bytes weiter. Gibt 0 zurueck, wenn das Zeichen
 * uebersprungen werden soll. */
static char fold_utf8(const char **src)
{
	const unsigned char *p = (const unsigned char *)*src;
	unsigned cp;

	if (p[0] < 0x80) {
		*src += 1;
		return cf_lower((char)p[0]);
	}

	/* Nur der 2-Byte-Bereich ist fuer uns interessant (Latin-1
	 * Supplement und Latin Extended-A). Laengeres wird verworfen. */
	if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
		cp = (unsigned)((p[0] & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
		*src += 2;
	} else if ((p[0] & 0xF0) == 0xE0) {
		*src += (p[1] && p[2]) ? 3 : 1;
		return 0;
	} else if ((p[0] & 0xF8) == 0xF0) {
		*src += (p[1] && p[2] && p[3]) ? 4 : 1;
		return 0;
	} else {
		*src += 1;
		return 0;
	}

	switch (cp) {
	case 0xC4: case 0xE4: case 0xC0: case 0xC1: case 0xC2: case 0xC3:
	case 0xC5: case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE5:
		return 'a';
	case 0xC8: case 0xC9: case 0xCA: case 0xCB:
	case 0xE8: case 0xE9: case 0xEA: case 0xEB:
		return 'e';
	case 0xCC: case 0xCD: case 0xCE: case 0xCF:
	case 0xEC: case 0xED: case 0xEE: case 0xEF:
		return 'i';
	case 0xD3: case 0xD4: case 0xD5: case 0xD6: case 0xD2:
	case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6:
		return 'o';
	case 0xD9: case 0xDA: case 0xDB: case 0xDC:
	case 0xF9: case 0xFA: case 0xFB: case 0xFC:
		return 'u';
	case 0xDF:
		return 's';
	case 0xD1: case 0xF1:
		return 'n';
	case 0xC7: case 0xE7:
		return 'c';
	default:
		return 0;
	}
}

int cf_find_nocase(const char *haystack, const char *needle)
{
	const char *h;

	if (!needle || !*needle)
		return 1;
	if (!haystack)
		return 0;

	for (h = haystack; *h; h++) {
		const char *hp = h;
		const char *np = needle;
		int match = 1;

		while (*np) {
			char nc, hc;
			const char *np_before = np;

			nc = fold_utf8(&np);
			if (nc == 0) {
				if (np == np_before)
					np++;
				continue;
			}

			do {
				if (!*hp) {
					match = 0;
					break;
				}
				hc = fold_utf8(&hp);
			} while (hc == 0);

			if (!match || hc != nc) {
				match = 0;
				break;
			}
		}

		if (match)
			return 1;
	}

	return 0;
}

int cf_cmp_title(const char *a, const char *b)
{
	for (;;) {
		char ca, cb;

		do {
			if (!*a) { ca = 0; break; }
			ca = fold_utf8(&a);
		} while (ca == 0);

		do {
			if (!*b) { cb = 0; break; }
			cb = fold_utf8(&b);
		} while (cb == 0);

		if (ca != cb)
			return (ca < cb) ? -1 : 1;
		if (ca == 0)
			return 0;
	}
}

size_t cf_utf8_to_ascii(char *dst, size_t dstsz, const char *src)
{
	size_t o = 0;

	if (dstsz == 0)
		return 0;

	while (*src && o + 1 < dstsz) {
		const unsigned char *p = (const unsigned char *)src;
		unsigned cp;
		const char *rep = NULL;

		if (p[0] < 0x80) {
			dst[o++] = (char)p[0];
			src++;
			continue;
		}

		if ((p[0] & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
			cp = (unsigned)((p[0] & 0x1F) << 6) | (unsigned)(p[1] & 0x3F);
			src += 2;
		} else if ((p[0] & 0xF0) == 0xE0) {
			src += (p[1] && p[2]) ? 3 : 1;
			dst[o++] = '?';
			continue;
		} else if ((p[0] & 0xF8) == 0xF0) {
			src += (p[1] && p[2] && p[3]) ? 4 : 1;
			dst[o++] = '?';
			continue;
		} else {
			src += 1;
			dst[o++] = '?';
			continue;
		}

		switch (cp) {
		case 0xC4: rep = "Ae"; break;
		case 0xE4: rep = "ae"; break;
		case 0xD6: rep = "Oe"; break;
		case 0xF6: rep = "oe"; break;
		case 0xDC: rep = "Ue"; break;
		case 0xFC: rep = "ue"; break;
		case 0xDF: rep = "ss"; break;
		default:
			break;
		}

		if (rep) {
			size_t rl = strlen(rep);
			if (o + rl + 1 > dstsz)
				break;
			memcpy(dst + o, rep, rl);
			o += rl;
		} else {
			/* Akzente auf den Grundbuchstaben, Rest '?' */
			const char *q = (const char *)p;
			char f = fold_utf8(&q);
			dst[o++] = f ? f : '?';
		}
	}

	dst[o] = '\0';
	return o;
}

size_t cf_url_encode_path(char *dst, size_t dstsz, const char *src)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;

	if (dstsz == 0)
		return 0;

	for (; *src; src++) {
		unsigned char c = (unsigned char)*src;
		int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		           (c >= '0' && c <= '9') ||
		           c == '-' || c == '_' || c == '.' || c == '~' || c == '/';

		if (safe) {
			if (o + 2 > dstsz)
				break;
			dst[o++] = (char)c;
		} else {
			if (o + 4 > dstsz)
				break;
			dst[o++] = '%';
			dst[o++] = hex[(c >> 4) & 0xF];
			dst[o++] = hex[c & 0xF];
		}
	}

	dst[o] = '\0';
	return o;
}

void cf_fmt_size(char *dst, size_t dstsz, uint64_t bytes)
{
	if (bytes >= 1024ull * 1024 * 1024)
		snprintf(dst, dstsz, "%u.%02u GB",
		         (unsigned)(bytes / (1024ull * 1024 * 1024)),
		         (unsigned)((bytes / (1024ull * 1024 * 1024 / 100)) % 100));
	else if (bytes >= 1024ull * 1024)
		snprintf(dst, dstsz, "%u.%01u MB",
		         (unsigned)(bytes / (1024ull * 1024)),
		         (unsigned)((bytes / (1024ull * 1024 / 10)) % 10));
	else if (bytes >= 1024)
		snprintf(dst, dstsz, "%u KB", (unsigned)(bytes / 1024));
	else
		snprintf(dst, dstsz, "%u B", (unsigned)bytes);
}

void cf_fmt_rate(char *dst, size_t dstsz, uint64_t bps)
{
	if (bps >= 1024ull * 1024)
		snprintf(dst, dstsz, "%u.%01u MB",
		         (unsigned)(bps / (1024ull * 1024)),
		         (unsigned)((bps / (1024ull * 1024 / 10)) % 10));
	else
		snprintf(dst, dstsz, "%u KB", (unsigned)(bps / 1024));
}

void cf_fmt_duration(char *dst, size_t dstsz, uint32_t s)
{
	if (s >= 3600)
		snprintf(dst, dstsz, "%uh %02um", s / 3600, (s % 3600) / 60);
	else if (s >= 60)
		snprintf(dst, dstsz, "%um %02us", s / 60, s % 60);
	else
		snprintf(dst, dstsz, "%us", s);
}

void cf_fat_sanitize(char *s)
{
	size_t l;
	char *p;

	for (p = s; *p; p++) {
		unsigned char c = (unsigned char)*p;

		/* 0x5C ist der Backslash. Als Zahl geschrieben, damit die
		 * Zeichenkette unten lesbar bleibt. */
		if (c < 0x20 || c == 0x5C || strchr("<>:\"/|?*", (int)c))
			*p = '_';
	}

	l = strlen(s);
	while (l && (s[l - 1] == '.' || s[l - 1] == ' '))
		s[--l] = '\0';

	if (!s[0])
		snprintf(s, 2, "_");
}
