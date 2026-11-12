/* Ein Katalogeintrag: eine herunterladbare Datei aus einer Quelle. */
#ifndef CF_ENTRY_H
#define CF_ENTRY_H

#include <stdint.h>

#define CF_MAX_PATH   192   /* Pfad relativ zur Quelle, roh (nicht kodiert) */
#define CF_MAX_TITLE  128   /* Anzeigename, aus dem Dateinamen abgeleitet */

/* Woher die Disc-ID stammt. Siehe docs/METADATA.md. */
typedef enum {
	CF_ID_NONE = 0,   /* nicht ermittelt */
	CF_ID_HEADER,     /* aus dem Disc-Header gelesen, exakt */
	CF_ID_MANIFEST,   /* von der Quelle mitgeliefert */
	CF_ID_GUESS       /* ueber Namensaehnlichkeit geraten, unsicher */
} cf_id_origin;

typedef struct {
	char     path[CF_MAX_PATH];
	char     title[CF_MAX_TITLE];
	uint64_t size;          /* 0 = unbekannt */
	uint32_t crc32;
	uint8_t  has_crc;
	char     id[8];         /* "GW7P69", leer wenn unbekannt */
	uint8_t  id_origin;     /* cf_id_origin */
	uint8_t  src;           /* Index in cf_sources */
	uint8_t  is_image;      /* Dateiendung sieht nach Disc-Image aus */

	/* Aus GameTDB nachgetragen (cf_catalog_each), fuer Sortierung und
	 * Filter. Leer, solange keine Datenbank geladen ist. */
	uint16_t year;          /* 0 = unbekannt */
	uint8_t  meta_guess;    /* 1 = ueber den Namen zugeordnet */
	uint8_t  meta_score;    /* Aehnlichkeit in Prozent, bei meta_guess */
	char     meta_id[8];    /* zugeordnete GameTDB-ID, leer = keine */
	char     genre[64];     /* kommagetrennt, wie in GameTDB; so gross wie dort */
} cf_entry;

/* Leitet title aus path ab: Verzeichnisanteil und Endung entfernen. */
void cf_entry_title_from_path(cf_entry *e);

/* Setzt is_image anhand der Endung (.iso, .gcm, .rvz, .ciso). */
void cf_entry_mark_image(cf_entry *e);

#endif
