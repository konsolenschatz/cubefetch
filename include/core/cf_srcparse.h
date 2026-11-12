/* Antworten einer Quelle in Katalogeintraege verwandeln.
 *
 * Reiner Kern: bekommt den schon geladenen Text und ruft fuer jeden
 * gefundenen Eintrag add(). Das Holen uebernimmt die Plattformschicht.
 * Dadurch lassen sich die Parser gegen gespeicherte echte Antworten
 * pruefen, ohne Netzwerk und ohne Konsole.
 *
 * Format der Quellen: docs/SOURCE-FORMAT.md
 */
#ifndef CF_SRCPARSE_H
#define CF_SRCPARSE_H

#include <stddef.h>

#include "core/cf_entry.h"

/* 0 weitermachen, negativ bricht ab (z. B. Speicher voll). */
typedef int (*cf_add_fn)(void *ctx, const cf_entry *e);

/* CubeFetch-eigenes JSON. */
size_t cf_parse_manifest(const char *json, size_t len, unsigned src,
                         char *base_out, size_t base_sz,
                         cf_add_fn add, void *ctx);

/* Verzeichnislisting von Apache, nginx, lighttpd, Caddy. Heuristik:
 * liest href-Werte und verwirft Unterordner und Sortierlinks. */
size_t cf_parse_autoidx(const char *html, size_t len, unsigned src,
                        cf_add_fn add, void *ctx);

/* Haengt den URL-kodierten Pfad an die Basisadresse. Schraegstriche im
 * Pfad bleiben Schraegstriche. Gibt die benoetigte Laenge zurueck. */
size_t cf_join_url(char *dst, size_t dstsz, const char *base,
                   const char *path);

#endif
