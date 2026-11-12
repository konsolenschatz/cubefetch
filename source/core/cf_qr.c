#include "core/cf_qr.h"

#include <stdlib.h>
#include <string.h>

/* Fehlerkorrektur M, Versionen 1..10 (Index 0 unbenutzt). Aus der Tabelle
 * der Norm: Pruefwoerter je Block und Zahl der Bloecke. */
static const int ECC_PER_BLOCK[11] = { 0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26 };
static const int NUM_BLOCKS[11]    = { 0,  1,  1,  1,  2,  2,  4,  4,  4,  5,  5 };

/* Formatbits fuer die Fehlerkorrekturstufe M sind 00. */
#define ECL_M_BITS 0

/* ------------------------------------------------------------------ */
/* Groessen                                                           */
/* ------------------------------------------------------------------ */

/* Module, die Daten tragen koennen: alles ausser Funktionsmustern. */
static int raw_data_modules(int ver)
{
	int r = (16 * ver + 128) * ver + 64;

	if (ver >= 2) {
		int na = ver / 7 + 2;
		r -= (25 * na - 10) * na - 55;
		if (ver >= 7)
			r -= 36;
	}
	return r;
}

static int data_codewords(int ver)
{
	return raw_data_modules(ver) / 8 - ECC_PER_BLOCK[ver] * NUM_BLOCKS[ver];
}

/* ------------------------------------------------------------------ */
/* Reed-Solomon ueber GF(256), Polynom x^8 + x^4 + x^3 + x^2 + 1       */
/* ------------------------------------------------------------------ */

static uint8_t gf_mul(uint8_t x, uint8_t y)
{
	int z = 0, i;

	for (i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	return (uint8_t)z;
}

static void rs_divisor(int n, uint8_t *div)
{
	uint8_t root = 1;
	int i, j;

	memset(div, 0, (size_t)n);
	div[n - 1] = 1;

	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			div[j] = gf_mul(div[j], root);
			if (j + 1 < n)
				div[j] ^= div[j + 1];
		}
		root = gf_mul(root, 0x02);
	}
}

void cf_qr_rs(const uint8_t *data, size_t len, uint8_t *ecc, int n)
{
	uint8_t div[64];
	size_t k;
	int i;

	rs_divisor(n, div);
	memset(ecc, 0, (size_t)n);

	for (k = 0; k < len; k++) {
		uint8_t factor = data[k] ^ ecc[0];

		memmove(ecc, ecc + 1, (size_t)(n - 1));
		ecc[n - 1] = 0;
		for (i = 0; i < n; i++)
			ecc[i] ^= gf_mul(div[i], factor);
	}
}

/* ------------------------------------------------------------------ */
/* Matrix                                                             */
/* ------------------------------------------------------------------ */

static uint8_t g_fn[CF_QR_MAX_SIZE][CF_QR_MAX_SIZE];   /* Funktionsmodul */

static void set_fn(cf_qr *q, int x, int y, int dark)
{
	q->m[y][x] = (uint8_t)(dark ? 1 : 0);
	g_fn[y][x] = 1;
}

static void draw_finder(cf_qr *q, int x, int y)
{
	int dx, dy;

	for (dy = -4; dy <= 4; dy++) {
		for (dx = -4; dx <= 4; dx++) {
			int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
			int dist = ax > ay ? ax : ay;
			int xx = x + dx, yy = y + dy;

			if (xx >= 0 && xx < q->size && yy >= 0 && yy < q->size)
				set_fn(q, xx, yy, dist != 2 && dist != 4);
		}
	}
}

static void draw_alignment(cf_qr *q, int x, int y)
{
	int dx, dy;

	for (dy = -2; dy <= 2; dy++)
		for (dx = -2; dx <= 2; dx++) {
			int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
			set_fn(q, x + dx, y + dy, (ax > ay ? ax : ay) != 1);
		}
}

static int align_positions(int ver, int size, int *out)
{
	int na, step, i;

	if (ver == 1)
		return 0;

	na = ver / 7 + 2;
	step = (ver * 8 + na * 3 + 5) / (na * 4 - 4) * 2;

	out[0] = 6;
	for (i = na - 1; i >= 1; i--)
		out[i] = size - 7 - (na - 1 - i) * step;
	return na;
}

