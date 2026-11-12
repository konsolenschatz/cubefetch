#include "platform/cf_ui.h"
#include "core/cf_i18n.h"
#include "core/cf_str.h"

#include <stdio.h>
#include <string.h>

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

/* Vom Makefile aus data/fonts eingebettet. */
#include "dejavusans_ttf.h"
#include "dejavusans_bold_ttf.h"
#include "ks_coin.h"

static cf_font *g_body;
static cf_font *g_small;
static cf_font *g_title;
static cf_font *g_bold;
static int      g_ok;

/* Tastenwiederholung beim Halten, sonst ist eine Liste mit Hunderten
 * Eintraegen nicht zu bedienen. */
#define REPEAT_DELAY_MS  350
#define REPEAT_RATE_MS   45

int cf_ui_init(void)
{
	cf_ui_set_theme(CF_THEME_KS);
	if (!cf_gfx_init())
		return 0;

	/* 18 Punkte fuer Text, 14 fuer Hinweise, 24 fuer Titel. Auf einer
	 * Roehre mit 480 Zeilen ist 18 die Grenze, unter der Umlautpunkte
	 * im Zeilensprung verschwinden. */
	return cf_ui_reload_fonts(NULL, NULL, 0);
}

int cf_ui_reload_fonts(const char *chars, const void *fb, size_t fblen)
{
	cf_font *b, *s, *t, *bo;

	cf_gfx_charset(chars);
	cf_gfx_set_fallback(fb, fblen);

	b  = cf_font_load(dejavusans_ttf, dejavusans_ttf_size, 18);
	s  = cf_font_load(dejavusans_ttf, dejavusans_ttf_size, 14);
	t  = cf_font_load(dejavusans_bold_ttf, dejavusans_bold_ttf_size, 24);
	bo = cf_font_load(dejavusans_bold_ttf, dejavusans_bold_ttf_size, 18);

	if (!b || !s || !t || !bo) {
		/* Alte Schriften behalten, wenn die neuen nicht passen. */
		cf_font_free(b); cf_font_free(s); cf_font_free(t); cf_font_free(bo);
		return g_ok;
	}

	/* Erst warten, bis die GPU die alten Atlanten nicht mehr liest. */
	GX_DrawDone();
	cf_font_free(g_body); cf_font_free(g_small);
	cf_font_free(g_title); cf_font_free(g_bold);
	g_body = b; g_small = s; g_title = t; g_bold = bo;
	g_ok = 1;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Eingabe                                                            */
/* ------------------------------------------------------------------ */

u32 cf_ui_poll(void)
{
	static u32 last_held;
	static u64 t_change;
	static u64 t_next;
	static int repeating;

	u32 down, held;
	u64 now;

	PAD_ScanPads();
	down = PAD_ButtonsDown(0);
	held = PAD_ButtonsHeld(0);
	now = gettime();

	if (held != last_held) {
		last_held = held;
		t_change = now;
		repeating = 0;
		return down;
	}

	/* Nur Richtungen wiederholen. Ein gehaltenes A soll nicht
	 * versehentlich zwanzig Titel in die Schlange legen. */
	{
		u32 dirs = held & (PAD_BUTTON_UP | PAD_BUTTON_DOWN |
		                   PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
		u32 ms;

		if (!dirs)
			return down;

		ms = ticks_to_millisecs(diff_ticks(t_change, now));

		if (!repeating) {
			if (ms < REPEAT_DELAY_MS)
				return down;
			repeating = 1;
			t_next = now;
		}

		if (ticks_to_millisecs(diff_ticks(t_next, now)) < REPEAT_RATE_MS)
			return down;

		t_next = now;
		return down | dirs;
	}
}

/* ------------------------------------------------------------------ */
/* Designs                                                            */
/* ------------------------------------------------------------------ */

/* bg, head, panel, line, rowsel, text, dim, accent, ok, warn, err, shadow */
static const cf_palette THEMES[CF_THEME_COUNT] = {
	/* Konsolenschatz: Farben aus dem Kanal-Stil */
	{ 0x1A1009FF, 0x2A1A0DFF, 0x24160BFF, 0x5A3A1EFF, 0x5A3818FF,
	  0xFAECCDFF, 0xB89C78FF, 0xFFC440FF, 0x4EC468FF, 0xF0A040FF,
	  0xD6483CFF, 0x000000A0 },
	/* Klassisch: das bisherige Blau */
	{ 0x0F141AFF, 0x18222CFF, 0x151D26FF, 0x2A3744FF, 0x2E4A68FF,
	  0xE8ECF1FF, 0x8893A2FF, 0x5EB4FFFF, 0x5FD08AFF, 0xF2B544FF,
	  0xF06B6BFF, 0x00000099 },
	/* Schwarz */
	{ 0x000000FF, 0x0C0C0CFF, 0x0A0A0AFF, 0x303030FF, 0x2A2A2AFF,
	  0xF2F2F2FF, 0x8C8C8CFF, 0xFFFFFFFF, 0x5FD08AFF, 0xF2B544FF,
	  0xF06B6BFF, 0x000000B0 },
	/* Hell */
	{ 0xECEFF3FF, 0xDCE2E9FF, 0xFFFFFFFF, 0xC2CAD4FF, 0xC6DAF2FF,
	  0x1A1F26FF, 0x5C6673FF, 0x1E6FD0FF, 0x1E9A50FF, 0xB07400FF,
	  0xC83232FF, 0x00000055 },
	/* Lila */
	{ 0x150F1FFF, 0x221833FF, 0x1C142AFF, 0x3C2D55FF, 0x4A3272FF,
	  0xEEE8F6FF, 0x9C8EB2FF, 0xB57BFFFF, 0x5FD08AFF, 0xF2B544FF,
	  0xF06B6BFF, 0x00000099 },
	/* Orange */
	{ 0x1A0F07FF, 0x2A1A0CFF, 0x22150AFF, 0x4C3016FF, 0x6A3C12FF,
	  0xF6ECE2FF, 0xB09880FF, 0xFF8A2AFF, 0x5FD08AFF, 0xFFC44AFF,
	  0xF06B6BFF, 0x00000099 },
	/* Pink */
	{ 0x1E0C16FF, 0x2E1424FF, 0x26101EFF, 0x502642FF, 0x702C56FF,
	  0xF8E8F0FF, 0xB68EA4FF, 0xFF6EB4FF, 0x5FD08AFF, 0xF2B544FF,
	  0xFF7A6AFF, 0x00000099 },
	/* Gruen */
	{ 0x0A160EFF, 0x12241AFF, 0x0F1E15FF, 0x24422FFF, 0x1F5C37FF,
	  0xE6F4EAFF, 0x8AAA96FF, 0x4EDB7EFF, 0x9BE86AFF, 0xF2B544FF,
	  0xF06B6BFF, 0x00000099 },
};

static const char *const THEME_CODE[CF_THEME_COUNT] = {
	"konsolenschatz", "classic", "black", "light",
	"purple", "orange", "pink", "green"
};

cf_palette cf_pal;
static int g_theme;

void cf_ui_set_theme(int theme)
{
	if (theme < 0 || theme >= CF_THEME_COUNT)
		theme = CF_THEME_KS;
	g_theme = theme;
	cf_pal = THEMES[theme];
}

int cf_ui_theme(void)
{
	return g_theme;
}

const char *cf_ui_theme_code(int theme)
{
	return THEME_CODE[(theme >= 0 && theme < CF_THEME_COUNT) ? theme : 0];
}

int cf_ui_theme_from_code(const char *code)
{
	int i;

	for (i = 0; i < CF_THEME_COUNT; i++)
		if (!strcmp(code, THEME_CODE[i]))
			return i;
	return CF_THEME_KS;
}

const char *cf_ui_theme_key(int theme)
{
	static char key[32];

	snprintf(key, sizeof(key), "theme.%s", cf_ui_theme_code(theme));
	return key;
}

/* Gefuellter Kreis aus waagerechten Streifen - GX kann es auch anders,
 * aber fuer eine Muenze in Kopfbandgroesse reicht das voellig. */
static void disc(int cx, int cy, int r, uint32_t col)
{
	int dy;

	for (dy = -r; dy <= r; dy++) {
		int dx = 0;
		while ((dx + 1) * (dx + 1) + dy * dy <= r * r)
			dx++;
		cf_gfx_rect(cx - dx, cy + dy, 2 * dx + 1, 1, col);
	}
}

/* Die ?-Muenze: dunkle Kontur, goldene Kante, Flaeche, Glanz, Fragezeichen.
 * Das Fragezeichen ist aus Bloecken gesetzt, damit es pixelig wirkt. */
static void coin(int cx, int cy, int r)
{
	static const char *const Q[7] = {
		".###.",
		"#...#",
		"....#",
		"...#.",
		"..#..",
		".....",
		"..#.."
	};
	int px = r / 5 < 2 ? 2 : r / 5, row, col;
	int qx = cx - (5 * px) / 2, qy = cy - (7 * px) / 2;

	disc(cx, cy, r + 2, 0x160C05FF);
	disc(cx, cy, r, 0xB87A1CFF);
	disc(cx - 1, cy - 1, r - 3, 0xFFC440FF);
	cf_gfx_rect(cx - r / 2, cy - r / 2 - 1, px, px, 0xFFE896FF);

	for (row = 0; row < 7; row++)
		for (col = 0; col < 5; col++)
			if (Q[row][col] == '#') {
				cf_gfx_rect(qx + col * px + 1, qy + row * px + 1, px, px,
				            0xB87A1CFF);
				cf_gfx_rect(qx + col * px, qy + row * px, px, px,
				            0x160C05FF);
			}
}

/* ------------------------------------------------------------------ */
/* Stile                                                              */
/* ------------------------------------------------------------------ */

static const cf_font *style_font(cf_style st)
{
	switch (st) {
	case CF_S_SMALL:
	case CF_S_SMALLDIM: return g_small;
	case CF_S_TITLE:    return g_title;
	case CF_S_BOLD:     return g_bold;
	default:            return g_body;
	}
}

static uint32_t style_color(cf_style st)
{
	switch (st) {
	case CF_S_DIM:
	case CF_S_SMALLDIM: return CF_COL_DIM;
	case CF_S_ACCENT:   return CF_COL_ACCENT;
	case CF_S_OK:       return CF_COL_OK;
	case CF_S_WARN:     return CF_COL_WARN;
	case CF_S_ERR:      return CF_COL_ERR;
	default:            return CF_COL_TEXT;
	}
}

int cf_ui_line_h(cf_style st)
{
	return cf_font_height(style_font(st));
}

int cf_ui_text_width(cf_style st, const char *utf8)
{
	return cf_gfx_text_width(style_font(st), utf8);
}

int cf_ui_text(int x, int y, cf_style st, const char *utf8)
{
	return cf_gfx_text(style_font(st), x, y, style_color(st), utf8);
}

int cf_ui_text_fit(int x, int y, int maxw, cf_style st, const char *utf8)
{
	return cf_gfx_text_fit(style_font(st), x, y, maxw, style_color(st), utf8);
}

/* Lauftext: passt der Text nicht, faehrt er nach einer Pause langsam nach
 * links durch, wartet am Ende und springt zurueck. ms ist die Zeit, seit
 * der Eintrag angewaehlt ist - so beginnt jeder neue Titel vorn.
 *
 * 40 Bildpunkte je Sekunde: schnell genug, dass man nicht ewig wartet,
 * langsam genug, dass man auf einer Roehre im Zeilensprung mitlesen kann. */
#define MARQUEE_PAUSE_MS 1200
#define MARQUEE_PX_S     40

int cf_ui_text_marquee(int x, int y, int maxw, cf_style st,
                       const char *utf8, uint32_t ms)
{
	int tw = cf_ui_text_width(st, utf8);
	int over, travel, cycle, t, off;

	if (tw <= maxw)
		return cf_ui_text(x, y, st, utf8);

	over = tw - maxw + 6;
	travel = over * 1000 / MARQUEE_PX_S;
	cycle = MARQUEE_PAUSE_MS + travel + MARQUEE_PAUSE_MS;
	t = (int)(ms % (uint32_t)cycle);

	if (t < MARQUEE_PAUSE_MS)
		off = 0;
	else if (t < MARQUEE_PAUSE_MS + travel)
		off = (t - MARQUEE_PAUSE_MS) * MARQUEE_PX_S / 1000;
	else
		off = over;

	cf_gfx_clip_push(x, y - 4, maxw, cf_ui_line_h(st) + 8);
	cf_ui_text(x - off, y, st, utf8);
	cf_gfx_clip_pop();
	return maxw;
}

void cf_ui_text_right(int xr, int y, cf_style st, const char *utf8)
{
	int w = cf_ui_text_width(st, utf8);
	cf_ui_text(xr - w, y, st, utf8);
}

void cf_ui_text_center(int xc, int y, cf_style st, const char *utf8)
{
	int w = cf_ui_text_width(st, utf8);
	cf_ui_text(xc - w / 2, y, st, utf8);
}

int cf_ui_wrap(int x, int y, int w, int maxlines, cf_style st,
               const char *utf8)
{
	const cf_font *f = style_font(st);
	uint32_t col = style_color(st);
	int lh = cf_font_height(f);
	const char *p = utf8;
	int lines = 0;

	while (*p && lines < maxlines) {
		const char *end = p;       /* bis hierhin passt es */
		const char *q = p;
		char buf[512];
		size_t n;

		/* Woertern folgen, solange die Zeile passt. */
		for (;;) {
			const char *sp = strchr(q, ' ');
			size_t len = sp ? (size_t)(sp - p) : strlen(p);

			if (len >= sizeof(buf) - 1)
				len = sizeof(buf) - 1;
			memcpy(buf, p, len);
			buf[len] = '\0';

			if (cf_gfx_text_width(f, buf) > w && end != p)
				break;
			end = p + len;
			if (!sp)
				break;
			q = sp + 1;
			if (cf_gfx_text_width(f, buf) > w)
				break;
		}

		if (end == p) {
			/* Ein einzelnes Wort, laenger als die Zeile. */
			cf_gfx_text_fit(f, x, y, w, col, p);
			break;
		}

		n = (size_t)(end - p);
		if (n >= sizeof(buf))
			n = sizeof(buf) - 1;
		memcpy(buf, p, n);
		buf[n] = '\0';
		cf_gfx_text(f, x, y, col, buf);

		y += lh;
		lines++;
		p = end;
		while (*p == ' ')
			p++;
	}

	return lines;
}

/* ------------------------------------------------------------------ */
/* Flaechen                                                           */
/* ------------------------------------------------------------------ */

void cf_ui_rect(int x, int y, int w, int h, uint32_t rgba)
{
	cf_gfx_rect(x, y, w, h, rgba);
}

void cf_ui_panel(int x, int y, int w, int h)
{
	cf_gfx_rect(x, y, w, h, CF_COL_PANEL);
	cf_gfx_frame(x, y, w, h, 1, CF_COL_LINE);
}

void cf_ui_hline(int x, int y, int w)
{
	cf_gfx_rect(x, y, w, 1, CF_COL_LINE);
}

void cf_ui_bar(int x, int y, int w, int h, double frac)
{
	int fill;

	if (frac < 0) frac = 0;
	if (frac > 1) frac = 1;
	fill = (int)((w - 2) * frac);

	cf_gfx_rect(x, y, w, h, CF_COL_PANEL);
	cf_gfx_frame(x, y, w, h, 1, CF_COL_LINE);
	if (fill > 0)
		cf_gfx_rect(x + 1, y + 1, fill, h - 2, CF_COL_ACCENT);
}

void cf_ui_image(int x, int y, int w, int h, const cf_tex *t)
{
	cf_gfx_draw_tex(t, x, y, w, h, 0xFFFFFFFF);
}

/* ------------------------------------------------------------------ */
/* Rahmen                                                             */
/* ------------------------------------------------------------------ */

void cf_ui_begin(const char *title, const char *right)
{
	int th;

	cf_gfx_begin(CF_COL_BG);

	/* Kopfband ueber die ganze Breite, Text im sicheren Bereich. */
	cf_gfx_rect(0, CF_UI_Y0, CF_GFX_W, CF_UI_HEAD_H, CF_COL_HEAD);
	cf_gfx_rect(0, CF_UI_Y0 + CF_UI_HEAD_H, CF_GFX_W, 2, CF_COL_ACCENT);

	th = cf_font_height(g_title);
	{
		int tx = CF_UI_X0;

		if (g_theme == CF_THEME_KS) {
			static cf_tex ct;
			static int    cok = -1;

			/* Pauls Muenze, beim ersten Mal in eine Textur. */
			if (cok < 0)
				cok = cf_gfx_tex_rgba8(&ct, KS_COIN_W, KS_COIN_H,
				                       ks_coin_rgba) ? 1 : 0;
			if (cok) {
				cf_gfx_draw_tex(&ct, CF_UI_X0,
				                CF_UI_Y0 + (CF_UI_HEAD_H - KS_COIN_H) / 2,
				                KS_COIN_W, KS_COIN_H, 0xFFFFFFFF);
				tx += KS_COIN_W + 10;
			} else {
				int r = CF_UI_HEAD_H / 2 - 6;
				coin(CF_UI_X0 + r + 2, CF_UI_Y0 + CF_UI_HEAD_H / 2, r);
				tx += 2 * r + 14;
			}
		}
		cf_ui_text_fit(tx, CF_UI_Y0 + (CF_UI_HEAD_H - th) / 2,
		               (right ? CF_UI_W - 180 : CF_UI_W) - (tx - CF_UI_X0),
		               CF_S_TITLE, title);
	}

	if (right && *right) {
		int sh = cf_font_height(g_body);
		cf_ui_text_right(CF_UI_X1, CF_UI_Y0 + (CF_UI_HEAD_H - sh) / 2,
		                 CF_S_DIM, right);
	}
}

void cf_ui_end(const char *hints)
{
	cf_gfx_noclip();
	cf_ui_hline(CF_UI_X0, CF_UI_FOOT_Y - 6, CF_UI_W);
	if (hints && *hints)
		cf_ui_text_fit(CF_UI_X0, CF_UI_FOOT_Y, CF_UI_W, CF_S_SMALLDIM, hints);
	cf_gfx_end();
}

/* ------------------------------------------------------------------ */
/* Liste                                                              */
/* ------------------------------------------------------------------ */

int cf_ui_list_rows(int h)
{
	int n = h / CF_UI_ROW_H;
	return n < 1 ? 1 : n;
}

int cf_ui_list(int x, int y, int w, int h, size_t count, size_t sel,
               size_t *top, cf_ui_rowfn fn, void *ctx)
{
	int rows = cf_ui_list_rows(h);
	size_t i;
	int listw = w;

	if (count > (size_t)rows)
		listw -= 10;   /* Platz fuer den Rollbalken */

	/* top so nachfuehren, dass sel sichtbar ist. */
	if (sel < *top)
		*top = sel;
	if (sel >= *top + (size_t)rows)
		*top = sel - (size_t)rows + 1;
	if (count <= (size_t)rows)
		*top = 0;
	else if (*top + (size_t)rows > count)
		*top = count - (size_t)rows;

	cf_gfx_clip(x, y, w, h);

	for (i = 0; i < (size_t)rows && *top + i < count; i++) {
		size_t idx = *top + i;
		int ry = y + (int)i * CF_UI_ROW_H;
		int selected = (idx == sel);

		if (selected)
			cf_gfx_rect(x, ry, listw, CF_UI_ROW_H, CF_COL_ROWSEL);

		fn(ctx, idx, x + 8, ry, listw - 16, selected);
	}

	cf_gfx_noclip();

	/* Rollbalken rechts: Position und Anteil der Seite. */
	if (count > (size_t)rows) {
		int track_h = h;
		int knob_h = (int)((uint64_t)track_h * rows / count);
		int knob_y;

		if (knob_h < 12)
			knob_h = 12;
		knob_y = y + (int)((uint64_t)(track_h - knob_h) * *top
		                   / (count - rows));

		cf_gfx_rect(x + w - 6, y, 4, track_h, CF_COL_PANEL);
		cf_gfx_rect(x + w - 6, knob_y, 4, knob_h, CF_COL_DIM);
	}

	return rows;
}

/* ------------------------------------------------------------------ */
/* Meldungen                                                          */
/* ------------------------------------------------------------------ */

static void modal_draw(const char *title, const char *text, const char *hint)
{
	int w = 480, h = 200;
	int x = (CF_GFX_W - w) / 2, y = (CF_GFX_H - h) / 2;

	cf_gfx_begin(CF_COL_BG);

	/* Dunkler Vorhang, darauf das Fenster. */
	cf_gfx_rect(0, 0, CF_GFX_W, CF_GFX_H, CF_COL_SHADOW);
	cf_gfx_rect(x + 4, y + 6, w, h, CF_COL_SHADOW);
	cf_ui_panel(x, y, w, h);
	cf_gfx_rect(x, y, w, 2, CF_COL_ACCENT);

	cf_ui_text_fit(x + 16, y + 14, w - 32, CF_S_BOLD, title);
	cf_ui_hline(x + 16, y + 42, w - 32);
	cf_ui_wrap(x + 16, y + 52, w - 32, 4, CF_S_BODY, text);

	cf_ui_text_right(x + w - 16, y + h - 30, CF_S_SMALLDIM, hint);

	cf_gfx_end();
}

void cf_ui_message(const char *title, const char *text)
{
	for (;;) {
		u32 d;

		modal_draw(title, text, cf_t("ui.ok_hint"));
		d = cf_ui_poll();
		if (d & (PAD_BUTTON_A | PAD_BUTTON_B))
			return;
	}
}

int cf_ui_confirm(const char *title, const char *text)
{
	for (;;) {
		u32 d;

		modal_draw(title, text, cf_t("ui.confirm_hint"));
		d = cf_ui_poll();
		if (d & PAD_BUTTON_A)
			return 1;
		if (d & PAD_BUTTON_B)
			return 0;
	}
}

/* ------------------------------------------------------------------ */
/* Bildschirmtastatur                                                 */
/* ------------------------------------------------------------------ */

#define KB_COLS 13
#define KB_ROWS 4

/* Jede Belegung ist KB_ROWS * KB_COLS Zeichen. Leerzeichen heisst
 * "Feld bleibt leer". */
static const char *kb_layout_text[] = {
	"abcdefghijklm"
	"nopqrstuvwxyz"
	"0123456789-_ "
	".,:;!?()'    ",

	"ABCDEFGHIJKLM"
	"NOPQRSTUVWXYZ"
	"0123456789-_ "
	".,:;!?()'    ",
};

/* Adressbelegung: was in einer URL gebraucht wird, liegt vorn. */
static const char *kb_layout_url[] = {
	"abcdefghijklm"
	"nopqrstuvwxyz"
	"0123456789./:"
	"-_~?=&%+#@   ",

	"ABCDEFGHIJKLM"
	"NOPQRSTUVWXYZ"
	"0123456789./:"
	"-_~?=&%+#@   ",
};

int cf_ui_keyboard(const char *title, char *buf, size_t bufsz, int url_mode)
{
	const char **layouts = url_mode ? kb_layout_url : kb_layout_text;
	int layer = 0;
	int cx = 0, cy = 0;
	size_t len = strlen(buf);

	const int key_w = 40, key_h = 34, gap = 4;
	const int grid_w = KB_COLS * key_w + (KB_COLS - 1) * gap;
	const int gx = (CF_GFX_W - grid_w) / 2;
	const int gy = CF_UI_BODY_Y + 70;

	for (;;) {
		const char *lay = layouts[layer];
		int r, c;
		u32 d;

		cf_ui_begin(title, url_mode ? "URL" : NULL);

		/* Eingabefeld mit Schreibmarke */
		cf_ui_panel(CF_UI_X0, CF_UI_BODY_Y, CF_UI_W, 40);
		{
			int tw = cf_ui_text_width(CF_S_BODY, buf);
			int maxw = CF_UI_W - 24;
			int tx = CF_UI_X0 + 12;

			cf_gfx_clip(CF_UI_X0 + 2, CF_UI_BODY_Y, CF_UI_W - 4, 40);
			if (tw > maxw)
				tx -= tw - maxw;    /* Ende sichtbar halten */
			cf_ui_text(tx, CF_UI_BODY_Y + 10, CF_S_BODY, buf);
			cf_gfx_rect(tx + tw + 2, CF_UI_BODY_Y + 9, 2,
			            cf_ui_line_h(CF_S_BODY) + 2, CF_COL_ACCENT);
			cf_gfx_noclip();
		}

		/* Tastenfeld */
		for (r = 0; r < KB_ROWS; r++) {
			for (c = 0; c < KB_COLS; c++) {
				char ch = lay[r * KB_COLS + c];
				char s[2] = { ch, '\0' };
				int kx = gx + c * (key_w + gap);
				int ky = gy + r * (key_h + gap);
				int cur = (r == cy && c == cx);

				if (ch == ' ')
					continue;

				cf_gfx_rect(kx, ky, key_w, key_h,
				            cur ? CF_COL_ACCENT : CF_COL_PANEL);
				cf_gfx_frame(kx, ky, key_w, key_h, 1,
				             cur ? CF_COL_ACCENT : CF_COL_LINE);
				{
					int tw = cf_ui_text_width(CF_S_BODY, s);
					int th = cf_ui_line_h(CF_S_BODY);
					cf_gfx_text(style_font(CF_S_BODY),
					            kx + (key_w - tw) / 2, ky + (key_h - th) / 2,
					            cur ? CF_COL_BG : CF_COL_TEXT, s);
				}
			}
		}

		{
			char info[64];
			snprintf(info, sizeof(info), "%s: %s",
			         cf_t("kb.layer"), layer ? "ABC" : "abc");
			cf_ui_text(CF_UI_X0, gy + KB_ROWS * (key_h + gap) + 10,
			           CF_S_SMALLDIM, info);
		}

		cf_ui_end(cf_t("kb.hints"));

		d = cf_ui_poll();

		if (d & PAD_BUTTON_UP)    cy = (cy + KB_ROWS - 1) % KB_ROWS;
		if (d & PAD_BUTTON_DOWN)  cy = (cy + 1) % KB_ROWS;
		if (d & PAD_BUTTON_LEFT)  cx = (cx + KB_COLS - 1) % KB_COLS;
		if (d & PAD_BUTTON_RIGHT) cx = (cx + 1) % KB_COLS;

		if (d & PAD_BUTTON_X)
			layer = layer ? 0 : 1;

		if (d & PAD_BUTTON_A) {
			char ch = lay[cy * KB_COLS + cx];
			if (ch != ' ' && len + 1 < bufsz) {
				buf[len++] = ch;
				buf[len] = '\0';
			}
		}

		if (d & PAD_BUTTON_Y) {
			if (len + 1 < bufsz) {
				buf[len++] = ' ';
				buf[len] = '\0';
			}
		}

		if (d & PAD_BUTTON_B) {
			if (len > 0)
				buf[--len] = '\0';
		}

		if (d & PAD_BUTTON_START)
			return 1;

		if (d & PAD_TRIGGER_Z)
			return 0;
	}
}
