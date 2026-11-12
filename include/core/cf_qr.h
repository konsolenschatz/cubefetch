/* QR-Code-Erzeugung, Byte-Modus, Fehlerkorrektur M, Versionen 1 bis 10.
 *
 * Fuer die Community-Seite: Links lassen sich auf dem GameCube nicht
 * anklicken, ein QR-Code laesst sich mit dem Telefon vom Fernseher
 * abfotografieren. Versionen 1 bis 10 reichen fuer bis zu 213 Bytes;
 * die Adressen hier haben unter 50.
 *
 * Aufbau nach ISO/IEC 18004: Daten in Codewoerter, Reed-Solomon ueber
 * GF(256) mit dem Polynom 0x11D, Bloecke verschraenkt, Funktionsmuster
 * (Finder, Timing, Ausrichtung, Format- und Versionsinformation), Daten
 * im Zickzack von rechts unten, Maske nach der geringsten Strafpunktzahl.
 *
 * Portables C ohne libogc, am PC getestet.
 */
#ifndef CF_QR_H
#define CF_QR_H

#include <stddef.h>
#include <stdint.h>

#define CF_QR_MAX_VERSION 10
#define CF_QR_MAX_SIZE    (CF_QR_MAX_VERSION * 4 + 17)   /* 57 */

typedef struct {
	int     version;   /* 1..10 */
	int     size;      /* Module je Seite, 4 * version + 17 */
	int     mask;      /* gewaehlte Maske 0..7 */
	uint8_t m[CF_QR_MAX_SIZE][CF_QR_MAX_SIZE];   /* 1 = dunkel */
} cf_qr;

/* Kodiert data (len Bytes). Waehlt die kleinste Version, in die es
 * passt. 1 bei Erfolg, 0 wenn es nicht passt. */
int cf_qr_encode(cf_qr *qr, const void *data, size_t len);

/* Wie oben, aber mit fester Maske (0..7) statt der besten. Fuer Tests. */
int cf_qr_encode_mask(cf_qr *qr, const void *data, size_t len, int mask);

/* Interna, fuer Tests freigelegt. */

/* Reed-Solomon-Pruefwoerter: ecc bekommt n Bytes. */
void cf_qr_rs(const uint8_t *data, size_t len, uint8_t *ecc, int n);

/* Die 15 Formatbits fuer Fehlerkorrektur M und diese Maske. */
uint32_t cf_qr_format_bits(int mask);

#endif
