/* Tests fuer den portablen Kern.
 *
 * Laeuft mit einem normalen Host-Compiler, ohne devkitPPC und ohne
 * Konsole.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/cf_catalog.h"
#include "core/cf_entry.h"
#include "core/cf_json.h"
#include "core/cf_queue.h"
#include "core/cf_qr.h"
#include "core/cf_sources.h"
#include "core/cf_srcparse.h"
#include "core/cf_crc32.h"
#include "core/cf_fuzzy.h"
#include "core/cf_gametdb.h"
#include "core/cf_str.h"

static int g_fail;
static int g_run;

#define CHECK(cond, ...) do {                        \
	g_run++;                                      \
	if (!(cond)) {                                \
		g_fail++;                             \
		printf("  FEHLER %s:%d  ", __FILE__, __LINE__); \
		printf(__VA_ARGS__);                  \
		printf("\n");                         \
	}                                             \
} while (0)

#define EQ_STR(got, want) do {                                        \
	g_run++;                                                       \
	if (strcmp((got), (want)) != 0) {                              \
		g_fail++;                                              \
		printf("  FEHLER %s:%d\n    erwartet [%s]\n    bekommen [%s]\n", \
		       __FILE__, __LINE__, (want), (got));             \
	}                                                              \
} while (0)

#define EQ_U64(got, want) do {                                        \
	g_run++;                                                       \
	if ((unsigned long long)(got) != (unsigned long long)(want)) { \
		g_fail++;                                              \
		printf("  FEHLER %s:%d  erwartet %llu, bekommen %llu\n", \
		       __FILE__, __LINE__,                             \
		       (unsigned long long)(want),                     \
		       (unsigned long long)(got));                     \
	}                                                              \
} while (0)

/* ------------------------------------------------------------------ */

static int add_cb(void *ctx, const cf_entry *e)
{
	return cf_catalog_add((cf_catalog *)ctx, e) ? 0 : -1;
}

/* ------------------------------------------------------------------ */

static void test_str(void)
{
	char buf[128];

	printf("cf_str\n");

	/* Umlaute fuer die Konsole */
	cf_utf8_to_ascii(buf, sizeof(buf), "Groesse");
	EQ_STR(buf, "Groesse");
	/* "Größe". Das \x9f muss durch Stringtrennung beendet werden, sonst
	 * liest der Compiler \x9fe als eine dreistellige Hex-Angabe. */
	cf_utf8_to_ascii(buf, sizeof(buf), "Gr\xc3\xb6\xc3\x9f" "e");
	EQ_STR(buf, "Groesse");
	cf_utf8_to_ascii(buf, sizeof(buf), "\xc3\x84rger");          /* Ärger */
	EQ_STR(buf, "Aerger");

	/* Suche ohne Beachtung von Gross-/Kleinschreibung und Umlauten */
	CHECK(cf_find_nocase("Chibi-Robo!", "chibi"), "Teilstring");
	CHECK(cf_find_nocase("007 - Nightfire", "NIGHT"), "Grossschreibung");
	CHECK(!cf_find_nocase("Luigi's Mansion", "mario"), "kein Treffer");

	/* Beim Suchen faltet ue auf u und ss auf s, "Größe" wird also als
	 * "grose" gefunden - anders als bei der Konsolenausgabe, die auf
	 * "Groesse" transliteriert. */
	CHECK(cf_find_nocase("Gr\xc3\xb6\xc3\x9f" "e", "grose"),
	      "Umlaut faltet auf Grundbuchstaben");
	CHECK(cf_find_nocase("\xc3\x84rger", "arger"), "Ae-Suche");

	/* Sortierung: Umlaut muss bei seinem Grundbuchstaben liegen */
	CHECK(cf_cmp_title("Apfel", "Banane") < 0, "A vor B");
	CHECK(cf_cmp_title("\xc3\x84pfel", "Banane") < 0, "Ae vor B");
	CHECK(cf_cmp_title("Zelda", "Mario") > 0, "Z nach M");
	CHECK(cf_cmp_title("gleich", "gleich") == 0, "gleich");

	/* URL-Kodierung: Schraegstriche bleiben, der Rest wird kodiert */
	cf_url_encode_path(buf, sizeof(buf), "#/My Homebrew - Demo.iso");
	EQ_STR(buf, "%23/My%20Homebrew%20-%20Demo.iso");

	/* FAT-taugliche Dateinamen */
	snprintf(buf, sizeof(buf), "%s", "Kirby: Air Ride?.iso");
	cf_fat_sanitize(buf);
	EQ_STR(buf, "Kirby_ Air Ride_.iso");

	snprintf(buf, sizeof(buf), "%s", "Trailing dots...");
	cf_fat_sanitize(buf);
	EQ_STR(buf, "Trailing dots");

	/* Groessenangaben */
	cf_fmt_size(buf, sizeof(buf), 1459978240ull);
	EQ_STR(buf, "1.35 GB");
	cf_fmt_size(buf, sizeof(buf), 1024);
	EQ_STR(buf, "1 KB");
	cf_fmt_duration(buf, sizeof(buf), 1920);
	EQ_STR(buf, "32m 00s");
}

