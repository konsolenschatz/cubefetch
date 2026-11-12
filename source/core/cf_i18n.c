#include "core/cf_i18n.h"
#include "core/cf_str.h"

#include <string.h>

#define CF_I18N_MAX   512
#define CF_TC_SLOTS   6
#define CF_TC_LEN     256

struct kv {
	const char *k;
	const char *v;
};

static struct kv g_tab[CF_I18N_MAX];
static size_t    g_n;

/* Englisch aus dem DOL, fuer Schluessel, die der Sprachdatei auf der
 * Karte fehlen (aeltere Datei nach einem Update). */
static struct kv g_fb[CF_I18N_MAX];
static size_t    g_nfb;

static char   g_tc[CF_TC_SLOTS][CF_TC_LEN];
static int    g_tc_next;

static const char *const LANG_CODE[CF_LANG_COUNT] = {
	"en", "de", "fr", "es", "it", "ru", "ja"
};
static const char *const LANG_NAME[CF_LANG_COUNT] = {
	"English", "Deutsch", "Fran\xC3\xA7" "ais", "Espa\xC3\xB1" "ol",
	"Italiano",
	"\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9",
	"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"
};

const char *cf_lang_code(cf_lang l)
{
	return LANG_CODE[(l >= 0 && l < CF_LANG_COUNT) ? l : 0];
}

const char *cf_lang_name(cf_lang l)
{
	return LANG_NAME[(l >= 0 && l < CF_LANG_COUNT) ? l : 0];
}

cf_lang cf_lang_from_code(const char *code)
{
	int i;

	for (i = 0; i < CF_LANG_COUNT; i++)
		if (!strcmp(code, LANG_CODE[i]))
			return (cf_lang)i;
	return CF_LANG_EN;
}

/* Zerlegt den Puffer an Ort und Stelle: Zeilenenden und das '=' werden
 * durch Nullbytes ersetzt, die Tabelle zeigt in den Puffer. */
static size_t parse(char *text, struct kv *tab)
{
	char *p = text;
	size_t n = 0;

	if (!text)
		return 0;

	while (*p && n < CF_I18N_MAX) {
		char *line = p;
		char *eq;

		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			*p++ = '\0';

		/* CR am Zeilenende entfernen */
		{
			size_t l = strlen(line);
			while (l && (line[l - 1] == '\r' || line[l - 1] == ' '))
				line[--l] = '\0';
		}

		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#')
			continue;

		eq = strchr(line, '=');
		if (!eq)
			continue;
		*eq = '\0';

		/* Leerzeichen um den Schluessel */
		{
			char *ke = eq;
			while (ke > line && (ke[-1] == ' ' || ke[-1] == '\t'))
				*--ke = '\0';
		}

		tab[n].k = line;
		tab[n].v = eq + 1;
		n++;
	}

	return n;
}

size_t cf_i18n_load(char *text)
{
	g_n = parse(text, g_tab);
	return g_n;
}

size_t cf_i18n_load_fallback(char *text)
{
	g_nfb = parse(text, g_fb);
	return g_nfb;
}

/* Nur das Nötigste, damit die Anwendung auch ohne Sprachdatei auf der
 * Karte bedienbar bleibt. Alles andere zeigt dann den Schluessel. */
void cf_i18n_builtin(void)
{
	static const struct kv builtin[] = {
		{ "app.title",      "CubeFetch" },
		{ "menu.catalog",   "Catalog" },
		{ "menu.queue",     "Queue" },
		{ "menu.sources",   "Sources" },
		{ "menu.settings",  "Settings" },
		{ "cat.search",     "Search" },
		{ "cat.empty",      "Nothing found" },
		{ "cat.sort",       "Sort" },
		{ "q.add",          "Add to queue" },
		{ "q.empty",        "Queue is empty" },
		{ "q.start",        "Start" },
		{ "q.done",         "Done" },
		{ "q.failed",       "Failed" },
		{ "src.none",       "No sources defined" },
		{ "btn.ok",         "OK" },
		{ "btn.cancel",     "Cancel" },
		{ "btn.back",       "Back" },
	};
	size_t i;

	g_n = 0;
	for (i = 0; i < sizeof(builtin) / sizeof(builtin[0]) && g_n < CF_I18N_MAX; i++)
		g_tab[g_n++] = builtin[i];
}

const char *cf_t(const char *key)
{
	size_t i;

	if (!key)
		return "";
	for (i = 0; i < g_n; i++) {
		if (strcmp(g_tab[i].k, key) == 0)
			return g_tab[i].v;
	}
	for (i = 0; i < g_nfb; i++) {
		if (strcmp(g_fb[i].k, key) == 0)
			return g_fb[i].v;
	}
	return key;
}

const char *cf_ascii(const char *utf8)
{
	char *slot = g_tc[g_tc_next];

	g_tc_next = (g_tc_next + 1) % CF_TC_SLOTS;
	cf_utf8_to_ascii(slot, CF_TC_LEN, utf8 ? utf8 : "");
	return slot;
}

const char *cf_tc(const char *key)
{
	return cf_ascii(cf_t(key));
}
