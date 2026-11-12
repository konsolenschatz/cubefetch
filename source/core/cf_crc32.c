#include "core/cf_crc32.h"

/* Tabelle wird beim ersten Aufruf gerechnet. 1 KB statt 1 KB Konstanten
 * im DOL, und es gibt nichts, was beim Abschreiben schiefgehen kann. */
static uint32_t g_tab[256];
static int      g_ready;

static void build(void)
{
	unsigned i, k;

	for (i = 0; i < 256; i++) {
		uint32_t c = (uint32_t)i;

		for (k = 0; k < 8; k++)
			c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		g_tab[i] = c;
	}
	g_ready = 1;
}

uint32_t cf_crc32(uint32_t crc, const void *buf, size_t len)
{
	const unsigned char *p = buf;

	if (!g_ready)
		build();

	/* Ohne Puffer ist das der Startwert, genau wie bei zlib. */
	if (!p)
		return 0;

	crc = ~crc;
	while (len--)
		crc = g_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return ~crc;
}

/* ------------------------------------------------------------------ */
/* Zusammensetzen                                                     */
/* ------------------------------------------------------------------ */

/* Eine Matrix ueber GF(2) ist hier ein Feld aus 32 Spalten; jede Spalte
 * ist das Bild eines Einheitsvektors. Multiplikation ist XOR der Spalten,
 * die im Vektor gesetzt sind. */
#define GF2_DIM 32

static uint32_t gf2_times(const uint32_t *mat, uint32_t vec)
{
	uint32_t sum = 0;
	int n = 0;

	while (vec) {
		if (vec & 1)
			sum ^= mat[n];
		vec >>= 1;
		n++;
	}
	return sum;
}

static void gf2_square(uint32_t *sq, const uint32_t *mat)
{
	int n;

	for (n = 0; n < GF2_DIM; n++)
		sq[n] = gf2_times(mat, mat[n]);
}

uint32_t cf_crc32_combine(uint32_t crc1, uint32_t crc2, uint64_t len2)
{
	uint32_t even[GF2_DIM];
	uint32_t odd[GF2_DIM];
	uint32_t row;
	int n;

	if (len2 == 0)
		return crc1;

	/* odd beschreibt zunaechst ein einzelnes Nullbit. */
	odd[0] = 0xEDB88320u;
	row = 1;
	for (n = 1; n < GF2_DIM; n++) {
		odd[n] = row;
		row <<= 1;
	}

	gf2_square(even, odd);    /* zwei Bits */
	gf2_square(odd, even);    /* vier Bits */

	/* Ab hier die Binaerdarstellung von len2 abarbeiten: quadrieren
	 * verdoppelt die Zahl der beschriebenen Bytes, und wo ein Bit
	 * gesetzt ist, wird angewandt. */
	do {
		gf2_square(even, odd);
		if (len2 & 1)
			crc1 = gf2_times(even, crc1);
		len2 >>= 1;
		if (len2 == 0)
			break;

		gf2_square(odd, even);
		if (len2 & 1)
			crc1 = gf2_times(odd, crc1);
		len2 >>= 1;
	} while (len2);

	return crc1 ^ crc2;
}
