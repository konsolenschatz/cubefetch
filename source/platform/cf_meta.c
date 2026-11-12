#include "platform/cf_meta.h"
#include "platform/cf_http.h"
#include "platform/cf_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>
#include <zlib.h>

/* GAMECUBE=TRUE ist Pflicht: ohne den Parameter liefert GameTDB nur
 * Wii-Titel - gepruefte 5.016 Eintraege, kein einziger GameCube-Titel.
 * Mit ihm sind es 1.708 GameCube-Spiele zusaetzlich, das Archiv waechst
 * von 2,0 auf 2,3 MB. So holt es auch USB Loader GX. */
#define DB_URL      "http://www.gametdb.com/wiitdb.zip?LANG=%s&GAMECUBE=TRUE"
#define COVER_URL   "http://art.gametdb.com/wii/cover/%s/%s.png"
#define COVER_LIMIT (512u * 1024)
#define CACHE_N     12         /* dekodierte Cover im Speicher */
#define MISSING_N   128        /* IDs ohne Cover, fuer diese Sitzung */

static char g_cfgdir[128];
static char g_lang[4];
static cf_gtdb_index g_ix;

typedef struct {
	char   id[8];
	cf_tex tex;
	unsigned stamp;
} cover_slot;

static cover_slot g_cache[CACHE_N];
static unsigned   g_stamp;
static char       g_missing[MISSING_N][8];
static size_t     g_nmissing;

/* ------------------------------------------------------------------ */
/* Pfade                                                              */
/* ------------------------------------------------------------------ */

static void db_path(char *dst, size_t dstsz, int tmp)
{
	snprintf(dst, dstsz, "%sgametdb/games-%s.tsv%s", g_cfgdir, g_lang,
	         tmp ? ".tmp" : "");
}

static void cover_path(char *dst, size_t dstsz, const char *id)
{
	snprintf(dst, dstsz, "%scovers/%.7s.png", g_cfgdir, id);
}

/* ------------------------------------------------------------------ */
/* Index                                                              */
/* ------------------------------------------------------------------ */

int cf_meta_init(const char *cfgdir, const char *lang)
{
	char path[256];
	char *txt;

	snprintf(g_cfgdir, sizeof(g_cfgdir), "%s", cfgdir ? cfgdir : "");
	snprintf(g_lang, sizeof(g_lang), "%.2s", lang && *lang ? lang : "EN");

	cf_gtdb_index_free(&g_ix);

	db_path(path, sizeof(path), 0);
	txt = cf_read_text(path, NULL);
	if (!txt)
		return 0;

	cf_gtdb_index_load(&g_ix, txt);
	free(txt);
	return g_ix.n > 0;
}

void cf_meta_free(void)
{
	size_t i;

	cf_gtdb_index_free(&g_ix);
	for (i = 0; i < CACHE_N; i++) {
		cf_gfx_tex_free(&g_cache[i].tex);
		g_cache[i].id[0] = '\0';
	}
	g_nmissing = 0;
}

size_t cf_meta_count(void)
{
	return g_ix.n;
}

const cf_gtdb_game *cf_meta_lookup(const char *id)
{
	if (!id || !*id)
		return NULL;
	return cf_gtdb_index_find(&g_ix, id);
}

const cf_gtdb_game *cf_meta_guess(const char *title, double *score)
{
	return cf_gtdb_index_fuzzy(&g_ix, title, score);
}

/* ------------------------------------------------------------------ */
/* Datenbank holen: Zip im Strom entpacken und parsen                 */
/* ------------------------------------------------------------------ */

typedef struct {
	/* Zip-Kopf des ersten Eintrags: 30 Bytes fest, dann Name und Extra. */
	unsigned char hdr[512];
	size_t   hdr_len, hdr_need;
	int      hdr_done;
	int      method;        /* 0 = gespeichert, 8 = deflate */
	int      finished;      /* erster Eintrag zu Ende, Rest egal */

	z_stream z;
	int      z_live;
	unsigned char out[16384];

	cf_gtdb_xml *xml;
	FILE    *f;
	size_t   games;

	uint64_t in_bytes, in_total;
	cf_meta_cb cb;
	void    *ctx;
	volatile int *cancel;
	int      err;
} zipstate;

static int game_cb(void *ctx, const cf_gtdb_game *g)
{
	zipstate *zs = ctx;
	char line[512];
	int n = cf_gtdb_line(g, line, sizeof(line));

	if (n <= 0)
		return 0;
	if (fwrite(line, 1, (size_t)n, zs->f) != (size_t)n) {
		zs->err = CF_META_EFILE;
		return -1;
	}
	zs->games++;
	return 0;
}

