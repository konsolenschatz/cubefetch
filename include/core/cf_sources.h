/* Quellen-Definition. Format in docs/SOURCE-FORMAT.md. */
#ifndef CF_SOURCES_H
#define CF_SOURCES_H

#include <stddef.h>

#define CF_MAX_SOURCES   8
#define CF_SRC_NAME_MAX  48
#define CF_SRC_URL_MAX   256

typedef enum {
	CF_SRC_AUTOIDX = 0, /* HTML-Verzeichnislisting */
	CF_SRC_MANIFEST    /* CubeFetch-JSON */
} cf_src_type;

typedef struct {
	cf_src_type type;
	char name[CF_SRC_NAME_MAX];
	char url[CF_SRC_URL_MAX];
	int  loaded;       /* Eintraege wurden schon geholt */
	int  entries;      /* wie viele es waren */
} cf_source;

typedef struct {
	cf_source v[CF_MAX_SOURCES];
	size_t    n;
} cf_sources;

const char *cf_src_type_name(cf_src_type t);

/* Raet den Typ anhand der Adresse, wenn das Feld leer gelassen wurde. */
cf_src_type cf_src_guess(const char *url);

/* Liest den Inhalt einer sources.txt. Gibt die Anzahl gelesener Quellen
 * zurueck. Vorhandene Eintraege werden ersetzt. */
size_t cf_sources_parse(cf_sources *s, const char *text);

/* Fuegt eine Quelle an. 0 wenn die Liste voll ist oder die URL leer. */
int cf_sources_add(cf_sources *s, cf_src_type t,
                   const char *name, const char *url);

int cf_sources_remove(cf_sources *s, size_t idx);

/* Schreibt die Liste im Dateiformat. Gibt die benoetigte Laenge zurueck
 * (auch wenn abgeschnitten wurde). */
size_t cf_sources_serialize(const cf_sources *s, char *dst, size_t dstsz);

#endif