uint32_t cf_qr_format_bits(int mask)
{
	uint32_t data = (uint32_t)(ECL_M_BITS << 3 | mask);
	uint32_t rem = data;
	int i;

	for (i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	return ((data << 10) | rem) ^ 0x5412;
}

static int bit(uint32_t v, int i)
{
	return (int)((v >> i) & 1);
}

static void draw_format(cf_qr *q, int mask)
{
	uint32_t b = cf_qr_format_bits(mask);
	int i, s = q->size;

	/* Erste Kopie, um den Finder oben links */
	for (i = 0; i <= 5; i++)
		set_fn(q, 8, i, bit(b, i));
	set_fn(q, 8, 7, bit(b, 6));
	set_fn(q, 8, 8, bit(b, 7));
	set_fn(q, 7, 8, bit(b, 8));
	for (i = 9; i < 15; i++)
		set_fn(q, 14 - i, 8, bit(b, i));

	/* Zweite Kopie, verteilt auf die anderen beiden Finder */
	for (i = 0; i < 8; i++)
		set_fn(q, s - 1 - i, 8, bit(b, i));
	for (i = 8; i < 15; i++)
		set_fn(q, 8, s - 15 + i, bit(b, i));
	set_fn(q, 8, s - 8, 1);   /* immer dunkles Modul */
}

static void draw_version(cf_qr *q)
{
	uint32_t rem = (uint32_t)q->version, b;
	int i;

	if (q->version < 7)
		return;

	for (i = 0; i < 12; i++)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	b = ((uint32_t)q->version << 12) | rem;

	for (i = 0; i < 18; i++) {
		int a = q->size - 11 + i % 3, c = i / 3;
		set_fn(q, a, c, bit(b, i));
		set_fn(q, c, a, bit(b, i));
	}
}

static void draw_function_patterns(cf_qr *q)
{
	int pos[8], na, i, j;

	memset(g_fn, 0, sizeof(g_fn));

	for (i = 0; i < q->size; i++) {
		set_fn(q, 6, i, i % 2 == 0);
		set_fn(q, i, 6, i % 2 == 0);
	}

	draw_finder(q, 3, 3);
	draw_finder(q, q->size - 4, 3);
	draw_finder(q, 3, q->size - 4);

	na = align_positions(q->version, q->size, pos);
	for (i = 0; i < na; i++)
		for (j = 0; j < na; j++) {
			/* Die drei Ecken mit Findern bleiben frei. */
			if ((i == 0 && j == 0) || (i == 0 && j == na - 1) ||
			    (i == na - 1 && j == 0))
				continue;
			draw_alignment(q, pos[i], pos[j]);
		}

	draw_format(q, 0);   /* Platz reservieren, Wert kommt spaeter */
	draw_version(q);
}

/* Daten im Zickzack: Spaltenpaare von rechts, abwechselnd hoch und
 * runter, die Timing-Spalte 6 wird uebersprungen. */
static void draw_codewords(cf_qr *q, const uint8_t *cw, int ncw)
{
	int i = 0, right, vert, j;

	for (right = q->size - 1; right >= 1; right -= 2) {
		if (right == 6)
			right = 5;
		for (vert = 0; vert < q->size; vert++) {
			for (j = 0; j < 2; j++) {
				int x = right - j;
				int up = ((right + 1) & 2) == 0;
				int y = up ? q->size - 1 - vert : vert;

				if (!g_fn[y][x] && i < ncw * 8) {
					q->m[y][x] = (uint8_t)((cw[i >> 3] >> (7 - (i & 7))) & 1);
					i++;
				}
			}
		}
	}
}

static int mask_bit(int mask, int x, int y)
{
	switch (mask) {
	case 0: return (x + y) % 2 == 0;
	case 1: return y % 2 == 0;
	case 2: return x % 3 == 0;
	case 3: return (x + y) % 3 == 0;
	case 4: return (x / 3 + y / 2) % 2 == 0;
	case 5: return x * y % 2 + x * y % 3 == 0;
	case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
	default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
	}
}

static void apply_mask(cf_qr *q, int mask)
{
	int x, y;

	for (y = 0; y < q->size; y++)
		for (x = 0; x < q->size; x++)
			if (!g_fn[y][x] && mask_bit(mask, x, y))
				q->m[y][x] ^= 1;
}

/* ------------------------------------------------------------------ */
/* Strafpunkte nach der Norm                                          */
/* ------------------------------------------------------------------ */

static int finder_like(const uint8_t *v, int step, int n, int i)
{
	/* 1011101 mit vier hellen Modulen davor oder danach */
	static const uint8_t pat[7] = { 1, 0, 1, 1, 1, 0, 1 };
	int k;

	if (i + 7 > n)
		return 0;
	for (k = 0; k < 7; k++)
		if (v[(i + k) * step] != pat[k])
			return 0;

	{
		int before = 1, after = 1;
		for (k = 1; k <= 4; k++) {
			if (i - k >= 0 && v[(i - k) * step]) before = 0;
			if (i + 6 + k < n && v[(i + 6 + k) * step]) after = 0;
		}
		return before || after;
	}
}

static long penalty(const cf_qr *q)
{
	int s = q->size, x, y, dark = 0;
	long p = 0;

	/* Regel 1: fuenf oder mehr gleiche in einer Reihe */
	for (y = 0; y < s; y++) {
		int run = 1;
		for (x = 1; x <= s; x++) {
			if (x < s && q->m[y][x] == q->m[y][x - 1]) {
				run++;
			} else {
				if (run >= 5)
					p += 3 + (run - 5);
				run = 1;
			}
		}
	}
	for (x = 0; x < s; x++) {
		int run = 1;
		for (y = 1; y <= s; y++) {
			if (y < s && q->m[y][x] == q->m[y - 1][x]) {
				run++;
			} else {
				if (run >= 5)
					p += 3 + (run - 5);
				run = 1;
			}
		}
	}

	/* Regel 2: gleichfarbige 2x2-Bloecke */
	for (y = 0; y + 1 < s; y++)
		for (x = 0; x + 1 < s; x++) {
			uint8_t c = q->m[y][x];
			if (c == q->m[y][x + 1] && c == q->m[y + 1][x] &&
			    c == q->m[y + 1][x + 1])
				p += 3;
		}

	/* Regel 3: Muster, die wie ein Finder aussehen */
	for (y = 0; y < s; y++)
		for (x = 0; x < s; x++)
			if (finder_like(&q->m[y][0], 1, s, x))
				p += 40;
	for (x = 0; x < s; x++)
		for (y = 0; y < s; y++)
			if (finder_like(&q->m[0][x], CF_QR_MAX_SIZE, s, y))
				p += 40;

	/* Regel 4: Anteil dunkler Module nahe 50 % */
	for (y = 0; y < s; y++)
		for (x = 0; x < s; x++)
			dark += q->m[y][x];
	{
		int total = s * s;
		int k = (abs(dark * 20 - total * 10) + total - 1) / total - 1;
		p += (long)k * 10;
	}

	return p;
}

/* ------------------------------------------------------------------ */
/* Kodieren                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
	uint8_t b[400];
	int     nbits;
} bitbuf;

static void put_bits(bitbuf *bb, uint32_t v, int n)
{
	int i;

	for (i = n - 1; i >= 0; i--) {
		if ((v >> i) & 1)
			bb->b[bb->nbits >> 3] |= (uint8_t)(0x80 >> (bb->nbits & 7));
		bb->nbits++;
	}
}

static int encode(cf_qr *q, const void *data, size_t len, int force_mask)
{
	const uint8_t *d = data;
	bitbuf bb;
	int ver, cap = 0, cc, i;
	int raw_cw, nb, ecc_len, nshort, short_len;
	static uint8_t blocks[5][160];
	static uint8_t all[400];
	int nall = 0, k;

	/* Kleinste Version, in die es passt */
	for (ver = 1; ver <= CF_QR_MAX_VERSION; ver++) {
		cc = ver <= 9 ? 8 : 16;
		cap = data_codewords(ver) * 8;
		if (4 + cc + (int)len * 8 <= cap)
			break;
	}
	if (ver > CF_QR_MAX_VERSION)
		return 0;

	memset(q, 0, sizeof(*q));
	q->version = ver;
	q->size = ver * 4 + 17;
	cc = ver <= 9 ? 8 : 16;

	/* Bitstrom: Modus Byte, Laenge, Daten, Ende, Fuellbytes */
	memset(&bb, 0, sizeof(bb));
	put_bits(&bb, 0x4, 4);
	put_bits(&bb, (uint32_t)len, cc);
	for (i = 0; i < (int)len; i++)
		put_bits(&bb, d[i], 8);
	put_bits(&bb, 0, (cap - bb.nbits) < 4 ? cap - bb.nbits : 4);
	put_bits(&bb, 0, (8 - bb.nbits % 8) % 8);
	for (i = 0; bb.nbits < cap; i++)
		put_bits(&bb, (i & 1) ? 0x11 : 0xEC, 8);

	/* Bloecke mit Pruefwoertern, dann verschraenkt */
	raw_cw = raw_data_modules(ver) / 8;
	nb = NUM_BLOCKS[ver];
	ecc_len = ECC_PER_BLOCK[ver];
	nshort = nb - raw_cw % nb;
	short_len = raw_cw / nb;

	for (i = 0, k = 0; i < nb; i++) {
		int dlen = short_len - ecc_len + (i < nshort ? 0 : 1);

		memset(blocks[i], 0, sizeof(blocks[i]));
		memcpy(blocks[i], bb.b + k, (size_t)dlen);
		k += dlen;
		/* Kurze Bloecke bekommen eine Luecke, damit alle gleich lang
		 * sind; die Luecke wird beim Verschraenken uebersprungen. */
		cf_qr_rs(blocks[i], (size_t)dlen,
		         blocks[i] + dlen + (i < nshort ? 1 : 0), ecc_len);
	}

	for (i = 0; i <= short_len; i++) {
		for (k = 0; k < nb; k++) {
			if (i == short_len - ecc_len && k < nshort)
				continue;
			all[nall++] = blocks[k][i];
		}
	}

	/* Matrix */
	draw_function_patterns(q);
	draw_codewords(q, all, nall);

	if (force_mask >= 0) {
		q->mask = force_mask;
	} else {
		long best = -1;
		int m;

		for (m = 0; m < 8; m++) {
			long pen;

			apply_mask(q, m);
			draw_format(q, m);
			pen = penalty(q);
			if (best < 0 || pen < best) {
				best = pen;
				q->mask = m;
			}
			apply_mask(q, m);   /* XOR, also wieder weg */
		}
	}

	apply_mask(q, q->mask);
	draw_format(q, q->mask);
	return 1;
}

int cf_qr_encode(cf_qr *q, const void *data, size_t len)
{
	return encode(q, data, len, -1);
}

int cf_qr_encode_mask(cf_qr *q, const void *data, size_t len, int mask)
{
	if (mask < 0 || mask > 7)
		return 0;
	return encode(q, data, len, mask);
}