static uint16_t le16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static int feed_xml(zipstate *zs, const unsigned char *p, size_t n)
{
	if (cf_gtdb_xml_feed(zs->xml, (const char *)p, n) < 0) {
		if (!zs->err)
			zs->err = CF_META_EFILE;
		return -1;
	}
	return 0;
}

static int zip_sink(void *ctx, const void *data, size_t len)
{
	zipstate *zs = ctx;
	const unsigned char *p = data;

	if (zs->cancel && *zs->cancel)
		return -1;

	zs->in_bytes += len;
	if (zs->cb)
		zs->cb(zs->ctx, "db", zs->in_bytes, zs->in_total);

	if (zs->finished)
		return 0;

	while (len) {
		if (!zs->hdr_done) {
			size_t take;

			/* Erst 30 Bytes, dann weiss man, wie lang der Rest ist. */
			if (zs->hdr_need == 0)
				zs->hdr_need = 30;
			take = zs->hdr_need - zs->hdr_len;
			if (take > len)
				take = len;
			if (zs->hdr_len + take > sizeof(zs->hdr)) {
				zs->err = CF_META_EZIP;
				return -1;
			}
			memcpy(zs->hdr + zs->hdr_len, p, take);
			zs->hdr_len += take;
			p += take;
			len -= take;

			if (zs->hdr_len < zs->hdr_need)
				continue;

			if (zs->hdr_need == 30) {
				if (zs->hdr[0] != 'P' || zs->hdr[1] != 'K' ||
				    zs->hdr[2] != 3 || zs->hdr[3] != 4) {
					zs->err = CF_META_EZIP;
					return -1;
				}
				zs->method = le16(zs->hdr + 8);
				zs->hdr_need = 30 + le16(zs->hdr + 26) + le16(zs->hdr + 28);
				if (zs->hdr_need > sizeof(zs->hdr)) {
					zs->err = CF_META_EZIP;
					return -1;
				}
				if (zs->hdr_len < zs->hdr_need)
					continue;
			}

			/* Kopf vollstaendig. */
			if (zs->method == 8) {
				memset(&zs->z, 0, sizeof(zs->z));
				/* -MAX_WBITS: roher Deflate-Strom ohne zlib-Kopf, so
				 * liegen die Daten im Zip. */
				if (inflateInit2(&zs->z, -MAX_WBITS) != Z_OK) {
					zs->err = CF_HTTP_EMEM;
					return -1;
				}
				zs->z_live = 1;
			} else if (zs->method != 0) {
				zs->err = CF_META_EZIP;
				return -1;
			}
			zs->hdr_done = 1;
			continue;
		}

		if (zs->method == 0) {
			if (feed_xml(zs, p, len) < 0)
				return -1;
			return 0;
		}

		zs->z.next_in = (Bytef *)p;
		zs->z.avail_in = (uInt)len;

		while (zs->z.avail_in > 0) {
			int r;

			zs->z.next_out = zs->out;
			zs->z.avail_out = sizeof(zs->out);
			r = inflate(&zs->z, Z_NO_FLUSH);

			if (r != Z_OK && r != Z_STREAM_END) {
				zs->err = CF_META_EZIP;
				return -1;
			}

			if (feed_xml(zs, zs->out, sizeof(zs->out) - zs->z.avail_out) < 0)
				return -1;

			if (r == Z_STREAM_END) {
				zs->finished = 1;
				return 0;
			}
		}
		return 0;
	}

	return 0;
}

