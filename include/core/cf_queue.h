/* Warteschlange.
 *
 * Wird auf dem Zielgeraet abgelegt und beim Start wieder eingelesen. Bei
 * knapp einer halben Stunde pro Image ist das kein Komfort, sondern
 * Voraussetzung - siehe docs/ARCHITECTURE.md.
 */
#ifndef CF_QUEUE_H
#define CF_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#include "core/cf_entry.h"

#define CF_QUEUE_MAX 32
#define CF_URL_MAX   1024

typedef enum {
	CF_Q_WAIT = 0,
	CF_Q_DONE,
	CF_Q_FAIL
} cf_qstate;

typedef struct {
	char     url[CF_URL_MAX];
	char     filename[CF_MAX_PATH];   /* nur der Dateiname, FAT-tauglich */
	char     title[CF_MAX_TITLE];
	uint64_t size;
	uint32_t crc32;
	uint8_t  has_crc;
	uint8_t  state;
	int      err;                     /* letzter Fehlercode bei CF_Q_FAIL */
} cf_qitem;

typedef struct {
	cf_qitem v[CF_QUEUE_MAX];
	size_t   n;
} cf_queue;

int    cf_queue_add(cf_queue *q, const cf_qitem *it);
int    cf_queue_remove(cf_queue *q, size_t idx);

/* Eintrag von Platz from nach Platz to verschieben; die dazwischen ruecken
 * nach. Damit legt man fest, was als Naechstes geladen wird. */
int    cf_queue_move(cf_queue *q, size_t from, size_t to);

/* Platz des Eintrags mit dieser Adresse, oder CF_QUEUE_MAX. */
size_t cf_queue_find(const cf_queue *q, const char *url);
void   cf_queue_clear_finished(cf_queue *q);

/* 1 wenn diese Adresse schon in der Schlange steht. */
int    cf_queue_contains(const cf_queue *q, const char *url);

/* Index des naechsten wartenden Eintrags, oder CF_QUEUE_MAX. */
size_t cf_queue_next_waiting(const cf_queue *q);

size_t   cf_queue_count_waiting(const cf_queue *q);
uint64_t cf_queue_bytes_waiting(const cf_queue *q);

/* Dateiformat fuer die Ablage. */
size_t cf_queue_serialize(const cf_queue *q, char *dst, size_t dstsz);
size_t cf_queue_parse(cf_queue *q, const char *text);

/* Baut einen Eintrag aus einem Katalogeintrag und der fertigen Adresse. */
void cf_qitem_from_entry(cf_qitem *it, const cf_entry *e, const char *url);

#endif
