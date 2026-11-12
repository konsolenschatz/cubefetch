#include "core/cf_catalog.h"
#include "core/cf_str.h"

#include <stdlib.h>
#include <string.h>

#define CF_SEARCH_MAX 64

struct cf_catalog {
	cf_entry *e;
	size_t    n;
	size_t    cap;

	unsigned *view;
	size_t    view_n;
	size_t    view_cap;

	char      search[CF_SEARCH_MAX];
	cf_sort   sort;
	int       images_only;
	char      genre[24];
};

const char *cf_sort_key(cf_sort s)
{
	switch (s) {
	case CF_SORT_NAME_ASC:  return "cat.sort.name_asc";
	case CF_SORT_NAME_DESC: return "cat.sort.name_desc";
	case CF_SORT_SIZE_ASC:  return "cat.sort.size_asc";
	case CF_SORT_SIZE_DESC: return "cat.sort.size_desc";
	case CF_SORT_YEAR_ASC:  return "cat.sort.year_asc";
	case CF_SORT_YEAR_DESC: return "cat.sort.year_desc";
	default:                return "cat.sort";
	}
}

cf_catalog *cf_catalog_new(void)
{
	cf_catalog *c = calloc(1, sizeof(*c));
	if (!c)
		return NULL;
	c->sort = CF_SORT_NAME_ASC;
	c->images_only = 1;
	return c;
}

void cf_catalog_free(cf_catalog *c)
{
	if (!c)
		return;
	free(c->e);
	free(c->view);
	free(c);
}

static int grow_entries(cf_catalog *c)
{
	size_t cap = c->cap ? c->cap * 2 : 256;
	cf_entry *p = realloc(c->e, cap * sizeof(*p));
	if (!p)
		return 0;
	c->e = p;
	c->cap = cap;
	return 1;
}

int cf_catalog_add(cf_catalog *c, const cf_entry *e)
{
	if (c->n == c->cap && !grow_entries(c))
		return 0;
	c->e[c->n++] = *e;
	return 1;
}

size_t cf_catalog_total(const cf_catalog *c) { return c->n; }

void cf_catalog_clear(cf_catalog *c)
{
	c->n = 0;
	c->view_n = 0;
}

void cf_catalog_drop_source(cf_catalog *c, unsigned src)
{
	size_t i, o = 0;

	for (i = 0; i < c->n; i++) {
		if (c->e[i].src != src) {
			if (o != i)
				c->e[o] = c->e[i];
			o++;
		}
	}
	c->n = o;
}

void cf_catalog_set_search(cf_catalog *c, const char *needle)
{
	size_t n;

	if (!needle)
		needle = "";

	n = strlen(needle);
	if (n > CF_SEARCH_MAX - 1)
		n = CF_SEARCH_MAX - 1;
	memcpy(c->search, needle, n);
	c->search[n] = '\0';
}

void cf_catalog_set_sort(cf_catalog *c, cf_sort s) { c->sort = s; }
void cf_catalog_set_images_only(cf_catalog *c, int on) { c->images_only = on ? 1 : 0; }
cf_sort cf_catalog_sort(const cf_catalog *c) { return c->sort; }
int cf_catalog_images_only(const cf_catalog *c) { return c->images_only; }
const char *cf_catalog_search(const cf_catalog *c) { return c->search; }

void cf_catalog_set_genre(cf_catalog *c, const char *genre)
{
	size_t n = genre ? strlen(genre) : 0;

	if (n >= sizeof(c->genre))
		n = sizeof(c->genre) - 1;
	memcpy(c->genre, genre ? genre : "", n);
	c->genre[n] = '\0';
}

const char *cf_catalog_genre(const cf_catalog *c) { return c->genre; }

void cf_catalog_each(cf_catalog *c, void (*fn)(void *ctx, cf_entry *e),
                     void *ctx)
{
	size_t i;

	for (i = 0; i < c->n; i++)
		fn(ctx, &c->e[i]);
}

/* Genres stehen kommagetrennt: "action,adventure,first-person shooter".
 * Ein Genre passt, wenn es als ganzes Feld vorkommt - "action" soll nicht
 * auf "action-adventure" passen. */
