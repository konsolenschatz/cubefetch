/* Zeichnen mit GX in zwei Dimensionen.
 *
 * Alles, was auf den Bildschirm kommt, geht hier durch: gefuellte
 * Rechtecke, Texturen (Cover) und Text aus einem Schriftatlas, den
 * FreeType beim Start aus der eingebetteten DejaVu Sans rendert. Damit
 * gibt es Umlaute, Proportionalschrift und Farben - die Konsolenschrift
 * von libogc konnte nur ASCII in einer Groesse.
 *
 * Koordinaten sind Bildpunkte, Ursprung oben links, 640 x 480 unabhaengig
 * vom Videomodus. Farben sind 0xRRGGBBAA.
 *
 * Doppelt gepuffert: gezeichnet wird in den Bildpuffer, der gerade nicht
 * angezeigt wird; cf_gfx_end wechselt. Das Flackern der Konsolenfassung
 * kann hier darum nicht auftreten.
 */
#ifndef CF_GFX_INCLUDED
#define CF_GFX_INCLUDED

#include <stddef.h>
#include <stdint.h>

#include <gccore.h>

#define CF_GFX_W 640
#define CF_GFX_H 480

typedef struct {
	GXTexObj obj;
	void    *data;      /* 32-Byte-ausgerichtet, im Kachelformat */
	int      w, h;
} cf_tex;

typedef struct cf_font cf_font;

int  cf_gfx_init(void);

/* Ein Bild: begin loescht mit der Hintergrundfarbe, end zeigt es an und
 * wartet auf den Bildwechsel. */
void cf_gfx_begin(uint32_t bg);
void cf_gfx_end(void);

void cf_gfx_rect(int x, int y, int w, int h, uint32_t rgba);
void cf_gfx_frame(int x, int y, int w, int h, int thick, uint32_t rgba);

/* Alles ausserhalb wird abgeschnitten, bis cf_gfx_noclip. */
void cf_gfx_clip(int x, int y, int w, int h);
void cf_gfx_noclip(void);

/* Innerer Ausschnitt: schneidet mit dem aktuellen und stellt ihn mit
 * pop wieder her. Fuer Lauftext in einer Listenzeile, die selbst schon
 * auf die Liste beschnitten ist. */
void cf_gfx_clip_push(int x, int y, int w, int h);
void cf_gfx_clip_pop(void);

/* Textur aus RGBA8-Bildpunkten (zeilenweise, oben links zuerst). Die
 * Daten werden ins Kachelformat des Grafikchips umgesetzt. 0 bei
 * Speichermangel. */
int  cf_gfx_tex_rgba8(cf_tex *t, int w, int h, const uint8_t *rgba);
void cf_gfx_tex_free(cf_tex *t);
void cf_gfx_draw_tex(const cf_tex *t, int x, int y, int w, int h,
                     uint32_t tint);

/* Schrift in Bildpunkten Hoehe. Rendert beim Laden alle Zeichen von
 * Latin-1 und einige typographische in einen Atlas. */
cf_font *cf_font_load(const void *ttf, size_t len, int px);

/* Zeichensatz fuer kuenftig geladene Schriften: der feste Grundbestand
 * plus jedes Zeichen in text (z. B. die Sprachdatei). */
void     cf_gfx_charset(const char *text);

/* Zweitschrift fuer Zeichen, die der Hauptschrift fehlen; NULL schaltet
 * sie ab. Der Speicher muss bestehen bleiben. */
void     cf_gfx_set_fallback(const void *ttf, size_t len);
void     cf_font_free(cf_font *f);
int      cf_font_height(const cf_font *f);    /* Zeilenabstand */
int      cf_font_ascent(const cf_font *f);

/* Text ist UTF-8. Rueckgabe ist die Breite in Bildpunkten. */
int  cf_gfx_text(const cf_font *f, int x, int y, uint32_t rgba,
                 const char *utf8);
int  cf_gfx_text_width(const cf_font *f, const char *utf8);

/* Auf maxw kuerzen, mit "..." am Ende. */
int  cf_gfx_text_fit(const cf_font *f, int x, int y, int maxw,
                     uint32_t rgba, const char *utf8);

#endif
