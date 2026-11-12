/* GameTDB: Spielinformationen zu einer Disc-ID.
 *
 * Quelle ist wiitdb.xml aus http://www.gametdb.com/wiitdb.zip - GameCube-
 * Titel stehen dort mit <type>GameCube</type>. Geparst wird als Strom in
 * Stuecken, damit die 15 MB XML nie ganz im Speicher liegen muessen;
 * heraus kommt je Spiel ein kompakter Datensatz, der als eine Zeile in
 * einer Textdatei auf der Karte landet und beim Start in einen nach ID
 * sortierten Index geladen wird. Hintergrund in docs/METADATA.md.
 *
 * Portables C ohne libogc, am PC testbar.
 */
#ifndef CF_GAMETDB_H
#define CF_GAMETDB_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	char     id[8];          /* "GALE01" */
	char     title[96];      /* in der gewuenschten Sprache, sonst EN */
	char     publisher[48];
	char     developer[48];
	char     genre[64];      /* wie in der Datenbank, kommagetrennt */
	uint16_t year;           /* 0 = unbekannt */
	uint8_t  month, day;
	uint8_t  players;
} cf_gtdb_game;

/* --- XML-Strom ---------------------------------------------------- */

typedef int (*cf_gtdb_game_cb)(void *ctx, const cf_gtdb_game *g);

typedef struct cf_gtdb_xml cf_gtdb_xml;

/* lang ist das Sprachkuerzel fuer den Titel, z. B. "DE"; faellt auf "EN"
 * zurueck, wenn es den Titel in der Sprache nicht gibt. */
cf_gtdb_xml *cf_gtdb_xml_new(cf_gtdb_game_cb cb, void *ctx, const char *lang);

/* Daten in beliebigen Stuecken. Ruft cb je vollstaendigem GameCube-Spiel.
 * Rueckgabe negativ, wenn cb abgebrochen hat. */
int  cf_gtdb_xml_feed(cf_gtdb_xml *x, const char *data, size_t len);
void cf_gtdb_xml_free(cf_gtdb_xml *x);

/* Wie viele Spiele bisher gemeldet wurden. */
size_t cf_gtdb_xml_count(const cf_gtdb_xml *x);

/* --- Zeilenformat fuer die Karte ---------------------------------- */

/* "ID\tJahr\tMonat\tTag\tSpieler\tHerausgeber\tEntwickler\tGenre\tTitel" */
int cf_gtdb_line(const cf_gtdb_game *g, char *dst, size_t dstsz);
int cf_gtdb_parse_line(const char *line, cf_gtdb_game *g);

/* --- Index -------------------------------------------------------- */

typedef struct {
	cf_gtdb_game *v;
	size_t        n, cap;
	int           sorted;
	char        (*norm)[96];   /* normalisierte Titel, parallel zu v */
} cf_gtdb_index;

int  cf_gtdb_index_add(cf_gtdb_index *ix, const cf_gtdb_game *g);
void cf_gtdb_index_sort(cf_gtdb_index *ix);
const cf_gtdb_game *cf_gtdb_index_find(const cf_gtdb_index *ix, const char *id);
void cf_gtdb_index_free(cf_gtdb_index *ix);

/* Laedt eine Datei im Zeilenformat in den Index. Rueckgabe: Eintraege. */
size_t cf_gtdb_index_load(cf_gtdb_index *ix, const char *txt);

/* Bester Treffer ueber Namensaehnlichkeit (cf_fuzzy), fuer Eintraege ohne
 * Disc-ID. score bekommt die Aehnlichkeit 0..1. NULL wenn nichts ueber
 * der Schwelle liegt. */
const cf_gtdb_game *cf_gtdb_index_fuzzy(const cf_gtdb_index *ix,
                                        const char *title, double *score);

#endif
