/* Schreibt einen QR-Code als BMP, um ihn am PC mit dem Telefon zu pruefen.
 *   ./qrbmp "https://discord.gg/EqUpCbGM5p" discord.bmp
 */
#include <stdio.h>
#include <string.h>
#include "core/cf_qr.h"

static void le(FILE *f, unsigned v, int n)
{
	while (n--) { fputc((int)(v & 0xFF), f); v >>= 8; }
}

int main(int argc, char **argv)
{
	static cf_qr q;
	const int S = 8, Q = 4;
	int dim, x, y, row;
	FILE *f;

	if (argc < 3 || !cf_qr_encode(&q, argv[1], strlen(argv[1])))
		return 1;
	dim = (q.size + 2 * Q) * S;
	row = (dim * 3 + 3) & ~3;
	f = fopen(argv[2], "wb");
	if (!f)
		return 1;
	fputc('B', f); fputc('M', f);
	le(f, 54 + row * dim, 4); le(f, 0, 4); le(f, 54, 4);
	le(f, 40, 4); le(f, dim, 4); le(f, dim, 4); le(f, 1, 2); le(f, 24, 2);
	le(f, 0, 4); le(f, row * dim, 4); le(f, 2835, 4); le(f, 2835, 4);
	le(f, 0, 4); le(f, 0, 4);
	for (y = dim - 1; y >= 0; y--) {
		for (x = 0; x < dim; x++) {
			int mx = x / S - Q, my = y / S - Q, dark = 0;
			if (mx >= 0 && my >= 0 && mx < q.size && my < q.size)
				dark = q.m[my][mx];
			le(f, dark ? 0 : 0xFFFFFF, 3);
		}
		for (x = dim * 3; x < row; x++)
			fputc(0, f);
	}
	fclose(f);
	printf("Version %d, Maske %d\n", q.version, q.mask);
	return 0;
}