static void test_json(void)
{
	static const char doc[] =
		"{\"a\":\"x\\/y\",\"n\":\"123\",\"m\":45,"
		"\"arr\":[{\"k\":1},{\"k\":2},{\"k\":3}],"
		"\"nest\":{\"deep\":{\"v\":\"ok\"}},\"last\":\"end\"}";
	const char *end = doc + sizeof(doc) - 1;
	const char *v;
	char buf[64];
	uint64_t u;

	printf("cf_json\n");

	/* Escape \/ muss zu / werden */
	v = cf_json_member(doc, end, "a");
	CHECK(v != NULL, "Mitglied a");
	cf_json_str(v, end, buf, sizeof(buf));
	EQ_STR(buf, "x/y");

	/* Zahl als String, wie manche Server Groessen liefern */
	v = cf_json_member(doc, end, "n");
	CHECK(v && cf_json_u64(v, end, &u), "n lesbar");
	EQ_U64(u, 123);

	/* Zahl als Zahl */
	v = cf_json_member(doc, end, "m");
	CHECK(v && cf_json_u64(v, end, &u), "m lesbar");
	EQ_U64(u, 45);

	/* Ein Mitglied hinter Array und verschachteltem Objekt muss noch
	 * gefunden werden; dafuer muss das Ueberspringen stimmen. */
	v = cf_json_member(doc, end, "last");
	CHECK(v != NULL, "Mitglied nach Array und Objekt");
	if (v) {
		cf_json_str(v, end, buf, sizeof(buf));
		EQ_STR(buf, "end");
	}

	/* Array durchlaufen */
	v = cf_json_member(doc, end, "arr");
	CHECK(v != NULL, "arr");
	if (v) {
		const char *p = cf_json_arr_begin(v, end);
		const char *item;
		int n = 0;

		while (p && cf_json_arr_next(&p, end, &item))
			n++;
		EQ_U64(n, 3);
	}

	/* Unbekanntes Mitglied */
	CHECK(cf_json_member(doc, end, "fehlt") == NULL, "fehlendes Mitglied");
}

static void test_sources(void)
{
	cf_sources s;
	char buf[2048];

	printf("cf_sources\n");

	memset(&s, 0, sizeof(s));
	cf_sources_parse(&s,
		"# Kommentar\n"
		"\n"
		"autoidx | Beispiel | http://192.168.1.50/homebrew/\n"
		"autoidx | Heim | http://192.168.1.50/gc/\n"
		"http://192.168.1.50/liste.json\n"
		"  manifest|Kuratiert|http://example.invalid/x.json  \n");

	EQ_U64(s.n, 4);
	EQ_U64(s.v[0].type, CF_SRC_AUTOIDX);
	EQ_STR(s.v[0].name, "Beispiel");
	EQ_STR(s.v[0].url, "http://192.168.1.50/homebrew/");
	EQ_U64(s.v[1].type, CF_SRC_AUTOIDX);

	/* Ohne Typ wird geraten: .json also manifest */
	EQ_U64(s.v[2].type, CF_SRC_MANIFEST);
	EQ_STR(s.v[2].url, "http://192.168.1.50/liste.json");

	/* Leerzeichen um die Felder muessen weg */
	EQ_U64(s.v[3].type, CF_SRC_MANIFEST);
	EQ_STR(s.v[3].name, "Kuratiert");

	/* Raten */
	EQ_U64(cf_src_guess("http://host/a.JSON"), CF_SRC_MANIFEST);
	EQ_U64(cf_src_guess("http://host/dir/"), CF_SRC_AUTOIDX);

	/* Hin und zurueck */
	cf_sources_serialize(&s, buf, sizeof(buf));
	{
		cf_sources s2;
		memset(&s2, 0, sizeof(s2));
		cf_sources_parse(&s2, buf);
		EQ_U64(s2.n, s.n);
		EQ_STR(s2.v[0].url, s.v[0].url);
		EQ_STR(s2.v[3].name, s.v[3].name);
	}
}

