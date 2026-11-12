/* CubeFetch - Netzwerk-Downloader fuer den GameCube.
 *
 * Dieses Modul haelt den Zustand der Anwendung und die Bildschirme. Die
 * Mechanik (Katalog, Warteschlange, Download, Netz) steckt in core/ und
 * platform/; hier wird nur gezeichnet und auf Tasten reagiert.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <ogc/exi.h>
#include <ogc/lwp_watchdog.h>

#include "core/cf_catalog.h"
#include "core/cf_i18n.h"
#include "core/cf_queue.h"
#include "core/cf_qr.h"
#include "en_lang.h"
#include "core/cf_sources.h"
#include "core/cf_srcparse.h"
#include "core/cf_str.h"
#include "platform/cf_dns.h"
#include "platform/cf_download.h"
#include "platform/cf_http.h"
#include "platform/cf_exi.h"
#include "platform/cf_meta.h"
#include "platform/cf_net.h"
#include "platform/cf_storage.h"
#include "platform/cf_ui.h"

/* Groesste Antwort einer Quelle, die in den Speicher geladen wird. */
#define META_LIMIT (6u * 1024 * 1024)

typedef struct {
	cf_catalog *cat;
	cf_sources  src;
	cf_queue    q;

	/* Basisadresse je Quelle, aus der die Datei-URLs gebaut werden. */
	char        base[CF_MAX_SOURCES][288];

	char       *lang_buf;     /* muss leben, solange die Tabelle gilt */
	cf_lang     lang;

	size_t      dev;          /* Index in cf_storage */
	char        cfgdir[128];  /* "sd2:/cubefetch/" */
	char        dir[256];     /* Zielordner fuer Images */

	int         verify;
	int         conns;        /* gleichzeitige Verbindungen, 1..CF_DL_MAX_CONN */
	int         cat_view;     /* 0 Liste + Detailfeld, 1 Liste, 2 Coverflow */

	cf_net_info net;
} app;

static app g_app;

/* ------------------------------------------------------------------ */
/* Pfade, Konfiguration                                               */
/* ------------------------------------------------------------------ */

static void paths_for_device(app *a, size_t dev)
{
	const cf_device *d = cf_storage_at(dev);

	if (!d) {
		a->cfgdir[0] = '\0';
		a->dir[0] = '\0';
		return;
	}

	a->dev = dev;
	snprintf(a->cfgdir, sizeof(a->cfgdir), "%scubefetch/", d->root);
	snprintf(a->dir, sizeof(a->dir), "%sgames/", d->root);
}

static void cfgpath(const app *a, const char *name, char *dst, size_t dstsz)
{
	snprintf(dst, dstsz, "%s%s", a->cfgdir, name);
}

static void config_load(app *a)
{
	char path[192];
	char *txt, *p;

	cfgpath(a, "config.txt", path, sizeof(path));
	txt = cf_read_text(path, NULL);
	if (!txt)
		return;

	p = txt;
	while (*p) {
		char *line = p;

		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			*p++ = '\0';
		{
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\r' || line[l - 1] == ' '))
				line[--l] = '\0';
		}

		if (!strncmp(line, "lang=", 5))
			a->lang = cf_lang_from_code(line + 5);
		else if (!strncmp(line, "dir=", 4) && line[4])
			snprintf(a->dir, sizeof(a->dir), "%s", line + 4);
		else if (!strncmp(line, "verify=", 7))
			a->verify = atoi(line + 7) ? 1 : 0;
		else if (!strncmp(line, "dns=", 4) && line[4])
			cf_dns_set_server(line + 4);
		else if (!strncmp(line, "theme=", 6))
			cf_ui_set_theme(cf_ui_theme_from_code(line + 6));
		else if (!strncmp(line, "panel=", 6))
			a->cat_view = atoi(line + 6) ? 0 : 1;   /* alte Konfiguration */
		else if (!strncmp(line, "view=", 5))
			a->cat_view = atoi(line + 5) % 3;
		else if (!strncmp(line, "conns=", 6)) {
			int n = atoi(line + 6);
			if (n < 1)
				n = 1;
			if (n > CF_DL_MAX_CONN)
				n = CF_DL_MAX_CONN;
			a->conns = n;
		}
	}

	free(txt);
}

static void config_save(const app *a)
{
	char path[192];
	char buf[512];

	cfgpath(a, "config.txt", path, sizeof(path));
	snprintf(buf, sizeof(buf),
	         "# CubeFetch\n"
	         "lang=%s\n"
	         "dir=%s\n"
	         "verify=%d\n"
	         "conns=%d\n"
	         "view=%d\n"
	         "theme=%s\n"
	         "dns=%s\n",
	         cf_lang_code(a->lang), a->dir, a->verify,
	         a->conns, a->cat_view,
	         cf_ui_theme_code(cf_ui_theme()), cf_dns_server());

	cf_mkdir_p(a->cfgdir);
	cf_write_text(path, buf);
}

static void lang_fallback(void)
{
	static char *buf;

	if (buf)
		return;
	buf = malloc(en_lang_size + 1);
	if (!buf)
		return;
	memcpy(buf, en_lang, en_lang_size);
	buf[en_lang_size] = '\0';
	cf_i18n_load_fallback(buf);
}

static void lang_load(app *a)
{
	char path[192];
	char name[32];

	lang_fallback();
	free(a->lang_buf);
	a->lang_buf = NULL;

	snprintf(name, sizeof(name), "lang/%s.lang", cf_lang_code(a->lang));
	cfgpath(a, name, path, sizeof(path));

	a->lang_buf = cf_read_text(path, NULL);

	/* Japanisch braucht eine Zweitschrift von der Karte; geladen wird
	 * sie nur dann, sie kostet 1,7 MB. Die Schriften bekommen genau die
	 * Zeichen der Sprachdatei - das muss vor cf_i18n_load passieren,
	 * das den Puffer zerlegt. */
	{
		static char  *jp;
		static size_t jplen;

		if (a->lang == CF_LANG_JA && !jp) {
			char fp[192];
			cfgpath(a, "fonts/mplus1p.ttf", fp, sizeof(fp));
			jp = cf_read_text(fp, &jplen);
		}
		cf_ui_reload_fonts(a->lang_buf,
		                   a->lang == CF_LANG_JA ? jp : NULL,
		                   a->lang == CF_LANG_JA ? jplen : 0);
	}

	if (a->lang_buf && cf_i18n_load(a->lang_buf) > 0)
		return;

	free(a->lang_buf);
	a->lang_buf = NULL;
	cf_i18n_builtin();
}

/* ------------------------------------------------------------------ */
/* Quellen und Warteschlange auf der Karte                            */
/* ------------------------------------------------------------------ */

static void sources_load(app *a)
{
	char path[192];
	char *txt;

	cfgpath(a, "sources.txt", path, sizeof(path));
	txt = cf_read_text(path, NULL);
	if (!txt)
		return;

	cf_sources_parse(&a->src, txt);
	free(txt);
}

static void sources_save(const app *a)
{
	char path[192];
	char buf[CF_MAX_SOURCES * (CF_SRC_URL_MAX + CF_SRC_NAME_MAX + 32) + 256];

	cfgpath(a, "sources.txt", path, sizeof(path));
	cf_sources_serialize(&a->src, buf, sizeof(buf));

	cf_mkdir_p(a->cfgdir);
	cf_write_text(path, buf);
}

static void queue_load(app *a)
{
	char path[192];
	char *txt;

	cfgpath(a, "queue.txt", path, sizeof(path));
	txt = cf_read_text(path, NULL);
	if (!txt)
		return;

	cf_queue_parse(&a->q, txt);
	free(txt);
}

static void queue_save(const app *a)
{
	char path[192];
	char *buf;
	size_t need;

	cfgpath(a, "queue.txt", path, sizeof(path));

	need = cf_queue_serialize(&a->q, NULL, 0) + 2;
	buf = malloc(need);
	if (!buf)
		return;

	cf_queue_serialize(&a->q, buf, need);
	cf_mkdir_p(a->cfgdir);
	cf_write_text(path, buf);
	free(buf);
}

/* ------------------------------------------------------------------ */
/* Kleine Helfer                                                      */
/* ------------------------------------------------------------------ */

/* snprintf mit Quelle und Ziel in derselben Struktur loest -Wrestrict
 * aus. Hier ueberlappt nichts, aber memmove sagt das auch dem Compiler. */
