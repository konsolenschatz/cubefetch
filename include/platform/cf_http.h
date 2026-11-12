/* HTTP/1.1-Client ohne TLS.
 *
 * Der GameCube bringt keine TLS-Implementierung mit, darum wird nur
 * "http://" bedient. Geprueft ist, dass art.gametdb.com und typische Server
 * ueber Port 80 antworten und der Redirect auf die Datei-Knoten ebenfalls
 * http bleibt - siehe docs/SOURCE-FORMAT.md.
 *
 * Unterstuetzt wird, was dafuer gebraucht wird: Range-Requests fuer das
 * Fortsetzen, Redirects und "Transfer-Encoding: chunked", weil die
 * manche Listen ohne Content-Length kommen.
 */
#ifndef CF_HTTP_H
#define CF_HTTP_H

#include <stddef.h>
#include <stdint.h>

#define CF_HTTP_EDNS      (-1)
#define CF_HTTP_ECONN     (-2)
#define CF_HTTP_ETIMEOUT  (-3)
#define CF_HTTP_EPROTO    (-4)
#define CF_HTTP_EMEM      (-5)
#define CF_HTTP_ECANCEL   (-6)
#define CF_HTTP_ETLS      (-7)
#define CF_HTTP_EREDIR    (-8)
#define CF_HTTP_ESINK     (-9)
#define CF_HTTP_EURL     (-10)
#define CF_HTTP_ETOOBIG  (-11)
#define CF_HTTP_EDONE    (-12)   /* Bereich vollstaendig gelesen */
#define CF_HTTP_ENORANGE (-13)   /* Server bedient keine Bereiche */
#define CF_HTTP_EBADRANGE (-14)  /* 206, aber ein anderer Bereich */

typedef struct {
	int      status;
	uint64_t length;   /* Content-Length, 0 bei chunked oder unbekannt */
	uint64_t total;    /* Gesamtgroesse aus Content-Range, sonst length */
	uint64_t range_from;/* erstes Byte aus Content-Range */
	int      has_range;/* Content-Range war vorhanden und lesbar */
	int      conn_close;/* Gegenstelle hat "Connection: close" gesagt */
	int      chunked;
	int      ranges;   /* Server hat Accept-Ranges: bytes gemeldet */
	char     final_url[1024];
} cf_http_info;

/* Rueckgabe: 0 weitermachen, negativ bricht den Abruf ab. */
typedef int (*cf_http_sink)(void *ctx, const void *buf, size_t len);

/* Laedt vollstaendig in den Speicher. *out kommt von malloc und ist
 * nullterminiert; der Aufrufer gibt es frei. limit begrenzt die Groesse. */
int cf_http_get_mem(const char *url, size_t limit,
                    char **out, size_t *out_len, cf_http_info *info);

/* Laedt ab offset und schiebt alles in die Senke. offset 0 schickt keinen
 * Range-Header. cancel darf NULL sein; zeigt es auf einen Wert ungleich 0,
 * wird abgebrochen. */
int cf_http_get_range(const char *url, uint64_t offset,
                      cf_http_sink sink, void *ctx,
                      cf_http_info *info, volatile int *cancel);

/* Holt genau den Bereich von..bis (beide einschliesslich). Fuer die
 * Disc-ID-Probe, die mit 1 KB auskommt - siehe docs/METADATA.md. */
int cf_http_get_slice(const char *url, uint64_t from, uint64_t to,
                      cf_http_sink sink, void *ctx, cf_http_info *info);

/* Nur die Kopfzeilen holen, fuer Groesse und Range-Faehigkeit. */
int cf_http_head(const char *url, cf_http_info *info);

/* ------------------------------------------------------------------ */
/* Offene Bereichsanfrage                                             */
/* ------------------------------------------------------------------ */

/* Mehrere Anfragen gleichzeitig offen zu halten ist beim GameCube keine
 * Spielerei, sondern der einzige Weg zu brauchbarem Durchsatz: das lwIP
 * von libogc2 ist mit einem Empfangsfenster von 2920 Bytes uebersetzt
 * und kennt kein Window Scaling. Mehr als 2920 Bytes sind pro Verbindung
 * nie unterwegs, der Durchsatz ist damit 2920 Bytes geteilt durch die
 * Umlaufzeit - bei 210 ms zur US-Westkueste also 14 kB/s, egal wie
 * schnell die Leitung ist. Jede weitere Verbindung bringt ihr eigenes
 * Fenster mit. Hintergrund in docs/ARCHITECTURE.md.
 *
 * Diese Schnittstelle ist deshalb nicht blockierend: der Aufrufer haelt
 * mehrere Anfragen offen, fragt mit net_select, welche Daten haben, und
 * holt sie ab. */
typedef struct cf_http_req cf_http_req;

/* Oeffnet die Verbindung, folgt Redirects und liest die Kopfzeilen -
 * das blockiert, dauert aber nur eine Umlaufzeit.
 *
 * from..to sind einschliesslich; to == 0 heisst "bis zum Dateiende".
 * Verlangt wird eine Bereichsantwort, sobald from oder to gesetzt sind:
 * antwortet der Server mit 200, kommt CF_HTTP_ENORANGE zurueck. */
int cf_http_req_open(const char *url, uint64_t from, uint64_t to,
                     cf_http_info *info, cf_http_req **out);

/* Der Sockel, fuer net_select im Aufrufer. */
int cf_http_req_fd(const cf_http_req *r);

/* Holt, was anliegt, ohne zu warten.
 *   > 0               so viele Bytes stehen in dst
 *   0                 gerade nichts da
 *   CF_HTTP_EDONE     der Bereich ist vollstaendig
 *   sonst             Fehler, die Verbindung ist hin */
int cf_http_req_read(cf_http_req *r, void *dst, size_t max);

void cf_http_req_close(cf_http_req *r);

/* Keep-Alive: naechste Bereichsanfrage auf derselben Verbindung.
 *
 * Jede neue Verbindung kostet DNS, TCP-Handshake, Anfrage und Warten auf
 * die Kopfzeilen - rund zwei Umlaufzeiten, und waehrenddessen blockiert
 * der Aufrufer. Bei Bloecken von 1 MB und einem Dutzend Verbindungen
 * staute das alle anderen jedes Mal auf. Hier wird nur die Anfrage
 * geschickt; die Kopfzeilen liest cf_http_req_read, sobald sie
 * angekommen sind, wie jeden anderen Datenblock auch.
 *
 * Geht nur, wenn der vorige Bereich vollstaendig gelesen ist und die
 * Gegenstelle nicht "Connection: close" gesagt hat
 * (cf_http_req_reusable). 0 bei Erfolg; sonst schliessen und neu oeffnen. */
int cf_http_req_again(cf_http_req *r, uint64_t from, uint64_t to);
int cf_http_req_reusable(const cf_http_req *r);

const char *cf_http_strerror(int code);

#endif