static void test_autoidx(void)
{
	static const char html[] =
		"<html><head><title>Index of /gc</title></head><body>"
		"<h1>Index of /gc</h1><pre>"
		"<a href=\"?C=N;O=D\">Name</a>"
		"<a href=\"../\">Parent Directory</a>"
		"<a href=\"subdir/\">subdir/</a>"
		"<a href=\"Chibi-Robo%21.iso\">Chibi-Robo!.iso</a>  1.4G"
		"<a href=\"Pikmin%202.iso\">Pikmin 2.iso</a>  1.4G"
		"<a href=\"notes&amp;more.txt\">notes&amp;more.txt</a>"
		"<a href=\"http://elsewhere.invalid/x.iso\">extern</a>"
		"</pre></body></html>";
	cf_catalog *c = cf_catalog_new();
	size_t n;

	printf("cf_parse_autoidx\n");

	n = cf_parse_autoidx(html, sizeof(html) - 1, 0, add_cb, c);

	/* Sortierlink, Elternlink, Unterordner und die absolute Adresse
	 * muessen wegfallen. Bleiben drei Dateien. */
	EQ_U64(n, 3);

	cf_catalog_set_images_only(c, 0);
	cf_catalog_set_sort(c, CF_SORT_NAME_ASC);
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 3);

	/* %21 muss zu ! dekodiert und &amp; zu & aufgeloest werden.
	 * Reihenfolge: c vor n vor p. */
	EQ_STR(cf_view_at(c, 0)->title, "Chibi-Robo!");
	EQ_STR(cf_view_at(c, 1)->title, "notes&more");
	EQ_STR(cf_view_at(c, 2)->title, "Pikmin 2");

	/* Nur die Images */
	cf_catalog_set_images_only(c, 1);
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 2);

	cf_catalog_free(c);
}

static void test_manifest(void)
{
	static const char doc[] =
		"{\"cubefetch\":1,\"name\":\"Test\","
		"\"base\":\"http://host/gc/\","
		"\"entries\":["
		"{\"file\":\"Chibi-Robo.iso\",\"size\":1459978240,"
		 "\"id\":\"GCBP01\",\"crc32\":\"a1b2c3d4\"},"
		"{\"file\":\"sub/Pikmin 2.iso\",\"title\":\"Pikmin 2 (PAL)\"}"
		"]}";
	cf_catalog *c = cf_catalog_new();
	char base[288];
	size_t n;
	cf_entry *e;

	printf("cf_parse_manifest\n");

	n = cf_parse_manifest(doc, sizeof(doc) - 1, 0,
	                      base, sizeof(base), add_cb, c);
	EQ_U64(n, 2);
	EQ_STR(base, "http://host/gc/");

	cf_catalog_set_sort(c, CF_SORT_NAME_ASC);
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 2);

	e = cf_view_at(c, 0);
	EQ_STR(e->title, "Chibi-Robo");
	EQ_U64(e->size, 1459978240ull);
	EQ_STR(e->id, "GCBP01");
	EQ_U64(e->id_origin, CF_ID_MANIFEST);
	EQ_U64(e->crc32, 0xa1b2c3d4u);

	/* Ein title im Manifest hat Vorrang vor dem Dateinamen */
	e = cf_view_at(c, 1);
	EQ_STR(e->title, "Pikmin 2 (PAL)");

	cf_catalog_free(c);
}

