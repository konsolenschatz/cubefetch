/* Speichergeraete erkennen und beschreiben.
 *
 * Es werden die Geraete unterstuetzt, fuer die libogc einen EXI-Treiber
 * mitbringt: SD Gecko in beiden Memory-Card-Slots und SD2SP2 an Serial
 * Port 2. IDE-EXI und M.2-Adapter brauchen einen eigenen Treiber und
 * stehen in docs/ARCHITECTURE.md unter Phase 5.
 */
#ifndef CF_STORAGE_H
#define CF_STORAGE_H

#include <stddef.h>
#include <stdint.h>

#define CF_MAX_DEVICES 3

typedef struct {
	char     mount[8];     /* "sd2" */
	char     root[12];     /* "sd2:/" */
	char     label[32];    /* "SD2SP2" */
	/* Ein Datentraegername waere huebsch, aber fatGetVolumeLabel gibt
	 * es in dieser libfat-Version nicht. */
	int      exi_channel;  /* fuer die Konfliktwarnung */
	int      mounted;
	uint64_t total;
	uint64_t avail;
} cf_device;

/* Haengt alles ein, was sich einhaengen laesst. Gibt die Anzahl der
 * eingehaengten Geraete zurueck. */
size_t cf_storage_scan(void);

size_t           cf_storage_count(void);
const cf_device *cf_storage_at(size_t i);

/* Liest total und avail neu ein. */
void cf_storage_refresh(size_t i);

/* Erzeugt den Pfad samt aller Elternverzeichnisse. 1 bei Erfolg oder
 * wenn er schon da war. */
int cf_mkdir_p(const char *path);

/* Groesse einer Datei, 0 wenn sie nicht existiert. */
uint64_t cf_file_size(const char *path);

int cf_file_exists(const char *path);

/* Liest eine Textdatei vollstaendig. Gibt einen mit malloc belegten,
 * nullterminierten Puffer zurueck oder NULL. */
char *cf_read_text(const char *path, size_t *len_out);

int cf_write_text(const char *path, const char *text);

#endif
