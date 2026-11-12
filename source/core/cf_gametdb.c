#include "core/cf_gametdb.h"
#include "core/cf_fuzzy.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Kleine Helfer                                                      */
/* ------------------------------------------------------------------ */

/* XML-Entities aufloesen, in place ist nicht noetig: src -> dst. */
static void unescape(const char *src, size_t len, char *dst, size_t dstsz)
{
	size_t o = 0, i = 0;

	if (!dstsz)
		return;

	while (i < len && o + 4 < dstsz) {
		if (src[i] == '&') {
			const char *rest = src + i;
			size_t left = len - i;

			if (left >= 5 && !strncmp(rest, "&amp;", 5))       { dst[o++] = '&'; i += 5; continue; }
			if (left >= 4 && !strncmp(rest, "&lt;", 4))        { dst[o++] = '<'; i += 4; continue; }
			if (left >= 4 && !strncmp(rest, "&gt;", 4))        { dst[o++] = '>'; i += 4; continue; }
			if (left >= 6 && !strncmp(rest, "&quot;", 6))      { dst[o++] = '"'; i += 6; continue; }
			if (left >= 6 && !strncmp(rest, "&apos;", 6))      { dst[o++] = '\''; i += 6; continue; }
			if (left >= 4 && rest[1] == '#') {
				/* &#123; oder &#x7B; -> UTF-8 */
				unsigned long cp;
				char *end;
				cp = (rest[2] == 'x' || rest[2] == 'X')
				   ? strtoul(rest + 3, &end, 16)
				   : strtoul(rest + 2, &end, 10);
				if (*end == ';') {
					if (cp < 0x80) {
						dst[o++] = (char)cp;
					} else if (cp < 0x800) {
						dst[o++] = (char)(0xC0 | (cp >> 6));
						dst[o++] = (char)(0x80 | (cp & 0x3F));
					} else if (cp < 0x10000) {
						dst[o++] = (char)(0xE0 | (cp >> 12));
						dst[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
						dst[o++] = (char)(0x80 | (cp & 0x3F));
					}
					i += (size_t)(end - rest) + 1;
					continue;
				}
			}
		}
		/* Tabulator und Zeilenumbruch wuerden das Zeilenformat stoeren. */
		dst[o++] = (src[i] == '\t' || src[i] == '\n' || src[i] == '\r')
		         ? ' ' : src[i];
		i++;
	}
	dst[o] = '\0';
}

/* Sucht needle in [p, end). */
static const char *find(const char *p, const char *end, const char *needle)
{
	size_t n = strlen(needle);

	while (p + n <= end) {
		if (*p == needle[0] && !memcmp(p, needle, n))
			return p;
		p++;
	}
	return NULL;
}

/* Text zwischen <tag> und </tag> innerhalb [p, end). 1 wenn gefunden. */
static int tag_text(const char *p, const char *end, const char *open,
                    const char *close, char *dst, size_t dstsz)
{
	const char *s = find(p, end, open);
	const char *e;

	dst[0] = '\0';
	if (!s)
		return 0;
	s += strlen(open);
	e = find(s, end, close);
	if (!e)
		return 0;

	/* GameTDB hat vereinzelt Leerraum am Rand ("Electronic Arts "). */
	while (s < e && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'))
		s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		e--;

	unescape(s, (size_t)(e - s), dst, dstsz);
	return 1;
}

/* Attributwert name="..." innerhalb eines Tags [p, end). */
static int attr(const char *p, const char *end, const char *name,
                char *dst, size_t dstsz)
{
	char key[32];
	const char *s, *e;

	snprintf(key, sizeof(key), " %s=\"", name);
	s = find(p, end, key);
	dst[0] = '\0';
	if (!s)
		return 0;
	s += strlen(key);
	e = find(s, end, "\"");
	if (!e)
		return 0;
	unescape(s, (size_t)(e - s), dst, dstsz);
	return 1;
}

/* ------------------------------------------------------------------ */
/* XML-Strom                                                          */
/* ------------------------------------------------------------------ */

/* Ein <game>-Block mit Beschreibungstext kann einige KB gross werden;
 * darueber wird er verworfen statt den Speicher wachsen zu lassen. */
#define BLOCK_CAP (96 * 1024)

struct cf_gtdb_xml {
	cf_gtdb_game_cb cb;
	void           *ctx;
	char            lang[4];
	char           *buf;
	size_t          len;
	size_t          count;
	int             stopped;
};

cf_gtdb_xml *cf_gtdb_xml_new(cf_gtdb_game_cb cb, void *ctx, const char *lang)
{
	cf_gtdb_xml *x = calloc(1, sizeof(*x));

	if (!x)
		return NULL;
	x->buf = malloc(BLOCK_CAP);
	if (!x->buf) {
		free(x);
		return NULL;
	}
	x->cb = cb;
	x->ctx = ctx;
	snprintf(x->lang, sizeof(x->lang), "%.2s", lang && *lang ? lang : "EN");
	return x;
}

void cf_gtdb_xml_free(cf_gtdb_xml *x)
{
	if (!x)
		return;
	free(x->buf);
	free(x);
}

size_t cf_gtdb_xml_count(const cf_gtdb_xml *x)
{
	return x ? x->count : 0;
}

/* Titel in der Wunschsprache, sonst EN, sonst der erste. */
static void pick_title(const char *p, const char *end, const char *lang,
                       char *dst, size_t dstsz)
{
	char open[32];
	const char *loc;

	dst[0] = '\0';

	snprintf(open, sizeof(open), "<locale lang=\"%s\">", lang);
	loc = find(p, end, open);
	if (loc && tag_text(loc, end, "<title>", "</title>", dst, dstsz) && dst[0])
		return;

	loc = find(p, end, "<locale lang=\"EN\">");
	if (loc && tag_text(loc, end, "<title>", "</title>", dst, dstsz) && dst[0])
		return;

	loc = find(p, end, "<locale ");
	if (loc)
		tag_text(loc, end, "<title>", "</title>", dst, dstsz);
}

/* Parst einen Block [p, end) von "<game" bis einschliesslich "</game>".
 * 1 wenn es ein GameCube-Spiel war und cb gerufen wurde. */
static int parse_block(cf_gtdb_xml *x, const char *p, const char *end)
{
	cf_gtdb_game g;
	char tmp[64];
	const char *d;

	memset(&g, 0, sizeof(g));

	/* Nur GameCube. Wii-Titel haben ein leeres oder fehlendes type. */
	if (!tag_text(p, end, "<type>", "</type>", tmp, sizeof(tmp)) ||
	    strcmp(tmp, "GameCube") != 0)
		return 0;

	if (!tag_text(p, end, "<id>", "</id>", tmp, sizeof(tmp)) || strlen(tmp) < 4)
		return 0;
	snprintf(g.id, sizeof(g.id), "%.6s", tmp);

	pick_title(p, end, x->lang, g.title, sizeof(g.title));
	if (!g.title[0]) {
		/* Notnagel: das name-Attribut des game-Tags */
		attr(p, find(p, end, ">") ? find(p, end, ">") : end, "name",
		     g.title, sizeof(g.title));
	}

	tag_text(p, end, "<developer>", "</developer>", g.developer, sizeof(g.developer));
	tag_text(p, end, "<publisher>", "</publisher>", g.publisher, sizeof(g.publisher));
	tag_text(p, end, "<genre>", "</genre>", g.genre, sizeof(g.genre));

	d = find(p, end, "<date ");
	if (d) {
		const char *de = find(d, end, ">");
		if (de) {
			if (attr(d, de, "year", tmp, sizeof(tmp)))  g.year  = (uint16_t)atoi(tmp);
			if (attr(d, de, "month", tmp, sizeof(tmp))) g.month = (uint8_t)atoi(tmp);
			if (attr(d, de, "day", tmp, sizeof(tmp)))   g.day   = (uint8_t)atoi(tmp);
		}
	}

	d = find(p, end, "<input ");
	if (d) {
		const char *de = find(d, end, ">");
		if (de && attr(d, de, "players", tmp, sizeof(tmp)))
			g.players = (uint8_t)atoi(tmp);
	}

	x->count++;
	if (x->cb && x->cb(x->ctx, &g) < 0)
		x->stopped = 1;
	return 1;
}

int cf_gtdb_xml_feed(cf_gtdb_xml *x, const char *data, size_t len)
{
	if (!x || x->stopped)
		return -1;

	while (len) {
		size_t room = BLOCK_CAP - x->len;
		size_t take = len < room ? len : room;
		const char *start, *stop;

		memcpy(x->buf + x->len, data, take);
		x->len += take;
		data += take;
		len -= take;

		/* So viele vollstaendige Bloecke verarbeiten, wie im Puffer sind. */
		for (;;) {
			start = find(x->buf, x->buf + x->len, "<game ");
			if (!start) {
				/* Nichts Brauchbares: alles bis auf einen moeglichen
				 * angefangenen Tag verwerfen. */
				size_t keep = x->len < 8 ? x->len : 8;
				memmove(x->buf, x->buf + x->len - keep, keep);
				x->len = keep;
				break;
			}

			stop = find(start, x->buf + x->len, "</game>");
			if (!stop) {
				/* Block noch unvollstaendig: Vorlauf verwerfen. */
				size_t off = (size_t)(start - x->buf);
				if (off) {
					memmove(x->buf, start, x->len - off);
					x->len -= off;
				}
				if (x->len >= BLOCK_CAP) {
					/* Zu gross fuer einen Block: aufgeben und hinter
					 * dem Anfang weitersuchen. */
					memmove(x->buf, x->buf + 6, x->len - 6);
					x->len -= 6;
				}
				break;
			}

			stop += 7;   /* hinter "</game>" */
			parse_block(x, start, stop);
			if (x->stopped)
				return -1;

			memmove(x->buf, stop, (size_t)(x->buf + x->len - stop));
			x->len = (size_t)(x->buf + x->len - stop);
		}
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Zeilenformat                                                       */
/* ------------------------------------------------------------------ */

int cf_gtdb_line(const cf_gtdb_game *g, char *dst, size_t dstsz)
{
	int n = snprintf(dst, dstsz, "%s\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\n",
	                 g->id, (unsigned)g->year, (unsigned)g->month,
	                 (unsigned)g->day, (unsigned)g->players,
	                 g->publisher, g->developer, g->genre, g->title);
	return (n > 0 && (size_t)n < dstsz) ? n : -1;
}

/* Naechstes Tab-Feld; schneidet auf dstsz. Rueckgabe: Zeiger hinter das
 * Trennzeichen oder NULL am Zeilenende. */
static const char *field(const char *p, char *dst, size_t dstsz)
{
	size_t n = 0;

	while (*p && *p != '\t' && *p != '\n' && *p != '\r') {
		if (n + 1 < dstsz)
			dst[n++] = *p;
		p++;
	}
	dst[n] = '\0';
	return (*p == '\t') ? p + 1 : NULL;
}

int cf_gtdb_parse_line(const char *line, cf_gtdb_game *g)
{
	char tmp[16];
	const char *p = line;

	memset(g, 0, sizeof(*g));

	p = field(p, g->id, sizeof(g->id));
	if (!p || strlen(g->id) < 4) return 0;
	p = field(p, tmp, sizeof(tmp)); if (!p) return 0; g->year = (uint16_t)atoi(tmp);
	p = field(p, tmp, sizeof(tmp)); if (!p) return 0; g->month = (uint8_t)atoi(tmp);
	p = field(p, tmp, sizeof(tmp)); if (!p) return 0; g->day = (uint8_t)atoi(tmp);
	p = field(p, tmp, sizeof(tmp)); if (!p) return 0; g->players = (uint8_t)atoi(tmp);
	p = field(p, g->publisher, sizeof(g->publisher)); if (!p) return 0;
	p = field(p, g->developer, sizeof(g->developer)); if (!p) return 0;
	p = field(p, g->genre, sizeof(g->genre)); if (!p) return 0;
	field(p, g->title, sizeof(g->title));
	return 1;
}

/* ------------------------------------------------------------------ */
/* Index                                                              */
/* ------------------------------------------------------------------ */

int cf_gtdb_index_add(cf_gtdb_index *ix, const cf_gtdb_game *g)
{
	if (ix->n == ix->cap) {
		size_t ncap = ix->cap ? ix->cap * 2 : 512;
		cf_gtdb_game *nv = realloc(ix->v, ncap * sizeof(*nv));
		if (!nv)
			return 0;
		ix->v = nv;
		ix->cap = ncap;
	}
	ix->v[ix->n++] = *g;
	ix->sorted = 0;
	return 1;
}

static int cmp_id(const void *a, const void *b)
{
	return strcmp(((const cf_gtdb_game *)a)->id, ((const cf_gtdb_game *)b)->id);
}

void cf_gtdb_index_sort(cf_gtdb_index *ix)
{
	size_t i;

	if (ix->n > 1)
		qsort(ix->v, ix->n, sizeof(*ix->v), cmp_id);
	ix->sorted = 1;

	/* Normalisierte Titel einmal vorab, in derselben Reihenfolge. Die
	 * Namenssuche vergleicht einen Titel gegen alle 2.500; ohne das
	 * wuerde jeder Vergleich beide Seiten neu normalisieren. */
	free(ix->norm);
	ix->norm = ix->n ? malloc(ix->n * sizeof(*ix->norm)) : NULL;
	if (ix->norm)
		for (i = 0; i < ix->n; i++)
			cf_fuzzy_normalize(ix->norm[i], sizeof(ix->norm[i]),
			                   ix->v[i].title);
}

const cf_gtdb_game *cf_gtdb_index_find(const cf_gtdb_index *ix, const char *id)
{
	size_t lo = 0, hi;

	if (!ix || !ix->n || !id || !ix->sorted)
		return NULL;

	hi = ix->n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		int c = strcmp(ix->v[mid].id, id);

		if (c == 0)
			return &ix->v[mid];
		if (c < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return NULL;
}

void cf_gtdb_index_free(cf_gtdb_index *ix)
{
	free(ix->norm);
	free(ix->v);
	memset(ix, 0, sizeof(*ix));
}

size_t cf_gtdb_index_load(cf_gtdb_index *ix, const char *txt)
{
	const char *p = txt;
	size_t n = 0;

	while (p && *p) {
		cf_gtdb_game g;
		const char *nl = strchr(p, '\n');

		if (cf_gtdb_parse_line(p, &g) && cf_gtdb_index_add(ix, &g))
			n++;

		p = nl ? nl + 1 : NULL;
	}

	cf_gtdb_index_sort(ix);
	return n;
}

const cf_gtdb_game *cf_gtdb_index_fuzzy(const cf_gtdb_index *ix,
                                        const char *title, double *score)
{
	const cf_gtdb_game *best = NULL;
	double best_s = 0.0;
	char nq[256];
	size_t i;

	if (score)
		*score = 0.0;
	if (!ix || !title || !*title)
		return NULL;

	cf_fuzzy_normalize(nq, sizeof(nq), title);

	for (i = 0; i < ix->n; i++) {
		double s = ix->norm ? cf_fuzzy_score_norm(nq, ix->norm[i])
		                    : cf_fuzzy_score(title, ix->v[i].title);

		/* Bei Gleichstand der kuerzere Datenbanktitel: der laengere hat
		 * nur zusaetzliche Woerter, die der Dateiname nicht kennt. */
		if (s > best_s ||
		    (s == best_s && best && strlen(ix->v[i].title) < strlen(best->title))) {
			best_s = s;
			best = &ix->v[i];
		}
	}

	if (best_s < CF_FUZZY_SUGGEST)
		return NULL;
	if (score)
		*score = best_s;
	return best;
}
