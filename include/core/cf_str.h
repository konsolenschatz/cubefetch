/* Zeichenketten-Hilfen. Portabel, ohne libogc. */
#ifndef CF_STR_H
#define CF_STR_H

#include <stddef.h>
#include <stdint.h>

/* Ein Ausschnitt in einem fremden Puffer. Nicht nullterminiert. */
typedef struct {
	const char *p;
	size_t len;
} cf_span;

#define CF_SPAN_NULL ((cf_span){ NULL, 0 })

static inline int cf_span_valid(cf_span s) { return s.p != NULL; }

/* Kopiert span nach dst (nullterminiert). Gibt die geschriebene Laenge
 * ohne Nullbyte zurueck, schneidet bei Platzmangel ab. */
size_t cf_span_copy(char *dst, size_t dstsz, cf_span s);

int cf_span_eq_cstr(cf_span s, const char *cstr);

/* Kleinbuchstaben nur fuer ASCII. Reicht fuer Suche und Sortierung. */
char cf_lower(char c);

/* Teilstring-Suche ohne Beachtung der Gross-/Kleinschreibung.
 * Vergleicht nach cf_fold_char, Umlaute zaehlen also als Grundbuchstabe. */
int cf_find_nocase(const char *haystack, const char *needle);

/* Vergleich fuer die Sortierung: ASCII-case-insensitiv, Umlaute auf den
 * Grundbuchstaben abgebildet, damit "Ärger" bei A einsortiert wird. */
int cf_cmp_title(const char *a, const char *b);

/* UTF-8 nach ASCII fuer die libogc-Konsole, die nur 7 Bit kann.
 * ae/oe/ue/ss fuer die deutschen Umlaute, Akzente auf den Grundbuchstaben,
 * alles Unbekannte auf '?'. Gibt die geschriebene Laenge zurueck. */
size_t cf_utf8_to_ascii(char *dst, size_t dstsz, const char *src);

/* URL-Kodierung. Schraegstriche bleiben erhalten, weil Dateinamen bei
 * Pfadanteile enthalten koennen. */
size_t cf_url_encode_path(char *dst, size_t dstsz, const char *src);

/* "1.459.978.240" -> lesbar als "1.4 GB" */
void cf_fmt_size(char *dst, size_t dstsz, uint64_t bytes);
void cf_fmt_rate(char *dst, size_t dstsz, uint64_t bytes_per_sec);
void cf_fmt_duration(char *dst, size_t dstsz, uint32_t seconds);

/* Macht einen Dateinamen aus dem Netz FAT-tauglich: ersetzt die auf
 * FAT verbotenen Zeichen durch '_' und entfernt Punkte und Leerzeichen am
 * Ende, die Windows und FAT nicht moegen. Arbeitet an Ort und Stelle. */
void cf_fat_sanitize(char *s);

#endif
