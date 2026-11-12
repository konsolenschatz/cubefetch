/* Katalog: haelt alle Eintraege aller Quellen und bietet darauf eine
 * gefilterte, sortierte Ansicht.
 *
 * Die Ansicht ist ein Index-Array, nicht eine Kopie der Eintraege. Damit
 * kostet Sortieren nur das Umstellen von 4-Byte-Indizes statt von
 * 350-Byte-Strukturen.
 */
#ifndef CF_CATALOG_H
#define CF_CATALOG_H

#include <stddef.h>

#include "core/cf_entry.h"

typedef enum {
	CF_SORT_NAME_ASC = 0,
	CF_SORT_NAME_DESC,
	CF_SORT_SIZE_ASC,
	CF_SORT_SIZE_DESC,
	CF_SORT_YEAR_ASC,      /* unbekanntes Jahr immer zuletzt */
	CF_SORT_YEAR_DESC,
	CF_SORT_COUNT
} cf_sort;

/* Schluessel fuer cf_t(), passend zu den Sprachdateien */
const char *cf_sort_key(cf_sort s);

typedef struct cf_catalog cf_catalog;

cf_catalog *cf_catalog_new(void);
void        cf_catalog_free(cf_catalog *c);

/* Kopiert den Eintrag in den Katalog. 0 bei Speichermangel. */
int    cf_catalog_add(cf_catalog *c, const cf_entry *e);
size_t cf_catalog_total(const cf_catalog *c);
void   cf_catalog_clear(cf_catalog *c);

/* Eintraege einer Quelle wieder entfernen (beim Neuladen). */
void cf_catalog_drop_source(cf_catalog *c, unsigned src);

/* --- Ansicht --- */
void cf_catalog_set_search(cf_catalog *c, const char *needle);
void cf_catalog_set_sort(cf_catalog *c, cf_sort s);
void cf_catalog_set_images_only(cf_catalog *c, int on);

/* Nur Eintraege, deren Genre-Liste dieses Genre enthaelt. NULL oder ""
 * hebt den Filter auf. */
void cf_catalog_set_genre(cf_catalog *c, const char *genre);
const char *cf_catalog_genre(const cf_catalog *c);

/* Alle Genres, die im Katalog vorkommen, alphabetisch, ohne Doppelte.
 * Rueckgabe: Anzahl. */
size_t cf_catalog_genres(const cf_catalog *c, char (*out)[24], size_t max);

/* Ruft fn fuer jeden Eintrag, zum Nachtragen von Jahr und Genre. */
void cf_catalog_each(cf_catalog *c, void (*fn)(void *ctx, cf_entry *e),
                     void *ctx);
cf_sort cf_catalog_sort(const cf_catalog *c);
int  cf_catalog_images_only(const cf_catalog *c);
const char *cf_catalog_search(const cf_catalog *c);

/* Baut die Ansicht neu auf. Muss nach Aenderungen gerufen werden. */
void cf_catalog_refresh(cf_catalog *c);

size_t          cf_view_count(const cf_catalog *c);
cf_entry       *cf_view_at(cf_catalog *c, size_t i);

#endif
