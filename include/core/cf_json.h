/* Minimaler JSON-Leser.
 *
 * Arbeitet ausschliesslich auf einem Puffer, den der Aufrufer haelt, und
 * belegt selbst keinen Speicher. Das ist Absicht: die Metadaten einer
 * grossen Quelle sind einige hundert Kilobyte, und bei 24 MB Hauptspeicher
 * soll davon nicht noch eine Baumstruktur daneben liegen. Die Eintraege im
 * Katalog zeigen spaeter direkt in diesen Puffer.
 *
 * Es wird nicht validiert. Bei kaputtem JSON liefern die Funktionen NULL
 * oder 0, statt einen Fehler zu melden.
 */
#ifndef CF_JSON_H
#define CF_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "core/cf_str.h"

const char *cf_json_skip_ws(const char *p, const char *end);

/* Ueberspringt einen vollstaendigen Wert (auch verschachtelte Objekte und
 * Arrays). Gibt den Zeiger dahinter zurueck, NULL bei Fehler. */
const char *cf_json_skip_value(const char *p, const char *end);

/* --- Objekte ---
 * begin erwartet ein '{' und liefert den Zeiger auf das erste Mitglied.
 * next liefert 1 solange es ein Mitglied gab und setzt key und val. */
const char *cf_json_obj_begin(const char *p, const char *end);
int cf_json_obj_next(const char **p, const char *end,
                     cf_span *key, const char **val);

/* --- Arrays --- */
const char *cf_json_arr_begin(const char *p, const char *end);
int cf_json_arr_next(const char **p, const char *end, const char **val);

/* Sucht ein Mitglied im Objekt, das bei obj beginnt. NULL wenn nicht da. */
const char *cf_json_member(const char *obj, const char *end, const char *key);

/* Liest einen String-Wert und loest dabei \" \ \/ \n \r \t \b \f und
 * \uXXXX auf (letzteres nach UTF-8). */
size_t cf_json_str(const char *val, const char *end, char *dst, size_t dstsz);

/* Liest eine Zahl. Akzeptiert auch eine in Anfuehrungszeichen, weil
 * manche Server Groessen als String liefern ("1459978240"). */
int cf_json_u64(const char *val, const char *end, uint64_t *out);

#endif
