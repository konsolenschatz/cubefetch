#include "core/cf_srcparse.h"
#include "core/cf_json.h"
#include "core/cf_str.h"

#include <stdio.h>
#include <string.h>

size_t cf_join_url(char *dst, size_t dstsz, const char *base,
                   const char *path)
{
	size_t bl, need;
	char enc[CF_MAX_PATH * 3 + 4];

	cf_url_encode_path(enc, sizeof(enc), path);

	bl = strlen(base);
	while (bl && base[bl - 1] == '/')
		bl--;

	need = bl + 1 + strlen(enc);
	if (dstsz == 0)
		return need;

	if (need < dstsz) {
		memcpy(dst, base, bl);
		dst[bl] = '/';
		memcpy(dst + bl + 1, enc, strlen(enc) + 1);
	} else {
		snprintf(dst, dstsz, "%s", "");
	}

	return need;
}

/* ------------------------------------------------------------------ */
/* Hilfen                                                             */
/* ------------------------------------------------------------------ */

static uint32_t hex32(const char *s)
{
	uint32_t v = 0;

	while (*s) {
		char c = *s++;
		uint32_t d;

		if (c >= '0' && c <= '9')      d = (uint32_t)(c - '0');
		else if (c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
		else break;

		v = (v << 4) | d;
	}
	return v;
}

/* ------------------------------------------------------------------ */
/* Eigenes Manifest                                                   */
/* ------------------------------------------------------------------ */

size_t cf_parse_manifest(const char *json, size_t len, unsigned src,
                         char *base_out, size_t base_sz,
                         cf_add_fn add, void *ctx)
{
	const char *end = json + len;
	const char *v;
	size_t n = 0;

	if (base_out && base_sz) {
		base_out[0] = '\0';
		v = cf_json_member(json, end, "base");
		if (v)
			cf_json_str(v, end, base_out, base_sz);
	}

	v = cf_json_member(json, end, "entries");
	if (!v)
		return 0;

	{
		const char *p = cf_json_arr_begin(v, end);
		const char *item;

		if (!p)
			return 0;

		while (cf_json_arr_next(&p, end, &item)) {
			cf_entry e;
			const char *f;
			char tmp[32];

			memset(&e, 0, sizeof(e));
			e.src = (uint8_t)src;

			f = cf_json_member(item, end, "file");
			if (!f)
				continue;
			if (cf_json_str(f, end, e.path, sizeof(e.path)) == 0)
				continue;

			f = cf_json_member(item, end, "size");
			if (f)
				cf_json_u64(f, end, &e.size);

			f = cf_json_member(item, end, "crc32");
			if (f && cf_json_str(f, end, tmp, sizeof(tmp)) > 0) {
				e.crc32 = hex32(tmp);
				e.has_crc = 1;
			}

			cf_entry_title_from_path(&e);
			cf_entry_mark_image(&e);

			/* Angaben aus dem Manifest haben Vorrang. */
			f = cf_json_member(item, end, "title");
			if (f)
				cf_json_str(f, end, e.title, sizeof(e.title));

			f = cf_json_member(item, end, "id");
			if (f && cf_json_str(f, end, e.id, sizeof(e.id)) >= 6)
				e.id_origin = CF_ID_MANIFEST;

			if (add(ctx, &e) < 0)
				break;
			n++;
		}
	}

	return n;
}

/* ------------------------------------------------------------------ */
/* Verzeichnislisting                                                 */
/* ------------------------------------------------------------------ */

/* Loest die paar HTML-Entitaeten auf, die in Dateinamen vorkommen. */
static void html_unescape(char *s)
{
	char *r = s, *w = s;

	while (*r) {
		if (*r != '&') {
			*w++ = *r++;
			continue;
		}

		if (!strncmp(r, "&amp;", 5))       { *w++ = '&';  r += 5; }
		else if (!strncmp(r, "&lt;", 4))   { *w++ = '<';  r += 4; }
		else if (!strncmp(r, "&gt;", 4))   { *w++ = '>';  r += 4; }
		else if (!strncmp(r, "&quot;", 6)) { *w++ = '"';  r += 6; }
		else if (!strncmp(r, "&#39;", 5))  { *w++ = '\''; r += 5; }
		else if (!strncmp(r, "&apos;", 6)) { *w++ = '\''; r += 6; }
		else                               { *w++ = *r++; }
	}
	*w = '\0';
}

/* Dekodiert %XX an Ort und Stelle, weil href-Werte kodiert sind, der
 * Eintrag aber den rohen Namen halten soll. */
static void url_decode(char *s)
{
	char *r = s, *w = s;

	while (*r) {
		if (*r == '%' && r[1] && r[2]) {
			char h[3] = { r[1], r[2], 0 };
			unsigned v = (unsigned)hex32(h);
			if (v) {
				*w++ = (char)v;
				r += 3;
				continue;
			}
		}
		*w++ = *r++;
	}
	*w = '\0';
}

size_t cf_parse_autoidx(const char *html, size_t len, unsigned src,
                        cf_add_fn add, void *ctx)
{
	const char *p = html;
	const char *end = html + len;
	size_t n = 0;

	while (p < end) {
		const char *q, *vs, *ve;
		char quote;
		cf_entry e;

		/* naechstes href= finden */
		q = NULL;
		for (; p + 5 < end; p++) {
			if ((p[0] == 'h' || p[0] == 'H') &&
			    cf_lower(p[1]) == 'r' && cf_lower(p[2]) == 'e' &&
			    cf_lower(p[3]) == 'f') {
				const char *r = p + 4;
				while (r < end && (*r == ' ' || *r == '\t'))
					r++;
				if (r < end && *r == '=') {
					q = r + 1;
					break;
				}
			}
		}
		if (!q)
			break;

		while (q < end && (*q == ' ' || *q == '\t'))
			q++;
		if (q >= end)
			break;

		if (*q == '"' || *q == '\'') {
			quote = *q;
			vs = q + 1;
			ve = vs;
			while (ve < end && *ve != quote)
				ve++;
		} else {
			vs = q;
			ve = vs;
			while (ve < end && *ve != ' ' && *ve != '>' && *ve != '\t')
				ve++;
		}

		p = (ve < end) ? ve + 1 : end;

		if (ve <= vs || (size_t)(ve - vs) >= CF_MAX_PATH)
			continue;

		memset(&e, 0, sizeof(e));
		e.src = (uint8_t)src;
		memcpy(e.path, vs, (size_t)(ve - vs));
		e.path[ve - vs] = '\0';

		/* Unterordner, Sortierlinks, Elternlink, absolute Adressen und
		 * Anker fallen weg. */
		if (e.path[0] == '?' || e.path[0] == '#' || e.path[0] == '/')
			continue;
		if (e.path[strlen(e.path) - 1] == '/')
			continue;
		if (strstr(e.path, "://"))
			continue;
		if (!strcmp(e.path, "..") || !strcmp(e.path, "."))
			continue;

		html_unescape(e.path);
		url_decode(e.path);

		cf_entry_title_from_path(&e);
		cf_entry_mark_image(&e);

		/* Ohne Groesse im Listing bleibt size 0; die Oberflaeche holt
		 * sie bei Bedarf per HEAD nach. */
		if (add(ctx, &e) < 0)
			break;
		n++;
	}

	return n;
}
