#include "core/cf_queue.h"
#include "core/cf_str.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cf_queue_add(cf_queue *q, const cf_qitem *it)
{
	if (q->n >= CF_QUEUE_MAX)
		return 0;
	if (!it->url[0] || !it->filename[0])
		return 0;

	q->v[q->n++] = *it;
	return 1;
}

int cf_queue_remove(cf_queue *q, size_t idx)
{
	size_t i;

	if (idx >= q->n)
		return 0;
	for (i = idx; i + 1 < q->n; i++)
		q->v[i] = q->v[i + 1];
	q->n--;
	return 1;
}

int cf_queue_move(cf_queue *q, size_t from, size_t to)
{
	cf_qitem tmp;
	size_t i;

	if (from >= q->n || to >= q->n)
		return 0;
	if (from == to)
		return 1;

	tmp = q->v[from];
	if (from < to) {
		for (i = from; i < to; i++)
			q->v[i] = q->v[i + 1];
	} else {
		for (i = from; i > to; i--)
			q->v[i] = q->v[i - 1];
	}
	q->v[to] = tmp;
	return 1;
}

size_t cf_queue_find(const cf_queue *q, const char *url)
{
	size_t i;

	for (i = 0; i < q->n; i++)
		if (!strcmp(q->v[i].url, url))
			return i;
	return CF_QUEUE_MAX;
}

void cf_queue_clear_finished(cf_queue *q)
{
	size_t i, o = 0;

	for (i = 0; i < q->n; i++) {
		if (q->v[i].state != CF_Q_DONE) {
			if (o != i)
				q->v[o] = q->v[i];
			o++;
		}
	}
	q->n = o;
}

int cf_queue_contains(const cf_queue *q, const char *url)
{
	size_t i;

	for (i = 0; i < q->n; i++) {
		if (!strcmp(q->v[i].url, url))
			return 1;
	}
	return 0;
}

size_t cf_queue_next_waiting(const cf_queue *q)
{
	size_t i;

	for (i = 0; i < q->n; i++) {
		if (q->v[i].state == CF_Q_WAIT)
			return i;
	}
	return CF_QUEUE_MAX;
}

size_t cf_queue_count_waiting(const cf_queue *q)
{
	size_t i, n = 0;

	for (i = 0; i < q->n; i++) {
		if (q->v[i].state == CF_Q_WAIT)
			n++;
	}
	return n;
}

uint64_t cf_queue_bytes_waiting(const cf_queue *q)
{
	uint64_t t = 0;
	size_t i;

	for (i = 0; i < q->n; i++) {
		if (q->v[i].state == CF_Q_WAIT)
			t += q->v[i].size;
	}
	return t;
}

void cf_qitem_from_entry(cf_qitem *it, const cf_entry *e, const char *url)
{
	const char *base;

	memset(it, 0, sizeof(*it));

	snprintf(it->url, sizeof(it->url), "%s", url);
	snprintf(it->title, sizeof(it->title), "%s", e->title);
	it->size = e->size;
	it->crc32 = e->crc32;
	it->has_crc = e->has_crc;
	it->state = CF_Q_WAIT;

	/* Nur der Dateiname, ohne den Buchstabenordner der Quelle. */
	base = strrchr(e->path, '/');
	base = base ? base + 1 : e->path;
	snprintf(it->filename, sizeof(it->filename), "%s", base);
	cf_fat_sanitize(it->filename);
}

/* ------------------------------------------------------------------ */
/* Ablage                                                             */
/* ------------------------------------------------------------------ */

/* Tab als Trenner: in Dateinamen praktisch ausgeschlossen, waehrend '|'
 * in Spieletiteln durchaus vorkommt. */
size_t cf_queue_serialize(const cf_queue *q, char *dst, size_t dstsz)
{
	size_t need = 0, o = 0;
	size_t i;

	#define PUT(...) do {                                              \
		int r = snprintf(dst + (o < dstsz ? o : dstsz),             \
		                 (o < dstsz) ? dstsz - o : 0, __VA_ARGS__);  \
		if (r > 0) { need += (size_t)r; o += (size_t)r; }            \
	} while (0)

	PUT("%s", "# CubeFetch queue v1\n");

	for (i = 0; i < q->n; i++) {
		const cf_qitem *it = &q->v[i];

		if (it->has_crc)
			PUT("%u\t%llu\t%08x\t%s\t%s\t%s\n",
			    (unsigned)it->state, (unsigned long long)it->size,
			    (unsigned)it->crc32, it->title, it->filename, it->url);
		else
			PUT("%u\t%llu\t-\t%s\t%s\t%s\n",
			    (unsigned)it->state, (unsigned long long)it->size,
			    it->title, it->filename, it->url);
	}

	#undef PUT

	if (dstsz)
		dst[(o < dstsz) ? o : dstsz - 1] = '\0';
	return need;
}

/* Zerlegt eine Zeile an Tabs. Gibt die Anzahl der Felder zurueck. */
static size_t split_tabs(char *line, char **f, size_t maxf)
{
	size_t n = 0;

	f[n++] = line;
	while (*line && n < maxf) {
		if (*line == '\t') {
			*line++ = '\0';
			f[n++] = line;
			continue;
		}
		line++;
	}
	return n;
}

size_t cf_queue_parse(cf_queue *q, const char *text)
{
	char *copy;
	char *p;

	q->n = 0;
	if (!text)
		return 0;

	/* An Ort und Stelle zerlegen, darum eine Arbeitskopie. */
	copy = malloc(strlen(text) + 1);
	if (!copy)
		return 0;
	memcpy(copy, text, strlen(text) + 1);

	p = copy;
	while (*p && q->n < CF_QUEUE_MAX) {
		char *line = p;
		char *f[6];
		size_t nf;

		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			*p++ = '\0';

		{
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\r' || line[l - 1] == ' '))
				line[--l] = '\0';
		}

		if (!line[0] || line[0] == '#')
			continue;

		nf = split_tabs(line, f, 6);
		if (nf < 6)
			continue;

		{
			cf_qitem it;

			memset(&it, 0, sizeof(it));
			it.state = (uint8_t)strtoul(f[0], NULL, 10);
			it.size = (uint64_t)strtoull(f[1], NULL, 10);

			if (f[2][0] != '-') {
				it.crc32 = (uint32_t)strtoul(f[2], NULL, 16);
				it.has_crc = 1;
			}

			snprintf(it.title, sizeof(it.title), "%s", f[3]);
			snprintf(it.filename, sizeof(it.filename), "%s", f[4]);
			snprintf(it.url, sizeof(it.url), "%s", f[5]);

			if (it.state > CF_Q_FAIL)
				it.state = CF_Q_WAIT;

			if (it.url[0] && it.filename[0])
				q->v[q->n++] = it;
		}
	}

	free(copy);
	return q->n;
}