static void test_queue(void)
{
	cf_queue q;
	cf_entry e;
	cf_qitem it;
	char buf[4096];

	printf("cf_queue\n");

	memset(&q, 0, sizeof(q));
	memset(&e, 0, sizeof(e));

	snprintf(e.path, sizeof(e.path), "%s", "#/My Homebrew.iso");
	cf_entry_title_from_path(&e);
	cf_entry_mark_image(&e);
	e.size = 1459978240ull;
	e.crc32 = 0x12345678u;
	e.has_crc = 1;

	EQ_STR(e.title, "My Homebrew");
	EQ_U64(e.is_image, 1);

	cf_qitem_from_entry(&it, &e, "http://host/x.iso");

	/* Der Buchstabenordner der Quelle darf nicht in den Zielnamen */
	EQ_STR(it.filename, "My Homebrew.iso");

	CHECK(cf_queue_add(&q, &it), "Eintrag aufnehmen");
	EQ_U64(q.n, 1);
	CHECK(cf_queue_contains(&q, "http://host/x.iso"), "schon drin");
	CHECK(!cf_queue_contains(&q, "http://host/y.iso"), "nicht drin");
	EQ_U64(cf_queue_bytes_waiting(&q), 1459978240ull);

	/* Hin und zurueck ueber die Ablage */
	cf_queue_serialize(&q, buf, sizeof(buf));
	{
		cf_queue q2;
		memset(&q2, 0, sizeof(q2));
		cf_queue_parse(&q2, buf);
		EQ_U64(q2.n, 1);
		EQ_STR(q2.v[0].filename, it.filename);
		EQ_STR(q2.v[0].url, it.url);
		EQ_U64(q2.v[0].size, it.size);
		EQ_U64(q2.v[0].crc32, it.crc32);
		EQ_U64(q2.v[0].has_crc, 1);
	}

	/* Fertige entfernen */
	q.v[0].state = CF_Q_DONE;
	cf_queue_clear_finished(&q);
	EQ_U64(q.n, 0);

	/* Verschieben: "als Naechstes laden" im Download-Bildschirm */
	{
		const char *names[] = { "a", "b", "c", "d" };
		size_t i;

		memset(&q, 0, sizeof(q));
		for (i = 0; i < 4; i++) {
			memset(&it, 0, sizeof(it));
			snprintf(it.url, sizeof(it.url), "http://h/%s", names[i]);
			snprintf(it.title, sizeof(it.title), "%s", names[i]);
			snprintf(it.filename, sizeof(it.filename), "%s.iso", names[i]);
			CHECK(cf_queue_add(&q, &it), "add %s", names[i]);
		}

		/* d direkt hinter a: a d b c */
		CHECK(cf_queue_move(&q, 3, 1), "move 3->1");
		EQ_STR(q.v[0].title, "a");
		EQ_STR(q.v[1].title, "d");
		EQ_STR(q.v[2].title, "b");
		EQ_STR(q.v[3].title, "c");

		/* und zurueck nach hinten: a b c d */
		CHECK(cf_queue_move(&q, 1, 3), "move 1->3");
		EQ_STR(q.v[1].title, "b");
		EQ_STR(q.v[3].title, "d");

		CHECK(!cf_queue_move(&q, 9, 0), "ausserhalb");
		EQ_U64(cf_queue_find(&q, "http://h/c"), 2u);
		EQ_U64(cf_queue_find(&q, "http://h/x"), (uint64_t)CF_QUEUE_MAX);
	}
}

/* ------------------------------------------------------------------ */
/* cf_crc32 - geprueft gegen die Normwerte von CRC-32/ISO-HDLC, also
 * gegen dieselben Zahlen, die auch zlib liefert. Die
 * Funktion ersetzt zlibs crc32; falsch waere sie teuer, denn ein
 * fehlerhafter Vergleich verwirft am Ende eines halbstuendigen
 * Downloads eine heile Datei.                                        */

