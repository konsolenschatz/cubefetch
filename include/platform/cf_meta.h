/* Spielinformationen und Cover von GameTDB auf der Konsole.
 *
 * Der Kern (core/cf_gametdb.h) kennt nur Text; dieses Modul holt die
 * Daten, legt sie auf der Karte ab und macht aus PNG-Dateien Texturen.
 *
 *   <cfgdir>gametdb/games-<LANG>.tsv   eine Zeile je GameCube-Spiel
 *   <cfgdir>covers/<ID>.png             Packungsbild, wie geholt
 *
 * Die Datenbank kommt als wiitdb.zip (2 MB) von www.gametdb.com, wird im
 * Strom entpackt (zlib) und beim Entpacken gleich geparst - die 15 MB
 * XML beruehren die Karte nie. Cover kommen von art.gametdb.com; der
 * Ordner dort ist ein Sprachkuerzel, nicht der Regionsbuchstabe der ID,
 * darum wird eine Kette probiert (docs/METADATA.md).
 *
 * Alles hier blockiert. Cover darum nur holen, wenn der Nutzer auf einem
 * Eintrag verweilt, nicht beim Durchrollen.
 */
#ifndef CF_META_H
#define CF_META_H

#include <stddef.h>
#include <stdint.h>

#include "core/cf_gametdb.h"
#include "platform/cf_gfx.h"

typedef void (*cf_meta_cb)(void *ctx, const char *stage,
                           uint64_t done, uint64_t total);

/* Laedt den Index von der Karte, wenn er da ist. lang ist "DE"/"EN". */
int    cf_meta_init(const char *cfgdir, const char *lang);
void   cf_meta_free(void);
size_t cf_meta_count(void);

/* Datenbank von GameTDB holen und umsetzen. Blockiert minutenlang;
 * cb bekommt Zwischenstaende. 0 bei Erfolg, sonst ein Fehlercode aus
 * cf_http.h oder einer von CF_META_ hier. */
int cf_meta_update(cf_meta_cb cb, void *ctx, volatile int *cancel);

#define CF_META_EZIP   (-40)   /* Archiv nicht wie erwartet */
#define CF_META_EFILE  (-41)   /* Karte: Datei nicht schreibbar */
#define CF_META_EPNG   (-42)   /* Bild nicht dekodierbar */
#define CF_META_ENONE  (-43)   /* kein Cover unter keiner Sprache */
#define CF_META_EEMPTY (-44)   /* Archiv gelesen, aber kein GameCube-Spiel */

const cf_gtdb_game *cf_meta_lookup(const char *id);
const cf_gtdb_game *cf_meta_guess(const char *title, double *score);

/* Wie weit cf_meta_cover gehen darf. Schon die Karte kostet spuerbar:
 * 70 KB lesen und ein PNG dekodieren sind 100 bis 150 ms. Beim Rollen
 * durch die Liste darum nur aus dem Speicher. */
#define CF_COVER_MEM   0   /* nur bereits dekodierte */
#define CF_COVER_CARD  1   /* auch von der Karte */
#define CF_COVER_NET   2   /* auch vom Netz */

/* Cover zu einer ID, hoechstens bis zur Stufe level. NULL wenn nicht
 * vorhanden; fehlende Cover werden fuer diese Sitzung gemerkt und nicht
 * erneut angefragt. */
const cf_tex *cf_meta_cover(const char *id, int level);

/* 1 wenn fuer diese ID in dieser Sitzung schon vergeblich gefragt wurde. */
int cf_meta_cover_missing(const char *id);

#endif
