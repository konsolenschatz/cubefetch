/* Gezeichnete Oberflaeche auf GX.
 *
 * Ersetzt die Konsolenfassung aus dem Testbuild. Was gleich bleibt: die
 * Bildschirme in main.c zeichnen sich in ihrer Tastenschleife jedes Bild
 * komplett neu - cf_ui_begin, Inhalt, cf_ui_end. Mit zwei Bildpuffern ist
 * das nicht nur erlaubt, sondern noetig: der Puffer, in den gezeichnet
 * wird, enthaelt das Bild von vor zwei Durchlaeufen.
 *
 * Koordinaten in Bildpunkten auf 640 x 480, Ursprung oben links. Der
 * sichtbare Bereich ist um den Overscan verkleinert, siehe CF_UI_X0 usw.
 * Text ist UTF-8 und darf Umlaute enthalten: cf_t() statt cf_tc().
 */
#ifndef CF_UI_H
#define CF_UI_H

#include <stddef.h>
#include <stdint.h>

#include <gccore.h>

#include "platform/cf_gfx.h"

/* Sicherer Bereich. Roehren schneiden am Rand ab. */
#define CF_UI_X0   28
#define CF_UI_X1   612
#define CF_UI_Y0   20
#define CF_UI_Y1   460
#define CF_UI_W    (CF_UI_X1 - CF_UI_X0)

/* Senkrechte Aufteilung */
#define CF_UI_HEAD_H   40                 /* Kopfband */
#define CF_UI_BODY_Y   (CF_UI_Y0 + CF_UI_HEAD_H + 10)
#define CF_UI_FOOT_Y   (CF_UI_Y1 - 26)    /* Oberkante Fusszeile */
#define CF_UI_BODY_H   (CF_UI_FOOT_Y - 8 - CF_UI_BODY_Y)
#define CF_UI_ROW_H    26                 /* Listenzeile */

/* Farben, 0xRRGGBBAA, aus dem aktiven Design. */
typedef struct {
	uint32_t bg, head, panel, line, rowsel, text, dim, accent;
	uint32_t ok, warn, err, shadow;
} cf_palette;

extern cf_palette cf_pal;

#define CF_COL_BG       (cf_pal.bg)
#define CF_COL_HEAD     (cf_pal.head)
#define CF_COL_PANEL    (cf_pal.panel)
#define CF_COL_LINE     (cf_pal.line)
#define CF_COL_ROWSEL   (cf_pal.rowsel)
#define CF_COL_TEXT     (cf_pal.text)
#define CF_COL_DIM      (cf_pal.dim)
#define CF_COL_ACCENT   (cf_pal.accent)
#define CF_COL_OK       (cf_pal.ok)
#define CF_COL_WARN     (cf_pal.warn)
#define CF_COL_ERR      (cf_pal.err)
#define CF_COL_SHADOW   (cf_pal.shadow)

/* Designs. Konsolenschatz ist der Standard: Braun und Gold, ?-Muenze im
 * Kopfband, feine Scanlines. */
enum {
	CF_THEME_KS = 0,
	CF_THEME_CLASSIC,
	CF_THEME_BLACK,
	CF_THEME_LIGHT,
	CF_THEME_PURPLE,
	CF_THEME_ORANGE,
	CF_THEME_PINK,
	CF_THEME_GREEN,
	CF_THEME_COUNT
};

void        cf_ui_set_theme(int theme);
int         cf_ui_theme(void);
const char *cf_ui_theme_code(int theme);    /* fuer die Konfiguration */
int         cf_ui_theme_from_code(const char *code);
const char *cf_ui_theme_key(int theme);     /* Sprachschluessel */

typedef enum {
	CF_S_BODY = 0,   /* normale Schrift, hell */
	CF_S_DIM,        /* normale Schrift, gedaempft */
	CF_S_SMALL,      /* kleine Schrift, hell */
	CF_S_SMALLDIM,   /* kleine Schrift, gedaempft */
	CF_S_TITLE,      /* grosse Schrift */
	CF_S_ACCENT,     /* normale Schrift, Akzentfarbe */
	CF_S_OK,
	CF_S_WARN,
	CF_S_ERR,
	CF_S_BOLD        /* normale Groesse, fett */
} cf_style;

int  cf_ui_init(void);

/* PAD_ScanPads und die Tasten, die in diesem Durchgang neu gedrueckt
 * wurden. Mit Wiederholung beim Halten, damit Listen scrollen. */
u32  cf_ui_poll(void);

/* Ein Bild: begin loescht und zeichnet das Kopfband, end die Fusszeile
 * mit den Tastenhinweisen und zeigt an. */
/* Schriften neu aufbauen: chars sind zusaetzlich benoetigte Zeichen
 * (die Sprachdatei), fb eine Zweitschrift fuer fehlende Zeichen. */
int  cf_ui_reload_fonts(const char *chars, const void *fb, size_t fblen);

void cf_ui_begin(const char *title, const char *right);
void cf_ui_end(const char *hints);

/* Text. Rueckgabe ist die gezeichnete Breite. */
int  cf_ui_text(int x, int y, cf_style st, const char *utf8);
int  cf_ui_text_fit(int x, int y, int maxw, cf_style st, const char *utf8);

/* Wie text_fit, aber statt abzuschneiden laeuft zu langer Text durch.
 * ms: Zeit seit der Eintrag angewaehlt ist, damit jeder vorn beginnt. */
int  cf_ui_text_marquee(int x, int y, int maxw, cf_style st,
                        const char *utf8, uint32_t ms);
void cf_ui_text_right(int xr, int y, cf_style st, const char *utf8);
void cf_ui_text_center(int xc, int y, cf_style st, const char *utf8);
int  cf_ui_text_width(cf_style st, const char *utf8);
int  cf_ui_line_h(cf_style st);

/* Fliesstext an Wortgrenzen umbrechen. Rueckgabe: Zahl der Zeilen. */
int  cf_ui_wrap(int x, int y, int w, int maxlines, cf_style st,
                const char *utf8);

/* Flaechen */
void cf_ui_rect(int x, int y, int w, int h, uint32_t rgba);
void cf_ui_panel(int x, int y, int w, int h);
void cf_ui_hline(int x, int y, int w);
void cf_ui_bar(int x, int y, int w, int h, double frac);
void cf_ui_image(int x, int y, int w, int h, const cf_tex *t);

/* Liste mit Auswahlbalken und Rollbalken. fn zeichnet eine Zeile in das
 * Rechteck (x, y, w, CF_UI_ROW_H); top wird so nachgefuehrt, dass sel
 * sichtbar bleibt. Rueckgabe: Zeilen je Seite. */
typedef void (*cf_ui_rowfn)(void *ctx, size_t idx, int x, int y, int w,
                            int selected);
int  cf_ui_list(int x, int y, int w, int h, size_t count, size_t sel,
                size_t *top, cf_ui_rowfn fn, void *ctx);

/* Zeilen je Seite fuer ein Listenrechteck dieser Hoehe. */
int  cf_ui_list_rows(int h);

/* Blockierende Meldung, wartet auf A oder B. */
void cf_ui_message(const char *title, const char *text);

/* 1 bei A, 0 bei B. */
int  cf_ui_confirm(const char *title, const char *text);

/* Bildschirmtastatur. 1 wenn uebernommen, 0 bei Abbruch.
 * url_mode legt eine Belegung mit . / : - auf die erste Ebene. */
int  cf_ui_keyboard(const char *title, char *buf, size_t bufsz, int url_mode);

#endif
