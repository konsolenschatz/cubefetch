/* CRC32 wie in zlib, ohne zlib.
 *
 * Der Wechsel auf libogc2 (fuer die Netzwerkadapter im Memory-Card-Slot)
 * haette sonst auch ppc-zlib gegen die libogc2-Variante austauschen
 * muessen, weil die portlibs der beiden Bibliotheken nicht
 * ABI-kompatibel sind. Das haette jedes andere libogc-Projekt auf dem
 * Rechner mitbetroffen. zlib wurde hier aber nur fuer diese eine
 * Funktion gebraucht, darum steht sie jetzt selbst da.
 *
 * Es ist der verbreitete CRC-32/ISO-HDLC (wie zlib):
 * Polynom 0xEDB88320 gespiegelt, Start 0xFFFFFFFF, Ergebnis invertiert.
 * Die Aufrufform ist bewusst die von zlib, inklusive
 * cf_crc32(0, NULL, 0) == 0 als Startwert.
 */
#ifndef CF_CRC32_H
#define CF_CRC32_H

#include <stddef.h>
#include <stdint.h>

uint32_t cf_crc32(uint32_t crc, const void *buf, size_t len);

/* Setzt zwei Pruefsummen zusammen, als waere der zweite Block direkt
 * hinter dem ersten gelesen worden. len2 ist die Laenge des zweiten
 * Blocks in Bytes.
 *
 * Gebraucht wird das, weil der Download die Datei in Bereiche aufteilt
 * und diese gleichzeitig laedt: jeder Bereich fuehrt seine eigene
 * Pruefsumme mit, am Ende werden sie der Reihe nach zusammengesetzt. Die
 * Alternative waere, die fertige Datei noch einmal komplett von der Karte
 * zu lesen - 1,36 GB, die laenger brauchen als der Download selbst.
 *
 * Dasselbe Verfahren wie crc32_combine aus zlib: der Zustand wird als
 * Vektor ueber GF(2) betrachtet und mit der Matrix multipliziert, die
 * len2 Nullbytes beschreibt. Die Matrix entsteht durch wiederholtes
 * Quadrieren, der Aufwand ist also logarithmisch in len2. */
uint32_t cf_crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t len2);

#endif