static void test_crc32(void)
{
	static const char nine[] = "123456789";
	static const char fox[]  = "The quick brown fox jumps over the lazy dog";
	char              many[1000];
	uint32_t          c;

	printf("cf_crc32\n");

	/* Startwert, genau wie zlib ihn liefert */
	EQ_U64(cf_crc32(0, NULL, 0), 0u);

	/* leerer Puffer veraendert nichts */
	EQ_U64(cf_crc32(0, nine, 0), 0u);

	/* der Normpruefwert */
	EQ_U64(cf_crc32(0, nine, 9), 0xCBF43926u);

	EQ_U64(cf_crc32(0, fox, sizeof(fox) - 1), 0x414FA339u);

	/* stueckweise fortgesetzt muss dasselbe herauskommen - das ist der
	 * Fall, der beim Fortsetzen eines Downloads zaehlt */
	c = cf_crc32(0, nine, 4);
	EQ_U64(cf_crc32(c, nine + 4, 5), 0xCBF43926u);

	memset(many, 'a', sizeof(many));
	EQ_U64(cf_crc32(0, many, sizeof(many)), 0x9A38DA03u);
}

static void test_crc32_combine(void)
{
	static const char nine[] = "123456789";
	char     *p, *q;
	uint32_t  c1, c2;
	size_t    i;

	printf("cf_crc32_combine\n");

	/* Laenge 0: der zweite Block existiert nicht */
	EQ_U64(cf_crc32_combine(0x12345678u, 0xDEADBEEFu, 0), 0x12345678u);

	/* "1234" + "56789" muss den Normpruefwert ergeben, obwohl beide
	 * Teile getrennt gerechnet wurden - genau das macht der Download
	 * mit seinen Bereichen */
	c1 = cf_crc32(0, nine, 4);
	c2 = cf_crc32(0, nine + 4, 5);
	EQ_U64(c1, 0x9BE3E0A3u);
	EQ_U64(c2, 0x131DA070u);
	EQ_U64(cf_crc32_combine(c1, c2, 5), 0xCBF43926u);

	/* 377 + 623 Zeichen 'a' ergeben die 1000 von oben */
	p = malloc(1000);
	CHECK(p != NULL, "malloc");
	memset(p, 'a', 1000);
	c1 = cf_crc32(0, p, 377);
	c2 = cf_crc32(0, p + 377, 623);
	EQ_U64(cf_crc32_combine(c1, c2, 623), 0x9A38DA03u);
	free(p);

	/* Zwei Bloecke von je 1 MiB. Erst bei dieser Groesse laeuft die
	 * Matrix-Quadrierung ueber viele Stufen, und genau so gross sind
	 * die Bereiche beim echten Download. */
	p = malloc(1024 * 1024);
	q = malloc(1024 * 1024);
	CHECK(p != NULL && q != NULL, "malloc");
	for (i = 0; i < 1024u * 1024u; i++) {
		p[i] = (char)(i & 0xFF);
		q[i] = (char)(((i & 1023u) * 7u) & 0xFF);
	}
	c1 = cf_crc32(0, p, 1024 * 1024);
	c2 = cf_crc32(0, q, 1024 * 1024);
	EQ_U64(c1, 0x04D0E435u);
	EQ_U64(c2, 0x1E8123C3u);
	EQ_U64(cf_crc32_combine(c1, c2, 1024 * 1024), 0xE8F8C352u);
	free(p);
	free(q);
}

/* ------------------------------------------------------------------ */
/* Namensaehnlichkeit                                                 */
/* ------------------------------------------------------------------ */

static void test_fuzzy(void)
{
	char n[256];
	double s;

	printf("cf_fuzzy\n");

	cf_fuzzy_normalize(n, sizeof(n), "Legend of Zelda, The (USA)");
	EQ_STR(n, "the legend of zelda");

	cf_fuzzy_normalize(n, sizeof(n), "007 - Nightfire (Germany, France) [v1.01]");
	EQ_STR(n, "007 nightfire");

	/* Umlaute werden zu ae/ue/oe, wie cf_utf8_to_ascii es fuer deutsche
	 * Titel tut - Datenbank und Dateiname laufen durch dieselbe
	 * Funktion, also passen sie zueinander. */
	cf_fuzzy_normalize(n, sizeof(n), "Käpt'n Blaubär: Übers Meer");
	EQ_STR(n, "kaept n blaubaer uebers meer");

	/* gleich */
	s = cf_fuzzy_score("Super Mario Sunshine", "Super Mario Sunshine (Europe)");
	CHECK(s > 0.999, "gleich: %.3f", s);

	/* Reihentitel: halb Dice, halb Ueberdeckung -> 0,83, also Treffer */
	s = cf_fuzzy_score("007 - Nightfire (Germany)", "James Bond 007: Nightfire");
	CHECK(s > 0.82 && s < 0.84, "007: %.3f", s);
	CHECK(s >= CF_FUZZY_SUGGEST, "007 ueber der Schwelle");

	/* verwandt, aber ein anderes Spiel: 0,67, also kein Treffer */
	s = cf_fuzzy_score("Super Mario Sunshine", "Super Mario Strikers");
	CHECK(s > 0.66 && s < 0.68, "Sunshine/Strikers: %.3f", s);
	CHECK(s < CF_FUZZY_SUGGEST, "Strikers unter der Schwelle");

	/* nichts gemeinsam */
	s = cf_fuzzy_score("Metroid Prime", "The Legend of Zelda: The Wind Waker");
	CHECK(s < 0.01, "fremd: %.3f", s);

	/* leer */
	CHECK(cf_fuzzy_score("", "Metroid Prime") == 0.0, "leer");
}