static int genre_has(const char *list, const char *g)
{
	size_t gl = strlen(g);
	const char *p = list;

	while (*p) {
		const char *e = strchr(p, ',');
		size_t l = e ? (size_t)(e - p) : strlen(p);

		if (l == gl && !strncmp(p, g, gl))
			return 1;
		if (!e)
			break;
		p = e + 1;
	}
	return 0;
}

size_t cf_catalog_genres(const cf_catalog *c, char (*out)[24], size_t max)
{
	size_t n = 0, i, j, k;

	for (i = 0; i < c->n; i++) {
		const char *p = c->e[i].genre;

		while (*p) {
			const char *e = strchr(p, ',');
			size_t l = e ? (size_t)(e - p) : strlen(p);
			char g[24];
			int dup = 0;

			if (l > 0 && l < sizeof(g)) {
				memcpy(g, p, l);
				g[l] = '\0';
				for (j = 0; j < n; j++)
					if (!strcmp(out[j], g)) { dup = 1; break; }
				if (!dup && n < max) {
					/* sortiert einfuegen */
					for (k = n; k > 0 && strcmp(out[k - 1], g) > 0; k--)
						memcpy(out[k], out[k - 1], sizeof(out[k]));
					memcpy(out[k], g, sizeof(g));
					n++;
				}
			}
			if (!e)
				break;
			p = e + 1;
		}
	}
	return n;
}

/* qsort kennt in C99 keinen Nutzerkontext, darum ein Dateistatikum.
 * Der Katalog wird nur aus dem UI-Thread sortiert. */
static cf_catalog *g_sort_ctx;

static int cmp_view(const void *pa, const void *pb)
{
	const cf_entry *a = &g_sort_ctx->e[*(const unsigned *)pa];
	const cf_entry *b = &g_sort_ctx->e[*(const unsigned *)pb];

	switch (g_sort_ctx->sort) {
	case CF_SORT_NAME_DESC:
		return -cf_cmp_title(a->title, b->title);
	case CF_SORT_SIZE_ASC:
		if (a->size != b->size)
			return (a->size < b->size) ? -1 : 1;
		return cf_cmp_title(a->title, b->title);
	case CF_SORT_SIZE_DESC:
		if (a->size != b->size)
			return (a->size > b->size) ? -1 : 1;
		return cf_cmp_title(a->title, b->title);
	case CF_SORT_YEAR_ASC:
	case CF_SORT_YEAR_DESC:
		/* Unbekanntes Jahr in beiden Richtungen ans Ende: sonst stuenden
		 * bei "aelteste zuerst" alle Eintraege ohne Daten oben. */
		if (!a->year != !b->year)
			return a->year ? -1 : 1;
		if (a->year != b->year) {
			int lt = a->year < b->year;
			if (g_sort_ctx->sort == CF_SORT_YEAR_DESC)
				lt = !lt;
			return lt ? -1 : 1;
		}
		return cf_cmp_title(a->title, b->title);
	case CF_SORT_NAME_ASC:
	default:
		return cf_cmp_title(a->title, b->title);
	}
}

void cf_catalog_refresh(cf_catalog *c)
{
	size_t i;

	if (c->view_cap < c->n) {
		unsigned *p = realloc(c->view, (c->n ? c->n : 1) * sizeof(*p));
		if (!p) {
			c->view_n = 0;
			return;
		}
		c->view = p;
		c->view_cap = c->n;
	}

	c->view_n = 0;
	for (i = 0; i < c->n; i++) {
		if (c->images_only && !c->e[i].is_image)
			continue;
		if (c->search[0] && !cf_find_nocase(c->e[i].title, c->search) &&
		    !cf_find_nocase(c->e[i].genre, c->search))
			continue;
		if (c->genre[0] && !genre_has(c->e[i].genre, c->genre))
			continue;
		c->view[c->view_n++] = (unsigned)i;
	}

	if (c->view_n > 1) {
		g_sort_ctx = c;
		qsort(c->view, c->view_n, sizeof(*c->view), cmp_view);
		g_sort_ctx = NULL;
	}
}

size_t cf_view_count(const cf_catalog *c) { return c->view_n; }

cf_entry *cf_view_at(cf_catalog *c, size_t i)
{
	if (i >= c->view_n)
		return NULL;
	return &c->e[c->view[i]];
}