int cf_meta_update(cf_meta_cb cb, void *ctx, volatile int *cancel)
{
	zipstate *zs;
	cf_http_info info;
	char url[160], tmp[256], final[256], dir[256];
	int r;

	zs = calloc(1, sizeof(*zs));
	if (!zs)
		return CF_HTTP_EMEM;

	snprintf(dir, sizeof(dir), "%sgametdb", g_cfgdir);
	cf_mkdir_p(dir);
	db_path(tmp, sizeof(tmp), 1);
	db_path(final, sizeof(final), 0);

	zs->f = fopen(tmp, "wb");
	if (!zs->f) {
		free(zs);
		return CF_META_EFILE;
	}

	zs->xml = cf_gtdb_xml_new(game_cb, zs, g_lang);
	if (!zs->xml) {
		fclose(zs->f);
		free(zs);
		return CF_HTTP_EMEM;
	}

	zs->cb = cb;
	zs->ctx = ctx;
	zs->cancel = cancel;

	snprintf(url, sizeof(url), DB_URL, g_lang);
	memset(&info, 0, sizeof(info));

	/* Die Gesamtgroesse steht erst nach den Kopfzeilen fest; info wird
	 * von cf_http vor dem ersten Datenblock gefuellt, der Zeiger reicht. */
	zs->in_total = 0;
	r = cf_http_get_range(url, 0, zip_sink, zs, &info, cancel);
	if (info.total)
		zs->in_total = info.total;

	if (zs->z_live)
		inflateEnd(&zs->z);
	cf_gtdb_xml_free(zs->xml);

	if (fclose(zs->f) != 0 && !zs->err)
		zs->err = CF_META_EFILE;

	if (zs->err)
		r = zs->err;
	else if (r == 200 || r == 206)
		r = 0;
	else if (r >= 0)
		r = CF_HTTP_EPROTO;

	/* Archiv in Ordnung, aber kein GameCube-Spiel darin: das ist ein
	 * anderer Fehler als ein kaputtes Archiv und soll auch so heissen. */
	if (r == 0 && zs->games == 0)
		r = CF_META_EEMPTY;

	if (r == 0) {
		remove(final);
		if (rename(tmp, final) != 0)
			r = CF_META_EFILE;
	} else {
		remove(tmp);
	}

	free(zs);

	if (r == 0)
		cf_meta_init(g_cfgdir, g_lang);

	return r;
}

/* ------------------------------------------------------------------ */
/* Cover                                                              */
/* ------------------------------------------------------------------ */

/* PNG aus dem Speicher nach RGBA8. */
typedef struct {
	const unsigned char *p;
	size_t len, pos;
} memreader;

static void png_mem_read(png_structp png, png_bytep out, png_size_t n)
{
	memreader *m = png_get_io_ptr(png);

	if (m->pos + n > m->len) {
		png_error(png, "kurz");
		return;
	}
	memcpy(out, m->p + m->pos, n);
	m->pos += n;
}