/* ------------------------------------------------------------------ */
/* GameTDB                                                            */
/* ------------------------------------------------------------------ */

static const char gtdb_xml[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<datafile>\n"
	"<WiiTDB version=\"20261109\" games=\"2\"/>\n"
	"<game name=\"Metroid Prime\">\n"
	"  <id>GM8E01</id>\n"
	"  <type>GameCube</type>\n"
	"  <region>NTSC-U</region>\n"
	"  <locale lang=\"EN\"><title>Metroid Prime</title><synopsis>Samus &amp; co.</synopsis></locale>\n"
	"  <locale lang=\"DE\"><title>Metroid Prime (DE-Titel)</title></locale>\n"
	"  <developer>Retro Studios</developer>\n"
	"  <publisher>Nintendo</publisher>\n"
	"  <date year=\"2002\" month=\"11\" day=\"17\"/>\n"
	"  <genre>action,adventure,first-person shooter</genre>\n"
	"  <input players=\"1\"><control type=\"gamecube\" required=\"true\"/></input>\n"
	"</game>\n"
	"<game name=\"Wii Sports\">\n"
	"  <id>RSPE01</id>\n"
	"  <type></type>\n"
	"  <locale lang=\"EN\"><title>Wii Sports</title></locale>\n"
	"  <date year=\"2006\" month=\"11\" day=\"19\"/>\n"
	"</game>\n"
	"<game name=\"Zelda &amp; Co\">\n"
	"  <id>GZLP01</id>\n"
	"  <type>GameCube</type>\n"
	"  <locale lang=\"EN\"><title>The Legend of Zelda: The Wind Waker</title></locale>\n"
	"  <publisher>Nintendo</publisher>\n"
	"  <date year=\"2003\"/>\n"
	"  <genre>action,adventure</genre>\n"
	"  <input players=\"1\"/>\n"
	"</game>\n"
	"</datafile>\n";

typedef struct {
	cf_gtdb_game g[8];
	size_t       n;
} gtdb_collect;

static int gtdb_cb(void *ctx, const cf_gtdb_game *g)
{
	gtdb_collect *c = ctx;

	if (c->n < 8)
		c->g[c->n++] = *g;
	return 0;
}

static void test_gametdb(void)
{
	gtdb_collect c;
	cf_gtdb_xml *x;
	cf_gtdb_index ix;
	cf_gtdb_game g;
	char line[512];
	size_t total = sizeof(gtdb_xml) - 1, i;
	const cf_gtdb_game *f;
	double score;

	printf("cf_gametdb\n");

	/* In krummen Stuecken fuettern, damit Tags ueber Grenzen laufen. */
	memset(&c, 0, sizeof(c));
	x = cf_gtdb_xml_new(gtdb_cb, &c, "DE");
	CHECK(x != NULL, "xml new");
	for (i = 0; i < total; i += 37) {
		size_t n = total - i < 37 ? total - i : 37;
		CHECK(cf_gtdb_xml_feed(x, gtdb_xml + i, n) == 0, "feed");
	}
	EQ_U64(cf_gtdb_xml_count(x), 2u);        /* Wii Sports uebersprungen */
	cf_gtdb_xml_free(x);

	EQ_U64(c.n, 2u);
	EQ_STR(c.g[0].id, "GM8E01");
	EQ_STR(c.g[0].title, "Metroid Prime (DE-Titel)");   /* DE bevorzugt */
	EQ_STR(c.g[0].developer, "Retro Studios");
	EQ_STR(c.g[0].publisher, "Nintendo");
	EQ_STR(c.g[0].genre, "action,adventure,first-person shooter");
	EQ_U64(c.g[0].year, 2002u);
	EQ_U64(c.g[0].month, 11u);
	EQ_U64(c.g[0].day, 17u);
	EQ_U64(c.g[0].players, 1u);

	EQ_STR(c.g[1].id, "GZLP01");
	EQ_STR(c.g[1].title, "The Legend of Zelda: The Wind Waker"); /* kein DE: EN */
	EQ_U64(c.g[1].year, 2003u);
	EQ_U64(c.g[1].month, 0u);

	/* Zeilenformat hin und zurueck */
	CHECK(cf_gtdb_line(&c.g[0], line, sizeof(line)) > 0, "line");
	CHECK(cf_gtdb_parse_line(line, &g), "parse line");
	EQ_STR(g.id, c.g[0].id);
	EQ_STR(g.title, c.g[0].title);
	EQ_STR(g.genre, c.g[0].genre);
	EQ_U64(g.year, 2002u);
	EQ_U64(g.players, 1u);

	/* Index: einfuegen, sortieren, finden */
	memset(&ix, 0, sizeof(ix));
	CHECK(cf_gtdb_index_add(&ix, &c.g[1]), "add 1");
	CHECK(cf_gtdb_index_add(&ix, &c.g[0]), "add 0");
	cf_gtdb_index_sort(&ix);
	f = cf_gtdb_index_find(&ix, "GM8E01");
	CHECK(f && !strcmp(f->developer, "Retro Studios"), "find GM8E01");
	CHECK(cf_gtdb_index_find(&ix, "RSPE01") == NULL, "Wii nicht drin");
	CHECK(cf_gtdb_index_find(&ix, "AAAAAA") == NULL, "fremd");

	/* Namensaehnlichkeit ueber den Index, wie fuer Eintraege ohne ID */
	f = cf_gtdb_index_fuzzy(&ix, "Legend of Zelda, The - Wind Waker (Europe)", &score);
	CHECK(f && !strcmp(f->id, "GZLP01"), "fuzzy Zelda -> %s", f ? f->id : "-");
	CHECK(score >= CF_FUZZY_AUTO, "fuzzy Zelda sicher: %.3f", score);
	f = cf_gtdb_index_fuzzy(&ix, "Pikmin", &score);
	CHECK(f == NULL, "fuzzy Pikmin: nichts");

	/* Laden aus Text */
	cf_gtdb_index_free(&ix);
	{
		char two[1024];
		int n1 = cf_gtdb_line(&c.g[0], two, sizeof(two));
		cf_gtdb_line(&c.g[1], two + n1, sizeof(two) - (size_t)n1);
		EQ_U64(cf_gtdb_index_load(&ix, two), 2u);
		CHECK(cf_gtdb_index_find(&ix, "GZLP01") != NULL, "load+find");
	}
	cf_gtdb_index_free(&ix);
}

/* ------------------------------------------------------------------ */
/* Katalog: Jahr und Genre                                            */
/* ------------------------------------------------------------------ */

static void mk_entry(cf_entry *e, const char *title, unsigned year,
                     const char *genre)
{
	memset(e, 0, sizeof(*e));
	snprintf(e->path, sizeof(e->path), "%s.iso", title);
	snprintf(e->title, sizeof(e->title), "%s", title);
	snprintf(e->genre, sizeof(e->genre), "%s", genre);
	e->year = (uint16_t)year;
	e->is_image = 1;
	e->size = 1000;
}

static void test_catalog_meta(void)
{
	cf_catalog *c = cf_catalog_new();
	cf_entry e;
	char g[16][24];
	size_t n;

	printf("cf_catalog Jahr/Genre\n");
	CHECK(c != NULL, "new");

	mk_entry(&e, "Metroid Prime", 2002, "action,adventure,first-person shooter");
	cf_catalog_add(c, &e);
	mk_entry(&e, "Wind Waker", 2003, "action,adventure");
	cf_catalog_add(c, &e);
	mk_entry(&e, "Unbekannt", 0, "");
	cf_catalog_add(c, &e);
	mk_entry(&e, "Pikmin", 2001, "strategy");
	cf_catalog_add(c, &e);
	mk_entry(&e, "Mischmasch", 2004, "action-adventure");
	cf_catalog_add(c, &e);

	/* Jahr aufsteigend, ohne Jahr ans Ende */
	cf_catalog_set_sort(c, CF_SORT_YEAR_ASC);
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 5u);
	EQ_STR(cf_view_at(c, 0)->title, "Pikmin");
	EQ_STR(cf_view_at(c, 3)->title, "Mischmasch");
	EQ_STR(cf_view_at(c, 4)->title, "Unbekannt");

	/* absteigend: ohne Jahr trotzdem am Ende */
	cf_catalog_set_sort(c, CF_SORT_YEAR_DESC);
	cf_catalog_refresh(c);
	EQ_STR(cf_view_at(c, 0)->title, "Mischmasch");
	EQ_STR(cf_view_at(c, 3)->title, "Pikmin");
	EQ_STR(cf_view_at(c, 4)->title, "Unbekannt");

	/* Genre als ganzes Feld: "action" trifft nicht "action-adventure" */
	cf_catalog_set_genre(c, "action");
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 2u);
	cf_catalog_set_genre(c, "strategy");
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 1u);
	EQ_STR(cf_view_at(c, 0)->title, "Pikmin");
	cf_catalog_set_genre(c, "");
	cf_catalog_refresh(c);
	EQ_U64(cf_view_count(c), 5u);

	/* Genre-Liste: alphabetisch, ohne Doppelte */
	n = cf_catalog_genres(c, g, 16);
	EQ_U64(n, 5u);
	EQ_STR(g[0], "action");
	EQ_STR(g[1], "action-adventure");
	EQ_STR(g[2], "adventure");
	EQ_STR(g[3], "first-person shooter");
	EQ_STR(g[4], "strategy");

	cf_catalog_free(c);
}

