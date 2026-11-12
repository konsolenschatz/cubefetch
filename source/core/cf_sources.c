#include "core/cf_sources.h"
#include "core/cf_str.h"

#include <stdio.h>
#include <string.h>

const char *cf_src_type_name(cf_src_type t)
{
	switch (t) {
	case CF_SRC_AUTOIDX:  return "autoidx";
	case CF_SRC_MANIFEST: return "manifest";
	default:              return "autoidx";
	}
}

static int tail_is(const char *s, const char *suffix)
{
	size_t ls = strlen(s), lf = strlen(suffix);
	size_t i;

	if (ls < lf)
		return 0;
	for (i = 0; i < lf; i++) {
		if (cf_lower(s[ls - lf + i]) != cf_lower(suffix[i]))
			return 0;
	}
	return 1;
}

cf_src_type cf_src_guess(const char *url)
{
	if (!url)
		return CF_SRC_AUTOIDX;

	if (tail_is(url, ".json"))
		return CF_SRC_MANIFEST;

	return CF_SRC_AUTOIDX;
}

static void copy_trim(char *dst, size_t dstsz, const char *b, const char *e)
{
	size_t n;

	while (b < e && (*b == ' ' || *b == '\t'))
		b++;
	while (e > b && (e[-1] == ' ' || e[-1] == '\t' ||
	                 e[-1] == '\r' || e[-1] == '\n'))
		e--;

	n = (size_t)(e - b);
	if (n > dstsz - 1)
		n = dstsz - 1;
	memcpy(dst, b, n);
	dst[n] = '\0';
}

int cf_sources_add(cf_sources *s, cf_src_type t,
                   const char *name, const char *url)
{
	cf_source *d;

	if (s->n >= CF_MAX_SOURCES || !url || !*url)
		return 0;

	d = &s->v[s->n];
	memset(d, 0, sizeof(*d));
	d->type = t;
	snprintf(d->url, sizeof(d->url), "%s", url);
	if (name && *name)
		snprintf(d->name, sizeof(d->name), "%s", name);
	else
		snprintf(d->name, sizeof(d->name), "%s", url);

	s->n++;
	return 1;
}

int cf_sources_remove(cf_sources *s, size_t idx)
{
	size_t i;

	if (idx >= s->n)
		return 0;
	for (i = idx; i + 1 < s->n; i++)
		s->v[i] = s->v[i + 1];
	s->n--;
	return 1;
}

/* Eine Zeile: "<typ> | <name> | <url>", wobei typ und name wegfallen
 * duerfen. Ohne Trenner gilt die ganze Zeile als URL. */
static void parse_line(cf_sources *s, const char *b, const char *e)
{
	const char *bar1, *bar2, *p;
	char f1[CF_SRC_URL_MAX], f2[CF_SRC_URL_MAX], f3[CF_SRC_URL_MAX];
	int nf = 1;

	bar1 = NULL;
	bar2 = NULL;
	for (p = b; p < e; p++) {
		if (*p == '|') {
			if (!bar1)
				bar1 = p;
			else if (!bar2)
				bar2 = p;
		}
	}

	if (bar1 && bar2) {
		copy_trim(f1, sizeof(f1), b, bar1);
		copy_trim(f2, sizeof(f2), bar1 + 1, bar2);
		copy_trim(f3, sizeof(f3), bar2 + 1, e);
		nf = 3;
	} else if (bar1) {
		copy_trim(f1, sizeof(f1), b, bar1);
		copy_trim(f3, sizeof(f3), bar1 + 1, e);
		f2[0] = '\0';
		nf = 2;
	} else {
		copy_trim(f3, sizeof(f3), b, e);
		f1[0] = '\0';
		f2[0] = '\0';
	}

	if (!f3[0])
		return;

	{
		cf_src_type t;
		const char *name = NULL;

		if (nf == 3) {
			/* f1 ist der Typ, f2 der Name */
			if (!strcmp(f1, "autoidx"))       t = CF_SRC_AUTOIDX;
			else if (!strcmp(f1, "manifest")) t = CF_SRC_MANIFEST;
			else                              t = cf_src_guess(f3);
			name = f2;
		} else if (nf == 2) {
			/* Entweder "typ | url" oder "name | url". Wenn f1 ein
			 * bekannter Typ ist, gilt es als Typ. */
			if (!strcmp(f1, "autoidx"))       { t = CF_SRC_AUTOIDX;  name = NULL; }
			else if (!strcmp(f1, "manifest")) { t = CF_SRC_MANIFEST; name = NULL; }
			else                              { t = cf_src_guess(f3); name = f1; }
		} else {
			t = cf_src_guess(f3);
		}

		cf_sources_add(s, t, name, f3);
	}
}

size_t cf_sources_parse(cf_sources *s, const char *text)
{
	const char *p = text;

	s->n = 0;
	if (!text)
		return 0;

	while (*p) {
		const char *b = p;
		const char *e;

		while (*p && *p != '\n')
			p++;
		e = p;
		if (*p == '\n')
			p++;

		/* fuehrende Leerzeichen weg, dann Kommentare ueberspringen */
		while (b < e && (*b == ' ' || *b == '\t'))
			b++;
		if (b >= e || *b == '#' || *b == ';')
			continue;

		parse_line(s, b, e);
		if (s->n >= CF_MAX_SOURCES)
			break;
	}

	return s->n;
}

size_t cf_sources_serialize(const cf_sources *s, char *dst, size_t dstsz)
{
	static const char head[] =
		"# CubeFetch - Quellen / sources\n"
		"# Format:  <typ> | <name> | <url>\n"
		"# typ: autoidx | manifest\n\n";
	size_t need = 0, o = 0;
	size_t i;

	/* Ein kleiner Helfer waere hier huebscher, aber so bleibt es ohne
	 * Allokation und zaehlt auch bei zu kleinem Puffer korrekt weiter. */
	#define PUT(fmt, ...) do {                                        \
		int r = snprintf(dst + (o < dstsz ? o : dstsz),            \
		                 (o < dstsz) ? dstsz - o : 0,              \
		                 fmt, __VA_ARGS__);                        \
		if (r > 0) { need += (size_t)r; o += (size_t)r; }          \
	} while (0)

	PUT("%s", head);
	for (i = 0; i < s->n; i++)
		PUT("%s | %s | %s\n", cf_src_type_name(s->v[i].type),
		    s->v[i].name, s->v[i].url);

	#undef PUT

	if (dstsz)
		dst[(o < dstsz) ? o : dstsz - 1] = '\0';
	return need;
}