static void sstr(char *dst, size_t dstsz, const char *src)
{
	size_t n = strlen(src);

	if (n > dstsz - 1)
		n = dstsz - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

/* Ein Bild mit einer Zeile Text in der Mitte, fuer Wartezustaende. */
static void splash(const char *title, const char *line1, const char *line2)
{
	int y = CF_UI_BODY_Y + 60;

	cf_ui_begin(title, NULL);
	cf_ui_panel(CF_UI_X0, y, CF_UI_W, 90);
	cf_ui_text(CF_UI_X0 + 20, y + 20, CF_S_BODY, line1);
	if (line2)
		cf_ui_text(CF_UI_X0 + 20, y + 20 + cf_ui_line_h(CF_S_BODY) + 8,
		           CF_S_DIM, line2);
	cf_ui_end(NULL);
}

/* Eine Zeile "Name: Wert" im Detailstil. Rueckgabe: naechstes y. */
static int kv_line(int x, int y, int w, const char *key, const char *val,
                   cf_style vst)
{
	int kw = 110;

	cf_ui_text_fit(x, y, kw - 8, CF_S_SMALLDIM, key);
	cf_ui_text_fit(x + kw, y, w - kw, vst, val ? val : "–");
	return y + cf_ui_line_h(CF_S_BODY) + 2;
}

/* ------------------------------------------------------------------ */
/* Quelle laden                                                       */
/* ------------------------------------------------------------------ */

/* Steht weiter unten bei den GameTDB-Helfern; gebraucht wird es schon
 * nach dem Laden einer Quelle. */
static void enrich_catalog(app *a);

static int add_cb(void *ctx, const cf_entry *e)
{
	return cf_catalog_add((cf_catalog *)ctx, e) ? 0 : -1;
}

static void source_load(app *a, size_t i)
{
	cf_source *s;
	char *txt = NULL;
	size_t len = 0;
	int r;
	size_t n = 0;
	char msg[256];

	if (i >= a->src.n)
		return;
	s = &a->src.v[i];

	splash(cf_t("menu.sources"), s->name, cf_t("src.loading"));

	r = cf_http_get_mem(s->url, META_LIMIT, &txt, &len, NULL);
	if (r != 200 && r != 206) {
		const char *why = cf_http_strerror(r);

		if (why)
			snprintf(msg, sizeof(msg), "%s: %s", s->name, why);
		else
			snprintf(msg, sizeof(msg), "%s: HTTP %d", s->name, r);

		free(txt);
		cf_ui_message(cf_t("src.load_fail"), msg);
		return;
	}

	cf_catalog_drop_source(a->cat, (unsigned)i);
	a->base[i][0] = '\0';

	switch (s->type) {
	case CF_SRC_MANIFEST:
		n = cf_parse_manifest(txt, len, (unsigned)i,
		                      a->base[i], sizeof(a->base[i]),
		                      add_cb, a->cat);
		if (!a->base[i][0])
			sstr(a->base[i], sizeof(a->base[i]), s->url);
		break;
	case CF_SRC_AUTOIDX:
	default:
		n = cf_parse_autoidx(txt, len, (unsigned)i, add_cb, a->cat);
		sstr(a->base[i], sizeof(a->base[i]), s->url);
		break;
	}

	free(txt);

	s->loaded = 1;
	s->entries = (int)n;
	cf_catalog_refresh(a->cat);
	enrich_catalog(a);

	if (n == 0) {
		snprintf(msg, sizeof(msg), "%s: %s", s->name, cf_t("cat.empty"));
		cf_ui_message(cf_t("src.load_fail"), msg);
	}
}

/* Baut die Download-Adresse eines Eintrags. */
static int entry_url(const app *a, const cf_entry *e, char *dst, size_t dstsz)
{
	const char *base;

	if (e->src >= CF_MAX_SOURCES)
		return 0;

	base = a->base[e->src];
	if (!base[0])
		return 0;

	return cf_join_url(dst, dstsz, base, e->path) < dstsz;
}

/* ------------------------------------------------------------------ */
/* Disc-ID aus dem Header lesen (docs/METADATA.md)                    */
/* ------------------------------------------------------------------ */

typedef struct {
	unsigned char buf[64];
	size_t        have;
} hdrbuf;

static int hdr_sink(void *ctx, const void *data, size_t len)
{
	hdrbuf *h = ctx;
	size_t room = sizeof(h->buf) - h->have;

	if (len > room)
		len = room;
	memcpy(h->buf + h->have, data, len);
	h->have += len;
	return 0;
}

/* 1 wenn ein Header gelesen wurde; is_gc sagt, ob es ein GC-Image ist. */
static int probe_disc_id(const char *url, char id[8], int *is_gc)
{
	hdrbuf h;
	int r;

	memset(&h, 0, sizeof(h));
	id[0] = '\0';
	*is_gc = 0;

	r = cf_http_get_slice(url, 0, 63, hdr_sink, &h, NULL);
	if ((r != 200 && r != 206) || h.have < 32)
		return 0;

	memcpy(id, h.buf, 6);
	id[6] = '\0';

	/* Magic 0xC2339F3D bei 0x1C kennzeichnet ein GC-Image. */
	if (h.buf[0x1C] == 0xC2 && h.buf[0x1D] == 0x33 &&
	    h.buf[0x1E] == 0x9F && h.buf[0x1F] == 0x3D)
		*is_gc = 1;

	return 1;
}

/* Sprachkuerzel fuer GameTDB: Titel und Packungen in der Sprache der
 * Oberflaeche. */
static const char *meta_lang(const app *a)
{
	switch (a->lang) {
	case CF_LANG_DE: return "DE";
	case CF_LANG_FR: return "FR";
	case CF_LANG_ES: return "ES";
	case CF_LANG_IT: return "IT";
	default:         return "EN";   /* RU und JA hat GameTDB kaum */
	}
}

/* Spielinformationen zu einem Eintrag: exakt ueber die Disc-ID, sonst
 * ueber den Namen. Die Namenssuche laeuft ueber alle Titel der Datenbank
 * und wird darum je Eintrag nur einmal gemacht. */
static const cf_gtdb_game *entry_game(const cf_entry *e, int *guessed,
                                      double *score)
{
	const cf_gtdb_game *g;

	*guessed = 0;
	*score = 0.0;

	if (e->id[0]) {
		g = cf_meta_lookup(e->id);
		if (g)
			return g;
	}

	/* Die Zuordnung steht seit dem Nachtragen im Eintrag. Vorher lief hier
	 * bei jedem Schritt durch die Liste eine Suche ueber alle 1.708
	 * Datenbanktitel - einer der beiden Gruende fuer das Ruckeln. Jetzt
	 * ist es eine Binaersuche. */
	if (!e->meta_id[0])
		return NULL;

	g = cf_meta_lookup(e->meta_id);
	if (g && e->meta_guess) {
		*guessed = 1;
		*score = e->meta_score / 100.0;
	}
	return g;
}

/* Jahr und Genre aus GameTDB in jeden Katalogeintrag schreiben, damit der
 * Katalog danach sortieren und filtern kann. Laeuft einmal nach dem Laden
 * einer Quelle und nach dem Laden der Datenbank, nicht bei jedem Bild. */
static void enrich_one(void *ctx, cf_entry *e)
{
	const cf_gtdb_game *g = NULL;
	double score = 0.0;

	(void)ctx;
	e->year = 0;
	e->genre[0] = '\0';
	e->meta_guess = 0;
	e->meta_score = 0;
	e->meta_id[0] = '\0';

	if (e->id[0])
		g = cf_meta_lookup(e->id);
	if (!g && e->is_image) {
		g = cf_meta_guess(e->title, &score);
		if (g) {
			e->meta_guess = 1;
			e->meta_score = (uint8_t)(score * 100.0 + 0.5);
		}
	}
	if (!g)
		return;

	snprintf(e->meta_id, sizeof(e->meta_id), "%.6s", g->id);
	e->year = g->year;
	snprintf(e->genre, sizeof(e->genre), "%s", g->genre);
}

static void enrich_catalog(app *a)
{
	if (!cf_meta_count() || !cf_catalog_total(a->cat))
		return;
	splash(cf_t("menu.catalog"), cf_t("meta.matching"), NULL);
	cf_catalog_each(a->cat, enrich_one, NULL);
	cf_catalog_refresh(a->cat);
}

static const char *region_name(char r)
{
	switch (r) {
	case 'E': return "USA";
	case 'P': return "Europa";
	case 'D': return "Deutschland";
	case 'F': return "Frankreich";
	case 'S': return "Spanien";
	case 'I': return "Italien";
	case 'J': return "Japan";
	case 'U': return "Australien";
	default:  return "?";
	}
}

/* ------------------------------------------------------------------ */
/* Download mit Fortschrittsanzeige                                   */
/* ------------------------------------------------------------------ */

typedef struct {
	app         *a;
	char         cur_url[CF_URL_MAX];  /* laeuft gerade, nicht in "Als Naechstes" */
	const char  *title;
	size_t       idx;
	size_t       total;
	volatile int cancel;

	cf_dl_progress last;     /* zuletzt gemeldeter Stand, zum Neuzeichnen */
	int          have_last;
	size_t       qsel;       /* Auswahl in "Als Naechstes" */
	u64          t_poll;     /* letzte Controllerabfrage */
} dlui;

static dlui g_dlui;

/* Die wartenden Eintraege ausser dem laufenden, in Ladereihenfolge.
 * Rueckgabe: Anzahl; out bekommt die Plaetze in a->q. */
static size_t upcoming(size_t *out, size_t max)
{
	const cf_queue *q = &g_dlui.a->q;
	size_t i, n = 0;

	for (i = 0; i < q->n && n < max; i++) {
		if (q->v[i].state != CF_Q_WAIT)
			continue;
		if (!strcmp(q->v[i].url, g_dlui.cur_url))
			continue;
		out[n++] = i;
	}
	return n;
}

static void draw_upcoming(int y)
{
	const cf_queue *q = &g_dlui.a->q;
	size_t idx[CF_QUEUE_MAX];
	size_t n = upcoming(idx, CF_QUEUE_MAX), i, first = 0;
	int rows = (CF_UI_FOOT_Y - 12 - (y + 28)) / CF_UI_ROW_H;
	char line[96];

	if (rows < 1)
		return;

	cf_ui_hline(CF_UI_X0, y, CF_UI_W);
	y += 8;
	snprintf(line, sizeof(line), "%s (%u)", cf_t("dl.upnext"), (unsigned)n);
	cf_ui_text(CF_UI_X0, y, CF_S_SMALLDIM, line);
	y += 22;

	if (n == 0) {
		cf_ui_text(CF_UI_X0 + 8, y + 4, CF_S_DIM, cf_t("dl.upnext_none"));
		return;
	}

	if (g_dlui.qsel >= n)
		g_dlui.qsel = n - 1;
	if (g_dlui.qsel >= (size_t)rows)
		first = g_dlui.qsel - (size_t)rows + 1;

	for (i = first; i < n && i < first + (size_t)rows; i++) {
		const cf_qitem *it = &q->v[idx[i]];
		int ry = y + (int)(i - first) * CF_UI_ROW_H;
		int ty = ry + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;
		char size[24];

		if (i == g_dlui.qsel)
			cf_gfx_rect(CF_UI_X0, ry, CF_UI_W, CF_UI_ROW_H, CF_COL_ROWSEL);

		snprintf(line, sizeof(line), "%u.", (unsigned)(i + 1));
		cf_ui_text(CF_UI_X0 + 8, ty, CF_S_DIM, line);
		cf_fmt_size(size, sizeof(size), it->size);
		cf_ui_text_right(CF_UI_X1 - 8, ty + 2, CF_S_SMALLDIM, size);
		cf_ui_text_fit(CF_UI_X0 + 44, ty, CF_UI_W - 140,
		               i == g_dlui.qsel ? CF_S_BODY : CF_S_DIM, it->title);
	}
}

static void draw_progress(const cf_dl_progress *p)
{
	char a[24], b[24], rate[24], eta[24], line[160];
	int y = CF_UI_BODY_Y;
	int lh = cf_ui_line_h(CF_S_BODY);

	snprintf(line, sizeof(line), "%u / %u",
	         (unsigned)(g_dlui.idx + 1), (unsigned)g_dlui.total);
	cf_ui_begin(cf_t("q.running"), line);

	/* Titel, darunter Balken und Zahlen - kompakt, damit unten Platz fuer
	 * die Warteschlange bleibt. */
	cf_ui_panel(CF_UI_X0, y, CF_UI_W, 44);
	cf_ui_text_fit(CF_UI_X0 + 14, y + 10, CF_UI_W - 28, CF_S_BOLD,
	               g_dlui.title);
	y += 56;

	cf_fmt_size(b, sizeof(b), p->total);

	if (p->phase != CF_DL_P_TRANSFER) {
		/* Vor dem ersten Byte gibt es keinen Balken, der etwas sagen
		 * koennte. Dann steht da, woran gerade gearbeitet wird - sonst
		 * sieht ein Wartezustand wie ein Absturz aus. */
		if (p->phase == CF_DL_P_PREPARE) {
			cf_ui_text(CF_UI_X0, y, CF_S_BODY, p->resumed
			           ? cf_t("dl.opening") : cf_t("dl.creating"));
		} else {
			if (p->conns > 1)
				snprintf(line, sizeof(line), cf_t("dl.connecting"),
				         p->conns_active, p->conns);
			else
				snprintf(line, sizeof(line), "%s", cf_t("dl.connecting1"));
			cf_ui_text(CF_UI_X0, y, CF_S_BODY, line);
		}
		y += lh + 6;
		if (p->total) {
			snprintf(line, sizeof(line), "%s: %s", cf_t("info.size"), b);
			cf_ui_text(CF_UI_X0, y, CF_S_DIM, line);
		}
		y += lh + 18;
	} else {
		cf_fmt_size(a, sizeof(a), p->done);
		cf_fmt_rate(rate, sizeof(rate), p->rate);
		cf_fmt_duration(eta, sizeof(eta), p->eta);

		cf_ui_bar(CF_UI_X0, y, CF_UI_W, 20,
		          p->total ? (double)p->done / (double)p->total : 0.0);
		y += 28;

		snprintf(line, sizeof(line), "%s / %s", a, b);
		cf_ui_text(CF_UI_X0, y, CF_S_BODY, line);
		snprintf(line, sizeof(line), "%s/s", rate);
		cf_ui_text_right(CF_UI_X1, y, CF_S_ACCENT, line);
		y += lh + 2;

		if (p->eta) {
			snprintf(line, sizeof(line), cf_t("q.eta"), eta);
			cf_ui_text(CF_UI_X0, y, CF_S_SMALLDIM, line);
		}
		if (p->total) {
			snprintf(line, sizeof(line), "%u %%",
			         (unsigned)((p->done * 100ull) / p->total));
			cf_ui_text_right(CF_UI_X1, y, CF_S_SMALLDIM, line);
		}
		if (p->conns > 1) {
			snprintf(line, sizeof(line), cf_t("q.conns"),
			         p->conns_active, p->conns);
			cf_ui_text_center(CF_GFX_W / 2, y, CF_S_SMALLDIM, line);
		}
		y += cf_ui_line_h(CF_S_SMALL) + 4;

		if (!p->crc_usable) {
			cf_ui_text(CF_UI_X0, y, CF_S_WARN, cf_t("q.crc_unusable"));
			y += cf_ui_line_h(CF_S_SMALL) + 2;
		}
		y += 8;
	}

	draw_upcoming(y);
	cf_ui_end(cf_t("hint.progress2"));
}

/* Controller abfragen. Wird aus der Ladeschleife gerufen, mehrmals je
 * Sekunde - und genau das war der Fehler beim Abbrechen: vorher wurde nur
 * alle 256 KB abgefragt, und ein kurzer Druck auf B dazwischen war fuer
 * PAD_ButtonsDown nie passiert. Gezeichnet wird hier nur, wenn sich durch
 * eine Eingabe etwas aendert. */
static void dl_input(void)
{
	u32 d, held;
	size_t idx[CF_QUEUE_MAX];
	size_t n;
	int redraw = 0;

	d = cf_ui_poll();
	held = PAD_ButtonsHeld(0);

	/* Gehalten zaehlt auch: wer B gedrueckt haelt, will abbrechen. */
	if ((d | held) & PAD_BUTTON_B) {
		g_dlui.cancel = 1;
		return;
	}

	n = upcoming(idx, CF_QUEUE_MAX);

	if ((d & PAD_BUTTON_UP) && n) {
		g_dlui.qsel = g_dlui.qsel ? g_dlui.qsel - 1 : n - 1;
		redraw = 1;
	}
	if ((d & PAD_BUTTON_DOWN) && n) {
		g_dlui.qsel = (g_dlui.qsel + 1 < n) ? g_dlui.qsel + 1 : 0;
		redraw = 1;
	}

	/* A: der markierte Eintrag wird als naechster geladen - er rueckt in
	 * der Schlange direkt hinter den laufenden. */
	if ((d & PAD_BUTTON_A) && n && g_dlui.qsel < n) {
		cf_queue *q = &g_dlui.a->q;
		size_t cur = cf_queue_find(q, g_dlui.cur_url);
		size_t from = idx[g_dlui.qsel];
		size_t to = (cur < q->n) ? cur + 1 : 0;

		if (from > to)
			cf_queue_move(q, from, to);
		else if (from < to && to > 0)
			cf_queue_move(q, from, to - 1);
		queue_save(g_dlui.a);
		g_dlui.qsel = 0;
		redraw = 1;
	}

	if (redraw && g_dlui.have_last)
		draw_progress(&g_dlui.last);
}

static void dl_tick(void *ctx)
{
	u64 now = gettime();

	(void)ctx;

	/* Hoechstens alle 30 ms: die Ladeschleife kann sehr viel oefter
	 * durchlaufen, und PAD_ScanPads kostet jedes Mal einen SI-Zugriff. */
	if (ticks_to_millisecs(diff_ticks(g_dlui.t_poll, now)) < 30)
		return;
	g_dlui.t_poll = now;
	dl_input();
}

static void dl_cb(void *ctx, const cf_dl_progress *p)
{
	(void)ctx;

	g_dlui.last = *p;
	g_dlui.have_last = 1;
	draw_progress(p);
	dl_input();
}

static void run_queue(app *a)
{
	cf_dl_opts o;
	size_t done_count = 0;
	size_t total;

	total = cf_queue_count_waiting(&a->q);
	if (total == 0) {
		cf_ui_message(cf_t("menu.queue"), cf_t("q.empty"));
		return;
	}

	if (!a->dir[0]) {
		cf_ui_message(cf_t("menu.queue"), cf_t("store.none"));
		return;
	}

	if (!cf_mkdir_p(a->dir)) {
		cf_ui_message(cf_t("menu.queue"), cf_t("q.no_dir"));
		return;
	}

	memset(&o, 0, sizeof(o));
	snprintf(o.dir, sizeof(o.dir), "%s", a->dir);
	o.cb = dl_cb;
	o.tick = dl_tick;
	o.ctx = NULL;
	o.cancel = &g_dlui.cancel;
	o.conns = a->conns;
	o.verify = a->verify;

	memset(&g_dlui, 0, sizeof(g_dlui));
	g_dlui.a = a;

	/* Sofort umschalten, damit der Start sichtbar ankommt. */
	splash(cf_t("q.running"), cf_t("q.starting"), NULL);

	for (;;) {
		size_t i = cf_queue_next_waiting(&a->q);
		cf_qitem cur;
		cf_qitem *it;
		int r;

		if (i >= a->q.n)
			break;
		if (g_dlui.cancel)
			break;

		/* Eine Kopie laden, nicht den Platz in der Schlange: waehrend
		 * des Downloads darf die Reihenfolge umgestellt werden, und
		 * dabei wandern die Eintraege im Feld. */
		cur = a->q.v[i];
		snprintf(g_dlui.cur_url, sizeof(g_dlui.cur_url), "%s", cur.url);

		g_dlui.title = cur.title;
		g_dlui.idx = done_count;
		g_dlui.total = total;
		g_dlui.have_last = 0;

		r = cf_download_run(&cur, &o);

		/* Danach ueber die Adresse wiederfinden. */
		i = cf_queue_find(&a->q, cur.url);
		it = (i < a->q.n) ? &a->q.v[i] : NULL;

		if (r == 0) {
			if (it) {
				it->state = CF_Q_DONE;
				it->err = 0;
			}
		} else if (r == CF_HTTP_ECANCEL) {
			/* Stand ist gesichert, Eintrag bleibt wartend. */
			queue_save(a);
			cf_ui_message(cf_t("menu.queue"), cf_t("q.cancelled"));
			return;
		} else {
			char msg[256];
			const char *why = cf_download_strerror(r);

			if (it) {
				it->state = CF_Q_FAIL;
				it->err = r;
			}

			if (why)
				snprintf(msg, sizeof(msg), "%s: %s", cur.title, why);
			else
				snprintf(msg, sizeof(msg), "%s: HTTP %d", cur.title, r);

			queue_save(a);
			cf_ui_message(cf_t("q.failed"), msg);
		}

		done_count++;
		queue_save(a);
	}

	queue_save(a);
	cf_ui_message(cf_t("menu.queue"), cf_t("q.done"));
}

/* ------------------------------------------------------------------ */
/* Listenbewegung                                                     */
/* ------------------------------------------------------------------ */

static void list_move(u32 d, size_t count, size_t *sel, int rows)
{
	if (count == 0) {
		*sel = 0;
		return;
	}
	if (*sel >= count)
		*sel = count - 1;

	if (d & PAD_BUTTON_UP)
		*sel = (*sel == 0) ? count - 1 : *sel - 1;
	if (d & PAD_BUTTON_DOWN)
		*sel = (*sel + 1 >= count) ? 0 : *sel + 1;
	if (d & PAD_TRIGGER_L)
		*sel = (*sel > (size_t)rows) ? *sel - (size_t)rows : 0;
	if (d & PAD_TRIGGER_R) {
		*sel += (size_t)rows;
		if (*sel >= count)
			*sel = count - 1;
	}

	/* Links und Rechts blaettern seitenweise, auf den Anfang der Seite.
	 * Zusammen mit der Seitenanzeige oben fuehlt sich das wie Umblaettern
	 * an, und eine Seite ist mit einem Druck da statt mit zwoelf. */
	if (d & PAD_BUTTON_LEFT) {
		size_t page = *sel / (size_t)rows;
		*sel = page ? (page - 1) * (size_t)rows : 0;
	}
	if (d & PAD_BUTTON_RIGHT) {
		size_t next = (*sel / (size_t)rows + 1) * (size_t)rows;
		*sel = next < count ? next : count - 1;
	}
}

/* ------------------------------------------------------------------ */
/* Detailfeld eines Eintrags (Katalog rechts, Detailansicht)          */
/* ------------------------------------------------------------------ */

/* Zeichnet Kennung, Region, Groesse und Pruefsumme eines Eintrags in das
 * Rechteck ab (x, y) mit Breite w. Rueckgabe: naechstes y. */
static int entry_info(const cf_entry *e, int x, int y, int w)
{
	char size[24], buf[64];

	cf_fmt_size(size, sizeof(size), e->size);
	y = kv_line(x, y, w, cf_t("info.size"), e->size ? size : "?", CF_S_BODY);

	if (e->id[0]) {
		y = kv_line(x, y, w, cf_t("info.id"), e->id, CF_S_BODY);
		snprintf(buf, sizeof(buf), "%s", region_name(e->id[3]));
		y = kv_line(x, y, w, cf_t("info.region"), buf, CF_S_BODY);
		if (e->id_origin == CF_ID_GUESS) {
			cf_ui_text_fit(x, y, w, CF_S_WARN, cf_t("info.unsure"));
			y += cf_ui_line_h(CF_S_BODY) + 2;
		}
	} else {
		y = kv_line(x, y, w, cf_t("info.id"), "–", CF_S_DIM);
	}

	if (e->has_crc) {
		snprintf(buf, sizeof(buf), "%08x", (unsigned)e->crc32);
		y = kv_line(x, y, w, "CRC32", buf, CF_S_DIM);
	}

	/* Aus GameTDB, wenn die Datenbank geladen ist. */
	{
		int guessed;
		double score;
		const cf_gtdb_game *g = entry_game(e, &guessed, &score);

		if (g) {
			y += 4;
			if (g->year) {
				if (g->month)
					snprintf(buf, sizeof(buf), "%u-%02u", g->year, g->month);
				else
					snprintf(buf, sizeof(buf), "%u", g->year);
				y = kv_line(x, y, w, cf_t("info.year"), buf, CF_S_BODY);
			}
			if (g->publisher[0])
				y = kv_line(x, y, w, cf_t("info.publisher"), g->publisher,
				            CF_S_BODY);
			if (g->developer[0] && strcmp(g->developer, g->publisher))
				y = kv_line(x, y, w, cf_t("info.developer"), g->developer,
				            CF_S_BODY);
			if (g->genre[0])
				y = kv_line(x, y, w, cf_t("info.genre"), g->genre, CF_S_BODY);
			if (g->players) {
				snprintf(buf, sizeof(buf), "%u", g->players);
				y = kv_line(x, y, w, cf_t("info.players"), buf, CF_S_BODY);
			}
			if (guessed) {
				snprintf(buf, sizeof(buf), cf_t("info.guess"),
				         (unsigned)(score * 100.0 + 0.5));
				cf_ui_text_fit(x, y, w, CF_S_WARN, buf);
				y += cf_ui_line_h(CF_S_BODY) + 2;
			}
		}
	}

	return y;
}

/* Cover im Seitenverhaeltnis 160:224. Aus dem Speicher oder von der Karte
 * jederzeit; vom Netz nur, wenn allow_net - also wenn der Nutzer auf dem
 * Eintrag verweilt und nicht beim Durchrollen. Ohne Bild steht die
 * Kennung in einem Rahmen. */
static void cover_box(const cf_entry *e, int x, int y, int w, int h,
                      int level)
{
	const cf_tex *t = NULL;
	const char *id = e->id[0] ? e->id : NULL;
	int guessed;
	double score;

	if (!id) {
		/* Ohne Disc-ID: die der Namenszuordnung, wenn es eine gibt. */
		const cf_gtdb_game *g = entry_game(e, &guessed, &score);
		if (g)
			id = g->id;
	}

	if (id)
		t = cf_meta_cover(id, level);

	if (t) {
		cf_gfx_rect(x + 3, y + 4, w, h, CF_COL_SHADOW);
		cf_ui_image(x, y, w, h, t);
		return;
	}

	cf_ui_panel(x, y, w, h);
	if (id) {
		cf_ui_text_center(x + w / 2, y + h / 2 - cf_ui_line_h(CF_S_BOLD),
		                  CF_S_DIM, id);
		if (cf_meta_cover_missing(id))
			cf_ui_text_center(x + w / 2, y + h / 2 + 4, CF_S_SMALLDIM,
			                  cf_t("info.cover_none"));
	} else {
		cf_ui_text_center(x + w / 2, y + h / 2 - cf_ui_line_h(CF_S_BODY) / 2,
		                  CF_S_DIM, "?");
	}
}

/* ------------------------------------------------------------------ */
/* Detailansicht                                                      */
/* ------------------------------------------------------------------ */

static void screen_detail(app *a, cf_entry *e)
{
	char url[1024];
	int have_url = entry_url(a, e, url, sizeof(url));
	u64 t_enter = gettime();   /* Lauftext beginnt beim Betreten vorn */

	for (;;) {
		u32 d;
		int cw = 180, ch = 252;
		int x = CF_UI_X0, y = CF_UI_BODY_Y;
		int ix = x + cw + 24, iw = CF_UI_X1 - ix;

		cf_ui_begin(cf_t("menu.catalog"), e->id[0] ? e->id : NULL);

		cover_box(e, x, y, cw, ch,
		          a->net.up ? CF_COVER_NET : CF_COVER_CARD);

		cf_ui_text_marquee(ix, y, iw, CF_S_BOLD, e->title,
		                   ticks_to_millisecs(diff_ticks(t_enter, gettime())));
		y += cf_ui_line_h(CF_S_BOLD) + 10;
		y = entry_info(e, ix, y, iw);

		y += 8;
		cf_ui_text_fit(ix, y, iw, CF_S_SMALLDIM,
		               have_url ? url : cf_t("info.url_fail"));

		cf_ui_end(cf_t("hint.detail"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_B)
			return;

		if ((d & PAD_BUTTON_X) && have_url) {
			char id[8];
			int is_gc = 0;

			splash(cf_t("menu.catalog"), e->title, cf_t("info.probing"));

			if (probe_disc_id(url, id, &is_gc)) {
				snprintf(e->id, sizeof(e->id), "%s", id);
				e->id_origin = CF_ID_HEADER;
				if (!is_gc)
					cf_ui_message(cf_t("info.id"), cf_t("info.not_gc"));
			} else {
				cf_ui_message(cf_t("info.id"), cf_t("info.hdr_fail"));
			}
		}

		if ((d & PAD_BUTTON_A) && have_url) {
			cf_qitem it;

			if (cf_queue_contains(&a->q, url)) {
				cf_ui_message(cf_t("menu.queue"), cf_t("q.already"));
				continue;
			}

			cf_qitem_from_entry(&it, e, url);
			if (cf_queue_add(&a->q, &it)) {
				queue_save(a);
				cf_ui_message(cf_t("q.added"), it.title);
			} else {
				cf_ui_message(cf_t("menu.queue"), cf_t("q.full"));
			}
			return;
		}
	}
}

/* ------------------------------------------------------------------ */
/* Katalog                                                            */
/* ------------------------------------------------------------------ */

/* Zeit seit die Auswahl auf dem jetzigen Eintrag steht, fuer den
 * Lauftext. Von den Listenbildschirmen vor dem Zeichnen gesetzt. */
static uint32_t g_sel_ms;

static void catalog_row(void *ctx, size_t idx, int x, int y, int w,
                        int selected)
{
	app *a = ctx;
	cf_entry *e = cf_view_at(a->cat, idx);
	char size[24];
	int ty = y + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;
	int sw;

	(void)selected;
	if (!e)
		return;

	cf_fmt_size(size, sizeof(size), e->size);
	sw = cf_ui_text_width(CF_S_SMALL, size);
	cf_ui_text_right(x + w, ty + 2, CF_S_SMALLDIM, e->size ? size : "?");
	if (e->year) {
		char yr[8];
		snprintf(yr, sizeof(yr), "%u", e->year);
		cf_ui_text_right(x + w - sw - 12, ty + 2, CF_S_SMALLDIM, yr);
		sw += cf_ui_text_width(CF_S_SMALL, "0000") + 12;
	}
	if (selected)
		cf_ui_text_marquee(x, ty, w - sw - 12, CF_S_BODY, e->title,
		                   g_sel_ms);
	else
		cf_ui_text_fit(x, ty, w - sw - 12, CF_S_BODY, e->title);
}

/* Erster Buchstabe eines Titels, gross, fuer den Sprung mit L/R.
 * Ziffern und Zeichen zaehlen als '#'. */
static int title_letter(const cf_entry *e)
{
	unsigned char c = (unsigned char)e->title[0];

	if (c >= 'a' && c <= 'z')
		c = (unsigned char)(c - 32);
	return (c >= 'A' && c <= 'Z') ? c : '#';
}

static size_t letter_jump(app *a, size_t sel, size_t count, int dir)
{
	cf_entry *e = cf_view_at(a->cat, sel);
	int cur = e ? title_letter(e) : 0;
	size_t i = sel;

	if (dir > 0) {
		while (++i < count) {
			e = cf_view_at(a->cat, i);
			if (e && title_letter(e) != cur)
				return i;
		}
		return 0;   /* am Ende wieder vorn */
	}

	/* Rueckwaerts: zum Anfang des vorigen Buchstabens. Steht die Auswahl
	 * mitten im Buchstaben, erst an dessen Anfang. */
	while (i > 0) {
		e = cf_view_at(a->cat, i - 1);
		if (!e || title_letter(e) != cur)
			break;
		i--;
	}
	if (i != sel)
		return i;
	if (i == 0)
		return count - 1;
	e = cf_view_at(a->cat, i - 1);
	cur = e ? title_letter(e) : 0;
	i--;
	while (i > 0) {
		e = cf_view_at(a->cat, i - 1);
		if (!e || title_letter(e) != cur)
			break;
		i--;
	}
	return i;
}

static char g_genres[64][24];

static void genre_row(void *ctx, size_t idx, int x, int y, int w,
                      int selected)
{
	app *a = ctx;
	const char *name = idx ? g_genres[idx - 1] : cf_t("cat.filter.all");
	const char *cur = cf_catalog_genre(a->cat);
	int on = idx ? !strcmp(cur, g_genres[idx - 1]) : !cur[0];
	int ty = y + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;

	(void)selected;
	cf_ui_text_fit(x, ty, w - 30, on ? CF_S_ACCENT : CF_S_BODY, name);
	if (on)
		cf_ui_text_right(x + w, ty, CF_S_ACCENT, "\xE2\x9C\x93");
}

/* 1 wenn ein anderes Genre gewaehlt wurde. */
static int pick_genre(app *a)
{
	size_t ng = cf_catalog_genres(a->cat, g_genres, 64);
	size_t sel = 0, top = 0, k;
	const char *cur = cf_catalog_genre(a->cat);

	for (k = 0; k < ng; k++)
		if (!strcmp(g_genres[k], cur))
			sel = k + 1;

	for (;;) {
		int ly = CF_UI_BODY_Y, lh = CF_UI_FOOT_Y - 10 - ly;
		u32 d;

		cf_ui_begin(cf_t("cat.filter.genre"), NULL);
		cf_ui_list(CF_UI_X0, ly, CF_UI_W, lh, ng + 1, sel, &top,
		           genre_row, a);
		cf_ui_end(cf_t("hint.genre"));

		d = cf_ui_poll();
		if (d & (PAD_BUTTON_B | PAD_TRIGGER_Z))
			return 0;
		list_move(d, ng + 1, &sel, cf_ui_list_rows(lh));
		if (d & PAD_BUTTON_A) {
			cf_catalog_set_genre(a->cat, sel ? g_genres[sel - 1] : "");
			cf_catalog_refresh(a->cat);
			return 1;
		}
		if (d & PAD_BUTTON_X) {   /* Filter loeschen */
			cf_catalog_set_genre(a->cat, "");
			cf_catalog_refresh(a->cat);
			return 1;
		}
	}
}

/* Coverflow: Auswahl gross in der Mitte, je zwei kleiner daneben.
 * Die Nachbarn kommen nur aus dem Speicher oder, wenn die Auswahl
 * steht, von der Karte - so bleibt das Blaettern fluessig. Wer alle
 * Cover vorab auf der Karte haben will, laedt sie in den Einstellungen. */
static void coverflow(app *a, size_t sel, int y, int h)
{
	size_t count = cf_view_count(a->cat);
	int cx = CF_UI_X0 + CF_UI_W / 2;
	int bw = 150, bh = 210;
	int cy = y + 6;
	uint32_t still = g_sel_ms;
	int k;
	cf_entry *e;

	/* Von aussen nach innen, damit die Mitte oben liegt. */
	for (k = 2; k >= 1; k--) {
		int sw = k == 1 ? 104 : 78, sh = sw * 7 / 5;
		int off = k == 1 ? 150 : 250;
		int side;

		for (side = -1; side <= 1; side += 2) {
			long idx = (long)sel + side * k;
			int x, yy;

			if (idx < 0 || idx >= (long)count)
				continue;
			e = cf_view_at(a->cat, (size_t)idx);
			if (!e)
				continue;
			x = cx + side * off - sw / 2;
			yy = cy + (bh - sh) / 2;
			cover_box(e, x, yy, sw, sh,
			          still < 600 ? CF_COVER_MEM : CF_COVER_CARD);
			/* abdunkeln */
			cf_gfx_rect(x, yy, sw, sh, k == 1 ? 0x00000050 : 0x00000090);
		}
	}

	e = cf_view_at(a->cat, sel);
	if (!e)
		return;
	{
		int level = still < 250 ? CF_COVER_MEM
		          : (still < 400 || !a->net.up) ? CF_COVER_CARD
		          : CF_COVER_NET;
		int ty = cy + bh + 12;
		char line[96];

		cover_box(e, cx - bw / 2, cy, bw, bh, level);
		cf_ui_text_marquee(CF_UI_X0 + 40, ty, CF_UI_W - 80, CF_S_BOLD,
		                   e->title, g_sel_ms);
		/* zentriert, wenn der Titel passt */
		ty += cf_ui_line_h(CF_S_BODY) + 2;
		snprintf(line, sizeof(line), "%u / %u", (unsigned)(sel + 1),
		         (unsigned)count);
		cf_ui_text_center(cx, ty, CF_S_SMALLDIM, line);
		(void)h;
	}
}

static void screen_catalog(app *a)
{
	size_t sel = 0, top = 0, last_sel = (size_t)-1;
	u64 sel_since = 0;
	char search[64];

	/* Liste links, Detailfeld rechts - oder die Liste ueber die ganze
	 * Breite, wenn das Detailfeld in den Einstellungen aus ist. */
	const int pw = 206;                             /* Detailfeld */
	int lw;
	const int ly = CF_UI_BODY_Y + 30;
	const int lh = CF_UI_FOOT_Y - 10 - ly;

	snprintf(search, sizeof(search), "%s", cf_catalog_search(a->cat));

	for (;;) {
		size_t count = cf_view_count(a->cat);
		int rows = cf_ui_list_rows(lh);
		u32 d;
		char right[48], line[160];

		lw = a->cat_view == 0 ? CF_UI_W - pw - 12 : CF_UI_W;

		if (count && sel >= count)
			sel = count - 1;

		/* Cover erst holen, wenn die Auswahl eine Weile steht. Sonst
		 * wuerde jeder Schritt durch die Liste einen Abruf ausloesen
		 * und das Rollen stocken lassen. */
		if (sel != last_sel) {
			last_sel = sel;
			sel_since = gettime();
		}
		g_sel_ms = ticks_to_millisecs(diff_ticks(sel_since, gettime()));

		/* Seite x von y statt der reinen Anzahl: mit Links/Rechts wird
		 * seitenweise geblaettert. */
		if (count > (size_t)rows)
			snprintf(right, sizeof(right), "%s %u / %u",
			         cf_t("cat.page"),
			         (unsigned)(sel / (size_t)rows + 1),
			         (unsigned)((count + (size_t)rows - 1) / (size_t)rows));
		else
			snprintf(right, sizeof(right), "%u / %u",
			         (unsigned)count, (unsigned)cf_catalog_total(a->cat));

		cf_ui_begin(cf_t("menu.catalog"), right);

		/* Suche und Sortierung ueber der Liste */
		if (cf_catalog_genre(a->cat)[0])
			snprintf(line, sizeof(line), "%s: %s · %s",
			         cf_t("cat.search"), search[0] ? search : "–",
			         cf_catalog_genre(a->cat));
		else
			snprintf(line, sizeof(line), "%s: %s", cf_t("cat.search"),
			         search[0] ? search : "–");
		cf_ui_text_fit(CF_UI_X0, CF_UI_BODY_Y, lw / 2, CF_S_SMALLDIM, line);
		snprintf(line, sizeof(line), "%s: %s", cf_t("cat.sort"),
		         cf_t(cf_sort_key(cf_catalog_sort(a->cat))));
		cf_ui_text_right(CF_UI_X0 + lw, CF_UI_BODY_Y, CF_S_SMALLDIM, line);

		if (count == 0) {
			cf_ui_text(CF_UI_X0 + 8, ly + 8, CF_S_DIM, cf_t("cat.empty"));
		} else if (a->cat_view == 2) {
			coverflow(a, sel, ly, lh);
		} else {
			cf_ui_list(CF_UI_X0, ly, lw, lh, count, sel, &top,
			           catalog_row, a);
		}

		/* Detailfeld */
		if (a->cat_view == 0) {
			int px = CF_UI_X1 - pw;
			int py = CF_UI_BODY_Y;
			cf_entry *e = count ? cf_view_at(a->cat, sel) : NULL;

			cf_ui_panel(px, py, pw, CF_UI_FOOT_Y - 10 - py);
			if (e) {
				int cw = 120, ch = 168;
				int iy;
				/* Waehrend die Auswahl wandert, nur Cover aus dem
				 * Speicher. Erst wenn sie 250 ms steht, von der Karte,
				 * und mit dem Netz nach 400 ms. Vorher wurde bei jedem
				 * Schritt von der Karte gelesen und dekodiert - 100
				 * bis 150 ms, der zweite Grund fuer das Ruckeln. */
				uint32_t still = ticks_to_millisecs(
					diff_ticks(sel_since, gettime()));
				int level = still < 250 ? CF_COVER_MEM
				          : (still < 400 || !a->net.up) ? CF_COVER_CARD
				          : CF_COVER_NET;

				cover_box(e, px + (pw - cw) / 2, py + 12, cw, ch,
				          level);
				iy = py + 12 + ch + 10;
				cf_gfx_clip(px + 1, py + 1, pw - 2, CF_UI_FOOT_Y - 12 - py);
				cf_ui_text_marquee(px + 10, iy, pw - 20, CF_S_SMALL,
				                   e->title, g_sel_ms);
				iy += cf_ui_line_h(CF_S_SMALL) + 6;
				entry_info(e, px + 10, iy, pw - 20);
				cf_gfx_noclip();
			}
		}

		cf_ui_end(a->cat_view == 2 ? cf_t("hint.flow")
		                           : cf_t("hint.catalog3"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_B)
			return;

		/* START wechselt die Ansicht: Liste mit Detailfeld, nur Liste,
		 * Coverflow. Gemerkt in der Konfiguration. */
		if (d & PAD_BUTTON_START) {
			a->cat_view = (a->cat_view + 1) % 3;
			config_save(a);
			continue;
		}

		/* L und R springen zum vorigen und naechsten Anfangsbuchstaben. */
		if (d & (PAD_TRIGGER_L | PAD_TRIGGER_R)) {
			if (count)
				sel = letter_jump(a, sel, count, (d & PAD_TRIGGER_R) ? 1 : -1);
			d &= ~(u32)(PAD_TRIGGER_L | PAD_TRIGGER_R);
		}

		/* Im Coverflow sind Links und Rechts ein Titel weiter. */
		if (a->cat_view == 2 && count) {
			if (d & PAD_BUTTON_LEFT)
				sel = sel ? sel - 1 : count - 1;
			if (d & PAD_BUTTON_RIGHT)
				sel = (sel + 1 < count) ? sel + 1 : 0;
			d &= ~(u32)(PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
		}

		list_move(d, count, &sel, rows);

		if (d & PAD_BUTTON_X) {
			cf_sort s = cf_catalog_sort(a->cat);
			s = (cf_sort)((s + 1) % CF_SORT_COUNT);
			cf_catalog_set_sort(a->cat, s);
			cf_catalog_refresh(a->cat);
			sel = 0;
			top = 0;
		}

		if (d & PAD_BUTTON_Y) {
			if (cf_ui_keyboard(cf_t("cat.search"), search,
			                   sizeof(search), 0)) {
				cf_catalog_set_search(a->cat, search);
				cf_catalog_refresh(a->cat);
				sel = 0;
				top = 0;
			}
		}

		/* Z oeffnet die Genre-Auswahl; ganz oben "alle Genres" setzt
		 * den Filter zurueck. */
		if ((d & PAD_TRIGGER_Z) && cf_meta_count()) {
			if (pick_genre(a)) {
				sel = 0;
				top = 0;
			}
		}

		if ((d & PAD_BUTTON_A) && count) {
			cf_entry *e = cf_view_at(a->cat, sel);
			if (e)
				screen_detail(a, e);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Warteschlange                                                      */
/* ------------------------------------------------------------------ */

static const char *qstate_text(const cf_qitem *it, cf_style *st)
{
	switch (it->state) {
	case CF_Q_DONE: *st = CF_S_OK;  return cf_t("q.done");
	case CF_Q_FAIL: *st = CF_S_ERR; return cf_t("q.failed");
	default:        *st = CF_S_DIM; return cf_t("q.waiting");
	}
}

static void queue_row(void *ctx, size_t idx, int x, int y, int w,
                      int selected)
{
	app *a = ctx;
	const cf_qitem *it = &a->q.v[idx];
	cf_style st;
	const char *state = qstate_text(it, &st);
	char size[24];
	int ty = y + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;
	int rw;

	(void)selected;

	cf_fmt_size(size, sizeof(size), it->size);
	rw = cf_ui_text_width(CF_S_SMALL, state);
	cf_ui_text_right(x + w, ty + 2, (cf_style)(st == CF_S_DIM ? CF_S_SMALLDIM : st),
	                 state);
	cf_ui_text_right(x + w - rw - 16, ty + 2, CF_S_SMALLDIM, size);
	if (selected)
		cf_ui_text_marquee(x, ty, w - rw - 100, CF_S_BODY, it->title,
		                   g_sel_ms);
	else
		cf_ui_text_fit(x, ty, w - rw - 100, CF_S_BODY, it->title);
}

static void screen_queue(app *a)
{
	size_t sel = 0, top = 0, last_sel = (size_t)-1;
	u64 sel_since = 0;
	const int ly = CF_UI_BODY_Y;
	const int lh = CF_UI_FOOT_Y - 40 - ly;

	for (;;) {
		size_t count = a->q.n;
		int rows = cf_ui_list_rows(lh);
		u32 d;
		char right[64], tot[24], line[300];

		if (sel != last_sel) {
			last_sel = sel;
			sel_since = gettime();
		}
		g_sel_ms = ticks_to_millisecs(diff_ticks(sel_since, gettime()));

		cf_fmt_size(tot, sizeof(tot), cf_queue_bytes_waiting(&a->q));
		snprintf(right, sizeof(right), cf_t("status.waiting"),
		         (unsigned)cf_queue_count_waiting(&a->q), tot);

		cf_ui_begin(cf_t("menu.queue"), right);

		if (count == 0)
			cf_ui_text(CF_UI_X0 + 8, ly + 8, CF_S_DIM, cf_t("q.empty"));
		else
			cf_ui_list(CF_UI_X0, ly, CF_UI_W, lh, count, sel, &top,
			           queue_row, a);

		snprintf(line, sizeof(line), "%s: %s", cf_t("store.target"), a->dir);
		cf_ui_text_fit(CF_UI_X0, CF_UI_FOOT_Y - 32, CF_UI_W, CF_S_SMALLDIM, line);

		cf_ui_end(cf_t("hint.queue"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_B)
			return;

		list_move(d, count, &sel, rows);

		if ((d & PAD_BUTTON_A) && count) {
			cf_queue_remove(&a->q, sel);
			queue_save(a);
		}

		if (d & PAD_BUTTON_X) {
			cf_queue_clear_finished(&a->q);
			queue_save(a);
		}

		if (d & PAD_BUTTON_START) {
			if (!a->net.up) {
				cf_ui_message(cf_t("menu.queue"), cf_t("net.fail"));
				continue;
			}
			run_queue(a);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Quellen                                                            */
/* ------------------------------------------------------------------ */

static void source_row(void *ctx, size_t idx, int x, int y, int w,
                       int selected)
{
	app *a = ctx;
	const cf_source *s = &a->src.v[idx];
	char tag[16], info[32];
	int ty = y + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;
	int tw;

	(void)selected;

	snprintf(tag, sizeof(tag), "%s", cf_src_type_name(s->type));
	tw = cf_ui_text_width(CF_S_SMALL, tag) + 12;
	cf_gfx_rect(x, y + 5, tw, CF_UI_ROW_H - 10, CF_COL_LINE);
	cf_ui_text(x + 6, ty + 2, CF_S_SMALL, tag);

	if (s->loaded)
		snprintf(info, sizeof(info), "%d", s->entries);
	else
		snprintf(info, sizeof(info), "–");
	cf_ui_text_right(x + w, ty + 2, CF_S_SMALLDIM, info);

	cf_ui_text_fit(x + tw + 10, ty, w - tw - 70, CF_S_BODY, s->name);
}

static void screen_sources(app *a)
{
	size_t sel = 0, top = 0;
	const int ly = CF_UI_BODY_Y;
	const int lh = CF_UI_FOOT_Y - 10 - ly;

	for (;;) {
		size_t count = a->src.n;
		int rows = cf_ui_list_rows(lh);
		u32 d;

		cf_ui_begin(cf_t("menu.sources"), NULL);

		if (count == 0) {
			char line[300];

			cf_ui_text(CF_UI_X0 + 8, ly + 8, CF_S_DIM, cf_t("src.none"));
			snprintf(line, sizeof(line), cf_t("src.hint_empty"), a->cfgdir);
			cf_ui_wrap(CF_UI_X0 + 8, ly + 8 + cf_ui_line_h(CF_S_BODY) + 10,
			           CF_UI_W - 16, 3, CF_S_SMALLDIM, line);
		} else {
			cf_ui_list(CF_UI_X0, ly, CF_UI_W, lh, count, sel, &top,
			           source_row, a);
		}

		cf_ui_end(cf_t("hint.sources"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_B)
			return;

		list_move(d, count, &sel, rows);

		if ((d & PAD_BUTTON_A) && count) {
			if (!a->net.up) {
				cf_ui_message(cf_t("menu.sources"), cf_t("net.fail"));
				continue;
			}
			source_load(a, sel);
		}

		if (d & PAD_BUTTON_Y) {
			char url[CF_SRC_URL_MAX];

			url[0] = '\0';
			if (cf_ui_keyboard(cf_t("src.enter_url"), url,
			                   sizeof(url), 1) && url[0]) {
				if (cf_sources_add(&a->src, cf_src_guess(url),
				                   NULL, url)) {
					sources_save(a);
				} else {
					cf_ui_message(cf_t("menu.sources"), cf_t("q.full"));
				}
			}
		}

		if ((d & PAD_BUTTON_X) && count) {
			cf_catalog_drop_source(a->cat, (unsigned)sel);
			cf_catalog_refresh(a->cat);
			cf_sources_remove(&a->src, sel);
			sources_save(a);
		}
	}
}

/* Alle Cover des Katalogs auf die Karte holen, damit Coverflow und
 * Detailfeld danach ohne Netz auskommen. Was schon auf der Karte liegt
 * oder bei GameTDB fehlt, wird uebersprungen. B bricht ab. */
static void precache_covers(app *a)
{
	size_t n = cf_view_count(a->cat), i, got = 0;

	if (!cf_meta_count()) {
		cf_ui_message(cf_t("set.precache"), cf_t("meta.none"));
		return;
	}
	if (!a->net.up) {
		cf_ui_message(cf_t("set.precache"), cf_t("net.fail"));
		return;
	}

	for (i = 0; i < n; i++) {
		cf_entry *e = cf_view_at(a->cat, i);
		const char *id = NULL;
		char l1[64];

		if (!e)
			continue;
		if (e->id[0]) {
			id = e->id;
		} else {
			int guessed;
			double score;
			const cf_gtdb_game *g = entry_game(e, &guessed, &score);
			if (g)
				id = g->id;
		}

		snprintf(l1, sizeof(l1), cf_t("cache.progress"),
		         (unsigned)(i + 1), (unsigned)n);
		splash(cf_t("set.precache"), l1, e->title);

		if (id && cf_meta_cover(id, CF_COVER_NET))
			got++;

		PAD_ScanPads();
		if (PAD_ButtonsHeld(0) & PAD_BUTTON_B)
			break;
	}

	{
		char msg[96];
		snprintf(msg, sizeof(msg), cf_t("cache.done"), (unsigned)got,
		         (unsigned)n);
		cf_ui_message(cf_t("set.precache"), msg);
	}
}

/* ------------------------------------------------------------------ */
/* Einstellungen                                                      */
/* ------------------------------------------------------------------ */

#define N_SETTINGS 9

typedef struct {
	const char *label[N_SETTINGS];
	char        value[N_SETTINGS][288];
} settings_view;

/* --- GameTDB-Datenbank laden, mit Fortschritt und Abbruch ----------- */

typedef struct {
	volatile int cancel;
	uint64_t     done, total;
} metaui;

static void meta_cb(void *ctx, const char *stage, uint64_t done,
                    uint64_t total)
{
	metaui *m = ctx;
	char a[24], b[24], line[96];
	int y = CF_UI_BODY_Y + 40;
	u32 d;

	(void)stage;
	m->done = done;
	m->total = total;

	cf_ui_begin(cf_t("set.gametdb"), NULL);
	cf_ui_text(CF_UI_X0, y, CF_S_BODY, cf_t("meta.updating"));
	y += cf_ui_line_h(CF_S_BODY) + 6;
	cf_ui_wrap(CF_UI_X0, y, CF_UI_W, 2, CF_S_DIM, cf_t("meta.updating2"));
	y += 2 * cf_ui_line_h(CF_S_BODY) + 16;

	cf_ui_bar(CF_UI_X0, y, CF_UI_W, 22,
	          total ? (double)done / (double)total : 0.0);
	y += 32;
	cf_fmt_size(a, sizeof(a), done);
	cf_fmt_size(b, sizeof(b), total);
	snprintf(line, sizeof(line), total ? "%s / %s" : "%s", a, b);
	cf_ui_text(CF_UI_X0, y, CF_S_DIM, line);

	cf_ui_end(cf_t("hint.progress"));

	d = cf_ui_poll();
	if (d & PAD_BUTTON_B)
		m->cancel = 1;
}

static void meta_update(app *a)
{
	metaui m;
	int r;

	if (!a->net.up) {
		cf_ui_message(cf_t("set.gametdb"), cf_t("net.fail"));
		return;
	}

	memset(&m, 0, sizeof(m));
	meta_cb(&m, "db", 0, 0);

	r = cf_meta_update(meta_cb, &m, &m.cancel);

	if (r == 0) {
		char msg[96];
		enrich_catalog(a);
		snprintf(msg, sizeof(msg), cf_t("meta.done"),
		         (unsigned)cf_meta_count());
		cf_ui_message(cf_t("set.gametdb"), msg);
	} else if (!m.cancel) {
		char msg[160];
		const char *why;

		switch (r) {
		case CF_META_EZIP:  why = "Archiv nicht wie erwartet"; break;
		case CF_META_EEMPTY: why = "keine GameCube-Spiele im Archiv"; break;
		case CF_META_EFILE: why = "Karte: Datei nicht schreibbar"; break;
		default:            why = cf_http_strerror(r); break;
		}
		snprintf(msg, sizeof(msg), "%s: %s", cf_t("meta.fail"), why);
		cf_ui_message(cf_t("set.gametdb"), msg);
	}
}

static void settings_fill(const app *a, settings_view *v)
{
	const cf_device *dev = cf_storage_at(a->dev);

	v->label[0] = cf_t("set.language");
	snprintf(v->value[0], sizeof(v->value[0]), "%s",
	         cf_lang_name(a->lang));

	v->label[1] = cf_t("store.target");
	snprintf(v->value[1], sizeof(v->value[1]), "%s", a->dir);

	v->label[2] = cf_t("set.device");
	snprintf(v->value[2], sizeof(v->value[2]), "%s",
	         dev ? dev->label : cf_t("store.none"));

	v->label[3] = cf_t("set.verify");
	snprintf(v->value[3], sizeof(v->value[3]), "%s",
	         a->verify ? cf_t("set.yes") : cf_t("set.no"));

	v->label[4] = cf_t("set.conns");
	snprintf(v->value[4], sizeof(v->value[4]), "%d", a->conns);

	v->label[8] = cf_t("set.precache");
	snprintf(v->value[8], sizeof(v->value[8]), "%s", cf_t("set.precache_go"));

	v->label[7] = cf_t("set.theme");
	snprintf(v->value[7], sizeof(v->value[7]), "%s",
	         cf_t(cf_ui_theme_key(cf_ui_theme())));

	v->label[6] = cf_t("set.panel");
	snprintf(v->value[6], sizeof(v->value[6]), "%s",
	         cf_t(a->cat_view == 2 ? "view.flow" :
	              a->cat_view == 1 ? "view.list" : "view.panel"));

	v->label[5] = cf_t("set.gametdb");
	if (cf_meta_count())
		snprintf(v->value[5], sizeof(v->value[5]), cf_t("meta.count"),
		         (unsigned)cf_meta_count());
	else
		snprintf(v->value[5], sizeof(v->value[5]), "%s", cf_t("meta.none"));
}

static void settings_row(void *ctx, size_t idx, int x, int y, int w,
                         int selected)
{
	settings_view *v = ctx;
	int ty = y + (CF_UI_ROW_H - cf_ui_line_h(CF_S_BODY)) / 2;
	int lw = 230;

	cf_ui_text_fit(x, ty, lw - 10, selected ? CF_S_BODY : CF_S_DIM,
	               v->label[idx]);
	cf_ui_text_fit(x + lw, ty, w - lw, CF_S_BODY, v->value[idx]);
}

static void screen_settings(app *a)
{
	size_t sel = 0, top = 0;
	const int ly = CF_UI_BODY_Y;
	const int lh = N_SETTINGS * CF_UI_ROW_H;

	for (;;) {
		settings_view v;
		u32 d;

		settings_fill(a, &v);

		cf_ui_begin(cf_t("menu.settings"), NULL);
		cf_ui_list(CF_UI_X0, ly, CF_UI_W, lh, N_SETTINGS, sel, &top,
		           settings_row, &v);
		cf_ui_end(cf_t("hint.settings_db"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_B) {
			config_save(a);
			return;
		}

		if (d & PAD_BUTTON_UP)
			sel = (sel == 0) ? N_SETTINGS - 1 : sel - 1;
		if (d & PAD_BUTTON_DOWN)
			sel = (sel + 1) % N_SETTINGS;

		if (d & PAD_BUTTON_A) {
			switch (sel) {
			case 0:
				a->lang = (cf_lang)((a->lang + 1) % CF_LANG_COUNT);
				lang_load(a);
				/* Titel und Packungen in der neuen Sprache. */
				cf_meta_init(a->cfgdir, meta_lang(a));
				break;
			case 1:
				cf_ui_keyboard(cf_t("store.pick_target"),
				               a->dir, sizeof(a->dir), 1);
				break;
			case 2: {
				size_t n = cf_storage_count();
				if (n > 1) {
					paths_for_device(a, (a->dev + 1) % n);
					lang_load(a);
					sources_load(a);
					cf_meta_init(a->cfgdir, meta_lang(a));
				}
				break;
			}
			case 3:
				a->verify = !a->verify;
				break;
			case 4:
				/* Durchreichen: 1, 2, 4, 8, 12 und zurueck. Mehr als
				 * CF_DL_MAX_CONN hat die Socket-Schicht von libogc2
				 * nicht. */
				if (a->conns >= CF_DL_MAX_CONN)
					a->conns = 1;
				else if (a->conns * 2 > CF_DL_MAX_CONN)
					a->conns = CF_DL_MAX_CONN;
				else
					a->conns *= 2;
				break;
			case 5:
				meta_update(a);
				break;
			case 6:
				a->cat_view = (a->cat_view + 1) % 3;
				break;
			case 7:
				cf_ui_set_theme((cf_ui_theme() + 1) % CF_THEME_COUNT);
				break;
			case 8:
				precache_covers(a);
				break;
			default:
				break;
			}
			config_save(a);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Netztest                                                           */
/* ------------------------------------------------------------------ */

/* Laedt ohne auf die Karte zu schreiben, mit verschiedenen
 * Verbindungszahlen, und zeigt die Rate. Damit laesst sich trennen, was
 * sonst nur zu raten ist:
 *
 *  - Steigt die Rate mit der Zahl der Verbindungen, liegt die Grenze bei
 *    jeder einzelnen (Fenster, Umlaufzeit).
 *  - Bleibt sie gleich, ist etwas Gemeinsames die Grenze - Adapter, EXI
 *    oder Treiber.
 *  - Ist sie deutlich hoeher als beim echten Download, kostet das
 *    Schreiben auf die Karte den Unterschied. */

/* Zeitmessung im ENC28J60-Treiber. Die gibt es nur in der selbst
 * gebauten libogc2 (tools/build-libogc2.sh); gegen das fertige Paket
 * wird dieser Teil nicht mitgebaut. Die Kopfdatei des Treibers liegt
 * nicht im installierten Satz, darum hier von Hand angemeldet - mit
 * unsigned int, weil die Bibliothek u32 benutzt. */
#if CF_BIG_WINDOW
void enc28j60if_stats_reset(void);
void enc28j60if_stats(unsigned int *out, unsigned int n);
#endif

static const int   nt_plan[] = { 1, 2, 4, 6, 8 };
#define NT_N ((int)(sizeof(nt_plan) / sizeof(nt_plan[0])))

typedef struct {
	const char *url;
	int         step;        /* laufender Schritt */
	uint32_t    secs;
	uint32_t    rate[NT_N];  /* Ergebnisse */
	uint64_t    done;        /* laufend */
	uint32_t    cur_rate;    /* laufend, geglaettet */
} ntstate;

static ntstate g_nt;

static void nettest_draw(int finished)
{
	char line[200];
	int y = CF_UI_BODY_Y;
	int lh = cf_ui_line_h(CF_S_BODY);
	int i;

	cf_ui_begin(cf_t("net.test"), finished ? NULL : "…");

	if (!finished) {
		snprintf(line, sizeof(line), "%d / %d  –  %d %s, %u s",
		         g_nt.step + 1, NT_N, nt_plan[g_nt.step],
		         cf_t("set.conns"), (unsigned)g_nt.secs);
		cf_ui_text(CF_UI_X0, y, CF_S_BODY, line);
		y += lh + 4;
		cf_ui_text_fit(CF_UI_X0, y, CF_UI_W, CF_S_SMALLDIM, g_nt.url);
		y += lh + 14;

		snprintf(line, sizeof(line), "%llu KB",
		         (unsigned long long)(g_nt.done / 1024));
		cf_ui_text(CF_UI_X0, y, CF_S_DIM, line);
		if (g_nt.cur_rate) {
			char r[24];
			cf_fmt_rate(r, sizeof(r), g_nt.cur_rate);
			snprintf(line, sizeof(line), "%s/s", r);
			cf_ui_text_right(CF_UI_X1, y, CF_S_ACCENT, line);
		}
		y += lh + 14;
	} else {
		cf_ui_text(CF_UI_X0, y, CF_S_BODY, cf_t("net.test_done"));
		y += lh + 14;
	}

	/* Tabelle der bisherigen Ergebnisse */
	for (i = 0; i < NT_N; i++) {
		char r[24];

		if (!g_nt.rate[i])
			continue;
		cf_fmt_rate(r, sizeof(r), g_nt.rate[i]);
		snprintf(line, sizeof(line), "%2d  %s", nt_plan[i], cf_t("set.conns"));
		cf_ui_text(CF_UI_X0 + 8, y, CF_S_BODY, line);
		snprintf(line, sizeof(line), "%s/s", r);
		cf_ui_text_right(CF_UI_X0 + 300, y, CF_S_ACCENT, line);
		/* Balken relativ zum groessten Wert */
		{
			uint32_t mx = 0;
			int j;
			for (j = 0; j < NT_N; j++)
				if (g_nt.rate[j] > mx)
					mx = g_nt.rate[j];
			if (mx)
				cf_ui_bar(CF_UI_X0 + 320, y + 3, CF_UI_W - 320, lh - 6,
				          (double)g_nt.rate[i] / mx);
		}
		y += lh + 6;
	}

#if CF_BIG_WINDOW
	if (finished) {
		/* Wo die Zeit je Rahmen bleibt. Gemessen wurde: 2,33 ms je
		 * Rahmen, davon 0,73 ms Nutzlast - die Luecke soll diese Anzeige
		 * fuellen. */
		unsigned int s[7];

		enc28j60if_stats(s, 7);
		y += 10;
		if (s[0]) {
			snprintf(line, sizeof(line),
			         "Treiber: %u Rahmen in %u Schüben, größter %u, "
			         "%u verworfen", s[0], s[1], s[3], s[2]);
			cf_ui_text_fit(CF_UI_X0, y, CF_UI_W, CF_S_SMALLDIM, line);
			y += cf_ui_line_h(CF_S_SMALL) + 2;
			snprintf(line, sizeof(line),
			         "je Rahmen %u µs, davon Nutzlast %u µs, "
			         "pbuf+input %u µs",
			         s[4] / s[0], s[5] / s[0], s[6] / s[0]);
			cf_ui_text_fit(CF_UI_X0, y, CF_UI_W, CF_S_SMALLDIM, line);
			y += cf_ui_line_h(CF_S_SMALL) + 2;
			if (s[1]) {
				snprintf(line, sizeof(line),
				         "je Schub %u µs für %u Rahmen",
				         s[4] / s[1], s[0] / s[1]);
				cf_ui_text_fit(CF_UI_X0, y, CF_UI_W, CF_S_SMALLDIM, line);
			}
		}
	}
#endif

	cf_ui_end(finished ? cf_t("ui.ok_hint") : cf_t("hint.progress"));
}

static void nettest_cb(void *ctx, const cf_dl_progress *p)
{
	u32 d;

	(void)ctx;
	g_nt.done = p->done;

	/* Die geglaettete Rate der Engine, nicht Bytes durch Gesamtzeit:
	 * acht Verbindungen aufzubauen kostet ein paar Sekunden, in denen
	 * nichts fliesst, und das wuerde gerade die hohen Zahlen
	 * kleinrechnen. */
	if (p->phase == CF_DL_P_TRANSFER && p->rate)
		g_nt.cur_rate = p->rate;

	nettest_draw(0);

	d = cf_ui_poll();
	if (d & PAD_BUTTON_B)
		g_dlui.cancel = 1;
}

static void screen_nettest(app *a)
{
	char url[CF_URL_MAX];
	int i;

	url[0] = '\0';

	/* Eine echte Datei als Gegenstelle. Die Warteschlange zuerst, weil
	 * dort schon eine Adresse steht, die nachweislich funktioniert. */
	if (a->q.n > 0) {
		sstr(url, sizeof(url), a->q.v[0].url);
	} else if (cf_view_count(a->cat) > 0) {
		const cf_entry *e = cf_view_at(a->cat, 0);
		if (e)
			entry_url(a, e, url, sizeof(url));
	}

	if (!url[0]) {
		cf_ui_message(cf_t("net.test"), cf_t("net.test_need"));
		return;
	}

	if (!a->net.up) {
		cf_ui_message(cf_t("net.test"), cf_t("net.fail"));
		return;
	}

	memset(&g_nt, 0, sizeof(g_nt));
	g_nt.url = url;
	g_nt.secs = 12;

#if CF_BIG_WINDOW
	enc28j60if_stats_reset();
#endif

	for (i = 0; i < NT_N; i++) {
		cf_qitem probe;
		cf_dl_opts o;
		u64 t0;
		uint32_t ms;
		int r;

		if (nt_plan[i] > CF_DL_MAX_CONN)
			continue;

		g_nt.step = i;
		g_nt.done = 0;
		g_nt.cur_rate = 0;
		nettest_draw(0);

		memset(&probe, 0, sizeof(probe));
		sstr(probe.url, sizeof(probe.url), url);
		/* Reichlich gross, damit die Zeit das Ende bestimmt und nicht
		 * die Datei. Die Bloecke liegen innerhalb der echten Datei. */
		probe.size = 256ull * 1024 * 1024;

		memset(&o, 0, sizeof(o));
		o.cb = nettest_cb;
		o.cancel = &g_dlui.cancel;
		o.conns = nt_plan[i];
		o.nofile = 1;
		o.limit_ms = g_nt.secs * 1000;

		g_dlui.cancel = 0;

		t0 = gettime();
		r = cf_download_run(&probe, &o);
		ms = ticks_to_millisecs(diff_ticks(t0, gettime()));

		if (g_dlui.cancel)
			return;

		if (r < 0 && r != CF_HTTP_ECANCEL) {
			char msg[160];
			snprintf(msg, sizeof(msg), "%d %s: %s", nt_plan[i],
			         cf_t("set.conns"), cf_download_strerror(r));
			cf_ui_message(cf_t("net.test"), msg);
			return;
		}

		if (g_nt.cur_rate)
			g_nt.rate[i] = g_nt.cur_rate;
		else if (ms)
			g_nt.rate[i] = (uint32_t)((g_nt.done * 1000ull) / ms);
	}

	for (;;) {
		u32 d;
		nettest_draw(1);
		d = cf_ui_poll();
		if (d & (PAD_BUTTON_A | PAD_BUTTON_B))
			return;
	}
}

/* ------------------------------------------------------------------ */
/* Diagnose                                                           */
/* ------------------------------------------------------------------ */

static void screen_diag(app *a)
{
	cf_exi_slot slots[CF_EXI_SLOTS];
	size_t nslots = cf_exi_scan(slots, CF_EXI_SLOTS);

	for (;;) {
		int y = CF_UI_BODY_Y;
		int lh = cf_ui_line_h(CF_S_SMALL) + 2;
		size_t i;
		char line[200];
		u32 d;

		cf_ui_begin(cf_t("menu.diag"), NULL);

		/* Gegen welche libogc2 dieses DOL gebaut wurde. Von aussen ist
		 * das sonst nicht zu erkennen, und es entscheidet ueber den
		 * Durchsatz. */
		snprintf(line, sizeof(line), "TCP-Fenster %s   ·   %s %d",
#if CF_BIG_WINDOW
		         "35040 (eigene libogc2)",
#else
		         "2920 (Paket)",
#endif
		         cf_t("set.conns"), a->conns);
		cf_ui_text(CF_UI_X0, y, CF_S_BODY, line);
		y += cf_ui_line_h(CF_S_BODY) + 12;

		/* Die rohe Kennung steht mit da, weil sie nicht eindeutig ist:
		 * ein SD-Adapter und ein Netzwerkadapter mit ENC28J60 oder W5500
		 * melden beide 0xFFFFFFFF. */
		cf_ui_text(CF_UI_X0, y, CF_S_ACCENT, "EXI");
		y += lh + 2;
		for (i = 0; i < nslots; i++) {
			snprintf(line, sizeof(line), "%-20s  EXI%d:%d   %08lx   %s",
			         slots[i].name, slots[i].ch, slots[i].dev,
			         (unsigned long)slots[i].id, cf_exi_meaning(&slots[i]));
			cf_ui_text_fit(CF_UI_X0 + 12, y, CF_UI_W - 12, CF_S_SMALL, line);
			y += lh;
		}
		y += 8;

		cf_ui_text(CF_UI_X0, y, CF_S_ACCENT, cf_t("status.storage"));
		y += lh + 2;
		for (i = 0; i < cf_storage_count(); i++) {
			const cf_device *dv = cf_storage_at(i);
			char fr[24], tt[24];

			if (!dv)
				continue;
			cf_fmt_size(fr, sizeof(fr), dv->avail);
			cf_fmt_size(tt, sizeof(tt), dv->total);
			snprintf(line, sizeof(line), "%-20s  %-7s  %s / %s",
			         dv->label, dv->root, fr, tt);
			cf_ui_text_fit(CF_UI_X0 + 12, y, CF_UI_W - 12, CF_S_SMALL, line);
			y += lh;
		}
		y += 8;

		cf_ui_text(CF_UI_X0, y, CF_S_ACCENT, cf_t("status.network"));
		y += lh + 2;
		if (a->net.up)
			snprintf(line, sizeof(line), "IP %s   Maske %s   Gateway %s",
			         a->net.ip, a->net.mask, a->net.gw);
		else
			snprintf(line, sizeof(line), "%s   %s", cf_t("net.fail"),
			         a->net.err);
		cf_ui_text_fit(CF_UI_X0 + 12, y, CF_UI_W - 12,
		               a->net.up ? CF_S_SMALL : CF_S_ERR, line);

		cf_ui_end(cf_t("hint.diag"));

		d = cf_ui_poll();
		if (d & PAD_BUTTON_X) {
			screen_nettest(a);
			return;
		}
		if (d & (PAD_BUTTON_B | PAD_BUTTON_A))
			return;
	}
}

/* ------------------------------------------------------------------ */
/* Community                                                          */
/* ------------------------------------------------------------------ */

#define URL_DISCORD "https://discord.gg/EqUpCbGM5p"
#define URL_GITHUB  "https://github.com/konsolenschatz"
#define URL_YOUTUBE "https://www.youtube.com/@Konsolenschatz"

/* Dunkel auf Weiss mit vier Modulen Ruhezone, unabhaengig vom Design -
 * so lesen Telefonkameras am zuverlaessigsten. */
static int draw_qr(const cf_qr *q, int x, int y, int mod)
{
	int dim = (q->size + 8) * mod, r, c;

	cf_gfx_rect(x, y, dim, dim, 0xFFFFFFFF);
	for (r = 0; r < q->size; r++)
		for (c = 0; c < q->size; c++)
			if (q->m[r][c])
				cf_gfx_rect(x + (c + 4) * mod, y + (r + 4) * mod,
				            mod, mod, 0x000000FF);
	return dim;
}

static void screen_community(app *a)
{
	static cf_qr qd, qg, qy;
	static int ready;

	(void)a;
	if (!ready) {
		cf_qr_encode(&qd, URL_DISCORD, strlen(URL_DISCORD));
		cf_qr_encode(&qg, URL_GITHUB, strlen(URL_GITHUB));
		cf_qr_encode(&qy, URL_YOUTUBE, strlen(URL_YOUTUBE));
		ready = 1;
	}

	for (;;) {
		int y = CF_UI_BODY_Y;
		int big = (qd.size + 8) * 6, small = (qg.size + 8) * 4;
		int gap = (CF_UI_W - big - 2 * small) / 2;
		int x1 = CF_UI_X0, x2 = x1 + big + gap, x3 = x2 + small + gap;
		int qy0, ly;
		u32 d;

		cf_ui_begin(cf_t("menu.socials"), NULL);

		y += 16;

		qy0 = y;
		draw_qr(&qd, x1, qy0, 6);
		draw_qr(&qg, x2, qy0 + big - small, 4);
		draw_qr(&qy, x3, qy0 + big - small, 4);

		ly = qy0 + big + 6;
		cf_ui_text_center(x1 + big / 2, ly, CF_S_BOLD, "Discord");
		cf_ui_text_center(x2 + small / 2, ly, CF_S_BOLD, "GitHub");
		cf_ui_text_center(x3 + small / 2, ly, CF_S_BOLD, "YouTube");
		ly += cf_ui_line_h(CF_S_BODY);
		cf_ui_text_center(x1 + big / 2, ly, CF_S_SMALLDIM, "discord.gg/EqUpCbGM5p");
		cf_ui_text_center(x2 + small / 2, ly, CF_S_SMALLDIM, "konsolenschatz");
		cf_ui_text_center(x3 + small / 2, ly, CF_S_SMALLDIM, "@Konsolenschatz");

		/* Text ueber den kleinen Codes, neben dem grossen */
		cf_ui_text_fit(x2, qy0, CF_UI_X1 - x2, CF_S_BODY, cf_t("com.discord"));
		cf_ui_text_fit(x2, qy0 + cf_ui_line_h(CF_S_BODY) + 2, CF_UI_X1 - x2,
		               CF_S_SMALLDIM, cf_t("com.subscribe"));

		cf_ui_end(cf_t("hint.back"));

		d = cf_ui_poll();
		if (d & (PAD_BUTTON_B | PAD_BUTTON_A))
			return;
	}
}

/* ------------------------------------------------------------------ */
/* Hauptmenue                                                         */
/* ------------------------------------------------------------------ */

#define N_MENU 6

static void screen_main(app *a)
{
	size_t sel = 0;

	for (;;) {
		const char *items[N_MENU];
		size_t i;
		u32 d;
		int mx = CF_UI_X0, my = CF_UI_BODY_Y + 8;
		int mw = 250, mh = 38;
		int px = mx + mw + 24, pw = CF_UI_X1 - px;
		int y;
		char line[160];

		items[0] = cf_t("menu.catalog");
		items[1] = cf_t("menu.queue");
		items[2] = cf_t("menu.sources");
		items[3] = cf_t("menu.settings");
		items[4] = cf_t("menu.diag");
		items[5] = cf_t("menu.socials");

		cf_ui_begin(cf_t("app.title"), a->net.up ? a->net.ip : NULL);

		/* Menue links */
		for (i = 0; i < N_MENU; i++) {
			int ry = my + (int)i * (mh + 6);
			int selected = (i == sel);

			if (selected) {
				cf_gfx_rect(mx, ry, mw, mh, CF_COL_ROWSEL);
				cf_gfx_rect(mx, ry, 4, mh, CF_COL_ACCENT);
			} else {
				cf_ui_panel(mx, ry, mw, mh);
			}
			cf_ui_text(mx + 18, ry + (mh - cf_ui_line_h(CF_S_BODY)) / 2,
			           selected ? CF_S_BODY : CF_S_DIM, items[i]);
		}

		/* Statusfeld rechts */
		cf_ui_panel(px, my, pw, N_MENU * (mh + 6) - 6);
		y = my + 14;

		cf_ui_text(px + 14, y, CF_S_SMALLDIM, cf_t("status.network"));
		y += cf_ui_line_h(CF_S_SMALL) + 2;
		cf_ui_text_fit(px + 14, y, pw - 28,
		               a->net.up ? CF_S_OK : CF_S_ERR,
		               a->net.up ? a->net.ip : cf_t("net.fail"));
		y += cf_ui_line_h(CF_S_BODY) + 10;

		cf_ui_text(px + 14, y, CF_S_SMALLDIM, cf_t("status.storage"));
		y += cf_ui_line_h(CF_S_SMALL) + 2;
		{
			const cf_device *dv = cf_storage_at(a->dev);
			if (dv) {
				char fr[24];
				cf_fmt_size(fr, sizeof(fr), dv->avail);
				snprintf(line, sizeof(line), "%s · %s %s", dv->label, fr,
				         "frei");
				cf_ui_text_fit(px + 14, y, pw - 28, CF_S_BODY, line);
			} else {
				cf_ui_text_fit(px + 14, y, pw - 28, CF_S_ERR,
				               cf_t("store.none"));
			}
		}
		y += cf_ui_line_h(CF_S_BODY) + 10;

		cf_ui_text(px + 14, y, CF_S_SMALLDIM, cf_t("status.catalog"));
		y += cf_ui_line_h(CF_S_SMALL) + 2;
		snprintf(line, sizeof(line), cf_t("status.titles"),
		         (unsigned)cf_catalog_total(a->cat));
		cf_ui_text_fit(px + 14, y, pw - 28, CF_S_BODY, line);
		y += cf_ui_line_h(CF_S_BODY) + 10;

		cf_ui_text(px + 14, y, CF_S_SMALLDIM, cf_t("status.queue"));
		y += cf_ui_line_h(CF_S_SMALL) + 2;
		{
			char tot[24];
			cf_fmt_size(tot, sizeof(tot), cf_queue_bytes_waiting(&a->q));
			snprintf(line, sizeof(line), cf_t("status.waiting"),
			         (unsigned)cf_queue_count_waiting(&a->q), tot);
			cf_ui_text_fit(px + 14, y, pw - 28, CF_S_BODY, line);
		}

		cf_ui_end(cf_t("hint.main"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_UP)
			sel = (sel == 0) ? N_MENU - 1 : sel - 1;
		if (d & PAD_BUTTON_DOWN)
			sel = (sel + 1) % N_MENU;

		if (d & PAD_BUTTON_A) {
			switch (sel) {
			case 0: screen_catalog(a);  break;
			case 1: screen_queue(a);    break;
			case 2: screen_sources(a);  break;
			case 3: screen_settings(a); break;
			case 4: screen_diag(a);     break;
			case 5: screen_community(a); break;
			default: break;
			}
		}

		if ((d & PAD_BUTTON_START) && (PAD_ButtonsHeld(0) & PAD_TRIGGER_Z))
			return;
	}
}

/* ------------------------------------------------------------------ */

int main(void)
{
	app *a = &g_app;
	size_t ndev;

	if (!cf_ui_init()) {
		/* Ohne Grafik gibt es nichts zu zeigen; die Konsole von libogc
		 * ist nicht mehr eingebunden. Stillhalten statt abstuerzen. */
		for (;;)
			VIDEO_WaitVSync();
	}

	a->verify = 1;
	a->cat_view = 0;
	/* Mehrere Verbindungen sind beim GameCube kein Luxus: lwIP
	 * begrenzt eine einzelne auf 2920 Bytes unterwegs, der Durchsatz
	 * haengt damit allein an der Umlaufzeit. Begruendung samt Zahlen an
	 * CF_DL_MAX_CONN in cf_download.h.
	 *
	 * Gegen die selbst gebaute libogc2 mit 35040 Bytes Fenster
	 * (tools/build-libogc2.sh) reichen sechs fuer die Adaptergrenze;
	 * gegen das Paket sind acht besser als sechs. */
#if CF_BIG_WINDOW
	a->conns = 6;
#else
	a->conns = 8;
#endif
	a->lang = CF_LANG_DE;

	cf_i18n_builtin();

	a->cat = cf_catalog_new();
	if (!a->cat) {
		cf_ui_message("CubeFetch", "Speicher reicht nicht.");
		return 0;
	}

	/* --- Speicher --- */
	splash("CubeFetch", cf_t("boot.storage"), NULL);

	ndev = cf_storage_scan();
	if (ndev > 0) {
		paths_for_device(a, 0);
		config_load(a);
		lang_load(a);
		sources_load(a);
		queue_load(a);
		/* GameTDB-Index von der Karte, wenn er schon einmal geholt
		 * wurde. Sonst bleibt das Detailfeld bei dem, was die Quelle
		 * weiss, bis man die Datenbank in den Einstellungen laedt. */
		cf_meta_init(a->cfgdir, meta_lang(a));
	}

	/* --- Netzwerk --- */
	splash("CubeFetch", cf_t("net.connecting"), cf_t("boot.network"));

	cf_net_up(&a->net);

	/* --- EXI-Konflikt, siehe docs/ARCHITECTURE.md ---
	 *
	 * Streiten koennen sich nur Geraete auf demselben EXI-Kanal, und
	 * das trifft allein Kanal 0: dort liegen Memory Card Slot A und
	 * Serial Port 1. Slot B hat Kanal 1 fuer sich, Serial Port 2
	 * Kanal 2. */
	if (a->net.up && ndev > 0) {
		const cf_device *d = cf_storage_at(a->dev);
		cf_exi_slot sl[CF_EXI_SLOTS];
		size_t ns = cf_exi_scan(sl, CF_EXI_SLOTS);
		size_t k;
		int sp1_busy = 0;

		for (k = 0; k < ns; k++) {
			if (sl[k].ch == 0 && sl[k].dev == 2 && sl[k].probed)
				sp1_busy = 1;
		}

		if (d && d->exi_channel == 0 && sp1_busy)
			cf_ui_message("CubeFetch", cf_t("store.exi_warning"));
	}

	if (ndev == 0)
		cf_ui_message("CubeFetch", cf_t("store.none"));
	else if (!a->net.up)
		cf_ui_message("CubeFetch", cf_t("net.fail"));

	screen_main(a);

	cf_meta_free();
	cf_catalog_free(a->cat);
	free(a->lang_buf);
	cf_net_down();

	return 0;
}