static void test_qr(void)
{
	/* Beispiel "HELLO WORLD" 1-M aus der gaengigen QR-Anleitung */
	static const uint8_t d[16] = { 32, 91, 11, 120, 209, 114, 220, 77,
	                               67, 64, 236, 17, 236, 17, 236, 17 };
	static const uint8_t want[10] = { 196, 35, 39, 119, 235, 215, 231,
	                                  226, 93, 23 };
	uint8_t ecc[10];
	static cf_qr q;
	const char *url = "https://discord.gg/EqUpCbGM5p";
	int ok, i;

	printf("cf_qr\n");
	cf_qr_rs(d, 16, ecc, 10);
	CHECK(!memcmp(ecc, want, 10), "Reed-Solomon");
	EQ_U64(cf_qr_format_bits(0), 0x5412u);
	EQ_U64(cf_qr_format_bits(1), 0x5125u);

	CHECK(cf_qr_encode(&q, url, strlen(url)), "kodiert");
	EQ_U64((uint64_t)q.version, 3u);
	EQ_U64((uint64_t)q.size, 29u);
	CHECK(q.m[q.size - 8][8] == 1, "dunkles Modul");
	ok = 1;
	for (i = 0; i < 7; i++)
		ok &= q.m[0][i] && q.m[i][0] && q.m[0][q.size - 1 - i] &&
		      q.m[q.size - 1 - i][0];
	CHECK(ok, "Finder-Rahmen");
	CHECK(q.m[6][8] == 1 && q.m[6][9] == 0, "Timing");
	CHECK(!cf_qr_encode(&q, d, 300), "zu lang");
}

int main(void)
{
	printf("CubeFetch Kern-Tests\n");
	printf("====================\n\n");

	test_crc32();
	test_crc32_combine();
	test_fuzzy();
	test_gametdb();
	test_catalog_meta();
	test_str();
	test_json();
	test_sources();
	test_autoidx();
	test_manifest();
	test_queue();
	test_qr();


	printf("\n%d Pruefungen, %d Fehler\n", g_run, g_fail);
	return g_fail ? 1 : 0;
}
