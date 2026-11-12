#include "core/cf_json.h"

#include <string.h>

const char *cf_json_skip_ws(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
		p++;
	return p;
}

/* Ueberspringt einen String ab dem oeffnenden Anfuehrungszeichen. */
static const char *skip_string(const char *p, const char *end)
{
	if (p >= end || *p != '"')
		return NULL;
	p++;
	while (p < end) {
		if (*p == '\\') {
			p += 2;
			continue;
		}
		if (*p == '"')
			return p + 1;
		p++;
	}
	return NULL;
}

const char *cf_json_skip_value(const char *p, const char *end)
{
	int depth;

	p = cf_json_skip_ws(p, end);
	if (p >= end)
		return NULL;

	if (*p == '"')
		return skip_string(p, end);

	if (*p == '{' || *p == '[') {
		depth = 0;
		while (p < end) {
			if (*p == '"') {
				p = skip_string(p, end);
				if (!p)
					return NULL;
				continue;
			}
			if (*p == '{' || *p == '[')
				depth++;
			else if (*p == '}' || *p == ']') {
				depth--;
				if (depth == 0)
					return p + 1;
			}
			p++;
		}
		return NULL;
	}

	/* Zahl, true, false, null */
	while (p < end && *p != ',' && *p != '}' && *p != ']' &&
	       *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
		p++;
	return p;
}

const char *cf_json_obj_begin(const char *p, const char *end)
{
	p = cf_json_skip_ws(p, end);
	if (p >= end || *p != '{')
		return NULL;
	return p + 1;
}

int cf_json_obj_next(const char **pp, const char *end,
                     cf_span *key, const char **val)
{
	const char *p = *pp;
	const char *ks, *ke;

	p = cf_json_skip_ws(p, end);
	if (p >= end || *p == '}')
		return 0;

	if (*p == ',') {
		p = cf_json_skip_ws(p + 1, end);
		if (p >= end || *p == '}')
			return 0;
	}

	if (*p != '"')
		return 0;
	ks = p + 1;
	p = skip_string(p, end);
	if (!p)
		return 0;
	ke = p - 1;

	p = cf_json_skip_ws(p, end);
	if (p >= end || *p != ':')
		return 0;
	p = cf_json_skip_ws(p + 1, end);
	if (p >= end)
		return 0;

	if (key) {
		key->p = ks;
		key->len = (size_t)(ke - ks);
	}
	if (val)
		*val = p;

	p = cf_json_skip_value(p, end);
	if (!p)
		return 0;

	*pp = p;
	return 1;
}

const char *cf_json_arr_begin(const char *p, const char *end)
{
	p = cf_json_skip_ws(p, end);
	if (p >= end || *p != '[')
		return NULL;
	return p + 1;
}

int cf_json_arr_next(const char **pp, const char *end, const char **val)
{
	const char *p = *pp;

	p = cf_json_skip_ws(p, end);
	if (p >= end || *p == ']')
		return 0;

	if (*p == ',') {
		p = cf_json_skip_ws(p + 1, end);
		if (p >= end || *p == ']')
			return 0;
	}

	if (val)
		*val = p;

	p = cf_json_skip_value(p, end);
	if (!p)
		return 0;

	*pp = p;
	return 1;
}

const char *cf_json_member(const char *obj, const char *end, const char *key)
{
	const char *p = cf_json_obj_begin(obj, end);
	cf_span k;
	const char *v;

	if (!p)
		return NULL;

	while (cf_json_obj_next(&p, end, &k, &v)) {
		if (cf_span_eq_cstr(k, key))
			return v;
	}
	return NULL;
}

static size_t put_utf8(char *dst, size_t dstsz, size_t o, unsigned cp)
{
	if (cp < 0x80) {
		if (o + 1 >= dstsz) return o;
		dst[o++] = (char)cp;
	} else if (cp < 0x800) {
		if (o + 2 >= dstsz) return o;
		dst[o++] = (char)(0xC0 | (cp >> 6));
		dst[o++] = (char)(0x80 | (cp & 0x3F));
	} else {
		if (o + 3 >= dstsz) return o;
		dst[o++] = (char)(0xE0 | (cp >> 12));
		dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
		dst[o++] = (char)(0x80 | (cp & 0x3F));
	}
	return o;
}

static int hex4(const char *p, const char *end, unsigned *out)
{
	unsigned v = 0;
	int i;

	if (p + 4 > end)
		return 0;
	for (i = 0; i < 4; i++) {
		char c = p[i];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
		else return 0;
	}
	*out = v;
	return 1;
}

size_t cf_json_str(const char *val, const char *end, char *dst, size_t dstsz)
{
	size_t o = 0;
	const char *p = val;

	if (dstsz == 0)
		return 0;
	dst[0] = '\0';
	if (!p || p >= end || *p != '"')
		return 0;
	p++;

	while (p < end && *p != '"' && o + 1 < dstsz) {
		if (*p != '\\') {
			dst[o++] = *p++;
			continue;
		}

		p++;
		if (p >= end)
			break;

		switch (*p) {
		case 'n': dst[o++] = '\n'; p++; break;
		case 'r': dst[o++] = '\r'; p++; break;
		case 't': dst[o++] = '\t'; p++; break;
		case 'b': dst[o++] = '\b'; p++; break;
		case 'f': dst[o++] = '\f'; p++; break;
		case 'u': {
			unsigned cp;
			if (hex4(p + 1, end, &cp)) {
				/* Surrogatpaare werden nicht zusammengesetzt;
				 * in Dateinamen kommen sie nicht vor. */
				if (cp >= 0xD800 && cp <= 0xDFFF)
					cp = '?';
				o = put_utf8(dst, dstsz, o, cp);
				p += 5;
			} else {
				dst[o++] = '?';
				p++;
			}
			break;
		}
		default:
			dst[o++] = *p++;
			break;
		}
	}

	dst[o] = '\0';
	return o;
}

int cf_json_u64(const char *val, const char *end, uint64_t *out)
{
	const char *p = val;
	uint64_t v = 0;
	int any = 0;

	if (!p || p >= end)
		return 0;

	if (*p == '"')
		p++;

	while (p < end && *p >= '0' && *p <= '9') {
		v = v * 10 + (uint64_t)(*p - '0');
		p++;
		any = 1;
	}

	if (!any)
		return 0;

	*out = v;
	return 1;
}
