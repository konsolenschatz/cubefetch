#include "core/cf_fuzzy.h"
#include "core/cf_str.h"

#include <ctype.h>
#include <string.h>

size_t cf_fuzzy_normalize(char *dst, size_t dstsz, const char *title)
{
	char ascii[256];
	char body[256];
	size_t n = 0, i;
	int depth = 0;
	int space = 1;

	if (dstsz == 0)
		return 0;
	dst[0] = '\0';
	if (!title)
		return 0;

	cf_utf8_to_ascii(ascii, sizeof(ascii), title);

	/* Klammerzusaetze weg, Satzzeichen zu Leerzeichen, Kleinschreibung. */
	for (i = 0; ascii[i] && n < sizeof(body) - 1; i++) {
		unsigned char c = (unsigned char)ascii[i];

		if (c == '(' || c == '[') { depth++; continue; }
		if (c == ')' || c == ']') { if (depth) depth--; continue; }
		if (depth)
			continue;

		if (isalnum(c)) {
			body[n++] = (char)tolower(c);
			space = 0;
		} else if (!space) {
			body[n++] = ' ';
			space = 1;
		}
	}
	while (n && body[n - 1] == ' ')
		n--;
	body[n] = '\0';

	/* ", The" am Ende steht jetzt als letztes Wort " the": nach vorn. */
	if (n > 4 && !strcmp(body + n - 4, " the")) {
		body[n - 4] = '\0';
		if (strlen(body) + 5 < dstsz) {
			memcpy(dst, "the ", 4);
			strcpy(dst + 4, body);
			return strlen(dst);
		}
	}

	strncpy(dst, body, dstsz - 1);
	dst[dstsz - 1] = '\0';
	return strlen(dst);
}

/* Wortmenge: Zeiger in den normalisierten Puffer, ohne Doppelte. */
#define MAX_TOK 32

typedef struct {
	const char *w[MAX_TOK];
	size_t      len[MAX_TOK];
	size_t      n;
} tokset;

static int tok_in(const tokset *t, const char *w, size_t len)
{
	size_t i;

	for (i = 0; i < t->n; i++)
		if (t->len[i] == len && !memcmp(t->w[i], w, len))
			return 1;
	return 0;
}

static void tokenize(const char *s, tokset *t)
{
	t->n = 0;

	while (*s && t->n < MAX_TOK) {
		const char *e;

		while (*s == ' ')
			s++;
		if (!*s)
			break;
		e = s;
		while (*e && *e != ' ')
			e++;

		if (!tok_in(t, s, (size_t)(e - s))) {
			t->w[t->n] = s;
			t->len[t->n] = (size_t)(e - s);
			t->n++;
		}
		s = e;
	}
}

/* Halb Dice, halb Ueberdeckung der kleineren Menge.
 *
 * Dice allein bestraft Reihentitel: "007 - Nightfire" gegen "James Bond
 * 007: Nightfire" ergibt 2*2/(2+4) = 0,67, obwohl es dasselbe Spiel ist.
 * Ueberdeckung allein laesst "Mario" auf alles mit Mario passen. Die
 * Mischung gibt dem 007-Fall 0,83 und "Super Mario Sunshine" gegen
 * "Super Mario Strikers" 0,67 - das eine ein Treffer, das andere nicht. */
double cf_fuzzy_score(const char *a, const char *b)
{
	char na[256], nb[256];

	cf_fuzzy_normalize(na, sizeof(na), a);
	cf_fuzzy_normalize(nb, sizeof(nb), b);
	return cf_fuzzy_score_norm(na, nb);
}

double cf_fuzzy_score_norm(const char *na, const char *nb)
{
	tokset ta, tb;
	size_t common = 0, i, smaller;
	double dice, overlap;

	tokenize(na, &ta);
	tokenize(nb, &tb);

	if (!ta.n || !tb.n)
		return 0.0;

	for (i = 0; i < ta.n; i++)
		if (tok_in(&tb, ta.w[i], ta.len[i]))
			common++;

	smaller = ta.n < tb.n ? ta.n : tb.n;
	dice = 2.0 * (double)common / (double)(ta.n + tb.n);
	overlap = (double)common / (double)smaller;

	return 0.5 * dice + 0.5 * overlap;
}