static int png_decode(const unsigned char *data, size_t len, cf_tex *out)
{
	png_structp png;
	png_infop info;
	memreader m = { data, len, 0 };
	png_uint_32 w, h;
	int depth, ctype, y;
	unsigned char *rgba = NULL;
	png_bytep *rows = NULL;
	int ok = 0;

	if (len < 8 || png_sig_cmp(data, 0, 8) != 0)
		return CF_META_EPNG;

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png)
		return CF_HTTP_EMEM;
	info = png_create_info_struct(png);
	if (!info) {
		png_destroy_read_struct(&png, NULL, NULL);
		return CF_HTTP_EMEM;
	}

	if (setjmp(png_jmpbuf(png))) {
		free(rgba);
		free(rows);
		png_destroy_read_struct(&png, &info, NULL);
		return CF_META_EPNG;
	}

	png_set_read_fn(png, &m, png_mem_read);
	png_read_info(png, info);
	png_get_IHDR(png, info, &w, &h, &depth, &ctype, NULL, NULL, NULL);

	if (w == 0 || h == 0 || w > 1024 || h > 1024)
		png_error(png, "gross");

	/* Alles auf RGBA8 bringen, egal was drinsteht. */
	if (ctype == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (ctype == PNG_COLOR_TYPE_GRAY && depth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (depth == 16)
		png_set_strip_16(png);
	if (ctype == PNG_COLOR_TYPE_GRAY || ctype == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
	png_set_interlace_handling(png);
	png_read_update_info(png, info);

	rgba = malloc((size_t)w * h * 4);
	rows = malloc((size_t)h * sizeof(*rows));
	if (!rgba || !rows)
		png_error(png, "speicher");

	for (y = 0; y < (int)h; y++)
		rows[y] = rgba + (size_t)y * w * 4;

	png_read_image(png, rows);
	png_destroy_read_struct(&png, &info, NULL);

	ok = cf_gfx_tex_rgba8(out, (int)w, (int)h, rgba);
	free(rows);
	free(rgba);
	return ok ? 0 : CF_HTTP_EMEM;
}

/* Sprachordner, die fuer diese ID probiert werden. */
static int cover_langs(const char *id, const char *out[6])
{
	int n = 0;
	char region = id[3];

	/* Oberflaechensprache zuerst, damit die deutsche Packung kommt,
	 * wenn es sie gibt. */
	if (!strcmp(g_lang, "DE"))
		out[n++] = "DE";

	switch (region) {
	case 'E': out[n++] = "US"; out[n++] = "EN"; break;
	case 'P': out[n++] = "EN"; out[n++] = "US"; break;
	case 'D': out[n++] = "DE"; out[n++] = "EN"; out[n++] = "US"; break;
	case 'F': out[n++] = "FR"; out[n++] = "EN"; out[n++] = "US"; break;
	case 'S': out[n++] = "ES"; out[n++] = "EN"; out[n++] = "US"; break;
	case 'I': out[n++] = "IT"; out[n++] = "EN"; out[n++] = "US"; break;
	case 'J': out[n++] = "JA"; out[n++] = "US"; out[n++] = "EN"; break;
	default:  out[n++] = "EN"; out[n++] = "US"; break;
	}
	return n;
}

int cf_meta_cover_missing(const char *id)
{
	size_t i;

	for (i = 0; i < g_nmissing; i++)
		if (!strcmp(g_missing[i], id))
			return 1;
	return 0;
}

static void mark_missing(const char *id)
{
	if (cf_meta_cover_missing(id))
		return;
	if (g_nmissing < MISSING_N) {
		snprintf(g_missing[g_nmissing], 8, "%.7s", id);
		g_nmissing++;
	}
}

static cover_slot *cache_find(const char *id)
{
	size_t i;

	for (i = 0; i < CACHE_N; i++)
		if (g_cache[i].id[0] && !strcmp(g_cache[i].id, id))
			return &g_cache[i];
	return NULL;
}

/* Den am laengsten nicht benutzten Platz freimachen. */
static cover_slot *cache_slot(void)
{
	size_t i, best = 0;

	for (i = 0; i < CACHE_N; i++) {
		if (!g_cache[i].id[0])
			return &g_cache[i];
		if (g_cache[i].stamp < g_cache[best].stamp)
			best = i;
	}
	cf_gfx_tex_free(&g_cache[best].tex);
	g_cache[best].id[0] = '\0';
	return &g_cache[best];
}

const cf_tex *cf_meta_cover(const char *id, int level)
{
	int allow_net = (level >= CF_COVER_NET);
	char path[256];
	cover_slot *s;
	char *png = NULL;
	size_t len = 0;
	int r = CF_META_ENONE;

	if (!id || strlen(id) < 4 || !g_cfgdir[0])
		return NULL;

	s = cache_find(id);
	if (s) {
		s->stamp = ++g_stamp;
		return &s->tex;
	}

	if (level < CF_COVER_CARD)
		return NULL;
	if (cf_meta_cover_missing(id) && !allow_net)
		return NULL;

	/* Von der Karte */
	cover_path(path, sizeof(path), id);
	png = cf_read_text(path, &len);

	/* Vom Netz, in der Sprachkette */
	if (!png && allow_net && !cf_meta_cover_missing(id)) {
		const char *langs[6];
		int n = cover_langs(id, langs), i;
		char tried[6][4];
		int ntried = 0;

		for (i = 0; i < n && !png; i++) {
			char url[160];
			cf_http_info info;
			int k, dup = 0;

			for (k = 0; k < ntried; k++)
				if (!strcmp(tried[k], langs[i]))
					dup = 1;
			if (dup)
				continue;
			snprintf(tried[ntried++], 4, "%s", langs[i]);

			snprintf(url, sizeof(url), COVER_URL, langs[i], id);
			memset(&info, 0, sizeof(info));
			r = cf_http_get_mem(url, COVER_LIMIT, &png, &len, &info);
			if (r != 200) {
				free(png);
				png = NULL;
			}
		}

		if (png) {
			char dir[256];
			FILE *f;

			snprintf(dir, sizeof(dir), "%scovers", g_cfgdir);
			cf_mkdir_p(dir);
			f = fopen(path, "wb");
			if (f) {
				fwrite(png, 1, len, f);
				fclose(f);
			}
		} else {
			mark_missing(id);
		}
	}

	if (!png)
		return NULL;

	s = cache_slot();
	r = png_decode((const unsigned char *)png, len, &s->tex);
	free(png);

	if (r != 0) {
		/* Kaputte Datei auf der Karte: weg damit, sonst bleibt sie
		 * fuer immer kaputt. */
		remove(path);
		mark_missing(id);
		return NULL;
	}

	snprintf(s->id, sizeof(s->id), "%.7s", id);
	s->stamp = ++g_stamp;
	return &s->tex;
}
