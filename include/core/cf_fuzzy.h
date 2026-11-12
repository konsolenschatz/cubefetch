/* Namensaehnlichkeit fuer den Rueckfallweg aus docs/METADATA.md.
 *
 * Dateinamen in Sammlungen sehen so aus: "007 - Nightfire (Germany).iso".
 * Die Datenbank kennt "James Bond 007: Nightfire". Verglichen werden
 * darum nicht die Zeichenketten, sondern die Mengen ihrer Woerter nach
 * einer Normalisierung: Kleinschreibung, Umlaute zu ASCII, alles in
 * runden und eckigen Klammern weg (Region, Fassung, Disc), Satzzeichen
 * weg, ", The" am Ende nach vorn.
 *
 * Mass ist der Dice-Koeffizient der Wortmengen, 2|A n B| / (|A| + |B|).
 * Schwellen: ab 0,85 wird automatisch zugeordnet, von 0,70 bis 0,85 als
 * unsicher gekennzeichnet, darunter verworfen.
 */
#ifndef CF_FUZZY_H
#define CF_FUZZY_H

#include <stddef.h>

#define CF_FUZZY_AUTO    0.85
#define CF_FUZZY_SUGGEST 0.70

/* Normalisierte Form, Woerter durch je ein Leerzeichen getrennt. */
size_t cf_fuzzy_normalize(char *dst, size_t dstsz, const char *title);

/* Aehnlichkeit zweier Titel, 0..1. */
double cf_fuzzy_score(const char *a, const char *b);

/* Dasselbe fuer bereits normalisierte Titel. Die Normalisierung ist der
 * teure Teil (UTF-8, Klammern); wer einen Titel gegen tausende
 * vergleicht, normalisiert die tausend einmal vorab. */
double cf_fuzzy_score_norm(const char *na, const char *nb);

#endif
