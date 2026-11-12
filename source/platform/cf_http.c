#include "platform/cf_http.h"
#include "platform/cf_dns.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <network.h>

#define CF_HTTP_MAX_REDIRECT 6
#define RECV_TIMEOUT_MS      20000
/* Pro offener Verbindung einmal da. Groesser als das Empfangsfenster
 * von lwIP (2920 Bytes) muss es sein, damit ein Lesevorgang das Fenster
 * immer vollstaendig freigibt; viel groesser bringt nichts. */
#define RBUF_SZ              4096

#define CF_UA "CubeFetch/0.1 (Nintendo GameCube)"

/* ------------------------------------------------------------------ */
/* Adresse zerlegen                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
	char host[160];
	char path[768];
	unsigned port;
} cf_url;

static int url_split(const char *url, cf_url *u)
{
	const char *p, *slash;
	char *colon;
	size_t hl;

	if (!url)
		return CF_HTTP_EURL;

	if (!strncmp(url, "https://", 8))
		return CF_HTTP_ETLS;

	if (!strncmp(url, "http://", 7))
		p = url + 7;
	else if (strstr(url, "://"))
		return CF_HTTP_EURL;
	else
		p = url;

	slash = strchr(p, '/');
	hl = slash ? (size_t)(slash - p) : strlen(p);
	if (hl == 0 || hl >= sizeof(u->host))
		return CF_HTTP_EURL;

	memcpy(u->host, p, hl);
	u->host[hl] = '\0';

	u->port = 80;
	colon = strchr(u->host, ':');
	if (colon) {
		unsigned v = (unsigned)strtoul(colon + 1, NULL, 10);
		if (v == 0 || v > 65535)
			return CF_HTTP_EURL;
		u->port = v;
		*colon = '\0';
	}

	snprintf(u->path, sizeof(u->path), "%s", slash ? slash : "/");
	return 0;
}

/* Haengt an dst an und meldet, wenn der Platz nicht reicht. Ein still
 * abgeschnittenes Ziel waere schlimmer als ein Fehler: es zeigt auf eine
 * andere Datei. */
static int url_put(char *dst, size_t dstsz, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(dst, dstsz, fmt, ap);
	va_end(ap);

	if (n < 0 || (size_t)n >= dstsz)
		return CF_HTTP_EURL;
	return 0;
}

/* Setzt eine moeglicherweise relative Location-Angabe gegen die bisherige
 * Adresse auf. */
static int url_resolve(char *dst, size_t dstsz,
                       const cf_url *base, const char *loc)
{
	if (!strncmp(loc, "http://", 7) || !strncmp(loc, "https://", 8))
		return url_put(dst, dstsz, "%s", loc);

	if (loc[0] == '/') {
		if (base->port == 80)
			return url_put(dst, dstsz, "http://%s%s", base->host, loc);
		return url_put(dst, dstsz, "http://%s:%u%s",
		               base->host, base->port, loc);
	}

	/* Relativ zum Verzeichnis des bisherigen Pfads. */
	{
		char dir[768];
		char *s;

		if (url_put(dir, sizeof(dir), "%s", base->path) < 0)
			return CF_HTTP_EURL;
		s = strrchr(dir, '/');
		if (s)
			s[1] = '\0';
		else
			dir[0] = '\0';

		if (base->port == 80)
			return url_put(dst, dstsz, "http://%s/%s%s",
			               base->host, dir[0] == '/' ? dir + 1 : dir, loc);
		return url_put(dst, dstsz, "http://%s:%u/%s%s",
		               base->host, base->port,
		               dir[0] == '/' ? dir + 1 : dir, loc);
	}
}

/* ------------------------------------------------------------------ */
/* Gepufferter Leser                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
	s32    s;
	char   buf[RBUF_SZ];
	size_t have;
	size_t pos;
	volatile int *cancel;
} reader;

static int wait_readable(s32 s, int ms)
{
	fd_set r;
	struct timeval tv;

	FD_ZERO(&r);
	FD_SET(s, &r);
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;

	return net_select(s + 1, &r, NULL, NULL, &tv);
}

/* 0 = Daten da, 1 = Verbindung zu, negativ = Fehler */
static int rd_fill(reader *r)
{
	s32 n;

	if (r->cancel && *r->cancel)
		return CF_HTTP_ECANCEL;

	if (r->pos < r->have)
		return 0;

	r->pos = 0;
	r->have = 0;

	if (wait_readable(r->s, RECV_TIMEOUT_MS) <= 0)
		return CF_HTTP_ETIMEOUT;

	n = net_recv(r->s, r->buf, sizeof(r->buf), 0);
	if (n == 0)
		return 1;
	if (n < 0)
		return CF_HTTP_ECONN;

	r->have = (size_t)n;
	return 0;
}

/* Liest eine Zeile bis zum Zeilenende. CR und LF landen nicht in dst. */
static int rd_line(reader *r, char *dst, size_t dstsz)
{
	size_t o = 0;

	for (;;) {
		char c;
		int e;

		if (r->pos >= r->have) {
			e = rd_fill(r);
			if (e < 0)
				return e;
			if (e == 1) {
				if (o == 0)
					return CF_HTTP_EPROTO;
				break;
			}
		}

		c = r->buf[r->pos++];
		if (c == '\n')
			break;
		if (c == '\r')
			continue;
		if (o + 1 < dstsz)
			dst[o++] = c;
	}

	dst[o] = '\0';
	return 0;
}

/* Holt bis zu max Bytes. Gibt die Anzahl zurueck, 0 bei
 * Verbindungsende, negativ bei Fehler. */
static int rd_body(reader *r, void *dst, size_t max)
{
	size_t n;
	int e;

	if (r->pos >= r->have) {
		e = rd_fill(r);
		if (e < 0)
			return e;
		if (e == 1)
			return 0;
	}

	n = r->have - r->pos;
	if (n > max)
		n = max;
	memcpy(dst, r->buf + r->pos, n);
	r->pos += n;
	return (int)n;
}

/* ------------------------------------------------------------------ */
/* Verbindung                                                         */
/* ------------------------------------------------------------------ */

static int tcp_open(const cf_url *u, s32 *out)
{
	struct in_addr ip;
	struct sockaddr_in sa;
	s32 s;

	/* Eigener Resolver: libogc bringt fuer den GameCube keine
	 * Namensauflösung mit. Begruendung in cf_dns.h. */
	if (!cf_dns_resolve(u->host, &ip))
		return CF_HTTP_EDNS;

	s = net_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0)
		return CF_HTTP_ECONN;

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((u16)u->port);
	sa.sin_addr = ip;

	if (net_connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		net_close(s);
		return CF_HTTP_ECONN;
	}

	*out = s;
	return 0;
}

static int send_all(s32 s, const char *data, size_t len)
{
	while (len) {
		s32 n = net_send(s, data, (s32)len, 0);
		if (n <= 0)
			return CF_HTTP_ECONN;
		data += n;
		len -= (size_t)n;
	}
	return 0;
}

/* end == 0 heisst "bis zum Dateiende". */
static int send_request(s32 s, const cf_url *u, const char *method,
                        uint64_t offset, uint64_t end, int keepalive)
{
	char req[1100];
	int n, m;

	n = snprintf(req, sizeof(req),
	             "%s %s HTTP/1.1\r\n"
	             "Host: %s\r\n"
	             "User-Agent: " CF_UA "\r\n"
	             "Accept: */*\r\n"
	             "Accept-Encoding: identity\r\n"
	             "Connection: %s\r\n",
	             method, u->path, u->host,
	             keepalive ? "keep-alive" : "close");
	if (n < 0 || (size_t)n >= sizeof(req))
		return CF_HTTP_EURL;

	if (offset || end) {
		if (end)
			m = snprintf(req + n, sizeof(req) - (size_t)n,
			             "Range: bytes=%llu-%llu\r\n",
			             (unsigned long long)offset,
			             (unsigned long long)end);
		else
			m = snprintf(req + n, sizeof(req) - (size_t)n,
			             "Range: bytes=%llu-\r\n",
			             (unsigned long long)offset);
		if (m < 0 || (size_t)(n + m) >= sizeof(req))
			return CF_HTTP_EURL;
		n += m;
	}

	m = snprintf(req + n, sizeof(req) - (size_t)n, "\r\n");
	if (m < 0 || (size_t)(n + m) >= sizeof(req))
		return CF_HTTP_EURL;
	n += m;

	return send_all(s, req, (size_t)n);
}

/* ------------------------------------------------------------------ */
/* Kopfzeilen                                                         */
/* ------------------------------------------------------------------ */

static char lower_ascii(char c)
{
	if (c >= 'A' && c <= 'Z')
		return (char)(c - 'A' + 'a');
	return c;
}

static int hdr_is(const char *line, const char *name, const char **val)
{
	size_t n = strlen(name);
	size_t i;
	const char *p;

	for (i = 0; i < n; i++) {
		if (lower_ascii(line[i]) != lower_ascii(name[i]))
			return 0;
	}
	if (line[n] != ':')
		return 0;

	p = line + n + 1;
	while (*p == ' ' || *p == '\t')
		p++;
	*val = p;
	return 1;
}

static int contains_nocase(const char *hay, const char *needle)
{
	size_t nl = strlen(needle);

	for (; *hay; hay++) {
		size_t i;
		for (i = 0; i < nl; i++) {
			if (!hay[i])
				return 0;
			if (lower_ascii(hay[i]) != lower_ascii(needle[i]))
				break;
		}
		if (i == nl)
			return 1;
	}
	return 0;
}

static int read_headers(reader *r, cf_http_info *info,
                        char *location, size_t locsz)
{
	char line[1024];
	const char *sp;
	int e;

	location[0] = '\0';
	info->length = 0;
	info->total = 0;
	info->chunked = 0;
	info->ranges = 0;
	info->status = 0;
	info->conn_close = 0;
	info->has_range = 0;

	e = rd_line(r, line, sizeof(line));
	if (e < 0)
		return e;

	if (strncmp(line, "HTTP/", 5))
		return CF_HTTP_EPROTO;

	sp = strchr(line, ' ');
	if (!sp)
		return CF_HTTP_EPROTO;
	info->status = (int)strtol(sp + 1, NULL, 10);

	for (;;) {
		const char *v;

		e = rd_line(r, line, sizeof(line));
		if (e < 0)
			return e;
		if (!line[0])
			break;

		if (hdr_is(line, "Content-Length", &v)) {
			info->length = (uint64_t)strtoull(v, NULL, 10);
		} else if (hdr_is(line, "Transfer-Encoding", &v)) {
			if (contains_nocase(v, "chunked"))
				info->chunked = 1;
		} else if (hdr_is(line, "Accept-Ranges", &v)) {
			if (contains_nocase(v, "bytes"))
				info->ranges = 1;
		} else if (hdr_is(line, "Connection", &v)) {
			if (contains_nocase(v, "close"))
				info->conn_close = 1;
		} else if (hdr_is(line, "Location", &v)) {
			snprintf(location, locsz, "%s", v);
		} else if (hdr_is(line, "Content-Range", &v)) {
			/* "bytes 0-99/1459978240" */
			const char *slash = strchr(v, '/');
			const char *num = v;

			if (slash)
				info->total = (uint64_t)strtoull(slash + 1, NULL, 10);

			/* Der Anfang muss mitgelesen werden. Antwortet ein
			 * Server mit 206, aber einem anderen Bereich als
			 * angefragt, landen die Bytes sonst an der falschen
			 * Stelle in der Datei - und auffallen wuerde das erst
			 * Stunden spaeter, wenn die Quelle ueberhaupt eine
			 * Pruefsumme mitbringt. */
			while (*num && (*num < '0' || *num > '9'))
				num++;
			if (*num) {
				info->range_from =
					(uint64_t)strtoull(num, NULL, 10);
				info->has_range = 1;
			}
		}
	}

	if (!info->total)
		info->total = info->length;

	return 0;
}

/* ------------------------------------------------------------------ */
/* Koerper                                                            */
/* ------------------------------------------------------------------ */

static int body_identity(reader *r, uint64_t len, int have_len,
                         cf_http_sink sink, void *ctx)
{
	char tmp[1024];
	uint64_t done = 0;

	for (;;) {
		size_t want = sizeof(tmp);
		int n;

		if (have_len) {
			if (done >= len)
				break;
			if (len - done < (uint64_t)want)
				want = (size_t)(len - done);
		}

		n = rd_body(r, tmp, want);
		if (n < 0)
			return n;
		if (n == 0)
			break;

		if (sink(ctx, tmp, (size_t)n) < 0)
			return CF_HTTP_ESINK;
		done += (uint64_t)n;
	}

	if (have_len && done < len)
		return CF_HTTP_EPROTO;
	return 0;
}

static int body_chunked(reader *r, cf_http_sink sink, void *ctx)
{
	char line[64];
	char tmp[1024];

	for (;;) {
		unsigned long sz;
		int e;

		e = rd_line(r, line, sizeof(line));
		if (e < 0)
			return e;

		sz = strtoul(line, NULL, 16);
		if (sz == 0)
			break;

		while (sz) {
			size_t want = sizeof(tmp);
			int n;

			if ((unsigned long)want > sz)
				want = (size_t)sz;
			n = rd_body(r, tmp, want);
			if (n < 0)
				return n;
			if (n == 0)
				return CF_HTTP_EPROTO;

			if (sink(ctx, tmp, (size_t)n) < 0)
				return CF_HTTP_ESINK;
			sz -= (unsigned long)n;
		}

		/* Zeilenende hinter dem Block */
		e = rd_line(r, line, sizeof(line));
		if (e < 0)
			return e;
	}

	return 0;
}

/* ------------------------------------------------------------------ */
/* Abruf mit Weiterleitungen                                          */
/* ------------------------------------------------------------------ */

static int fetch(const char *url, const char *method,
                 uint64_t offset, uint64_t end,
                 cf_http_sink sink, void *ctx, cf_http_info *info,
                 volatile int *cancel)
{
	char cur[1024];
	int hop;

	if (url_put(cur, sizeof(cur), "%s", url) < 0)
		return CF_HTTP_EURL;

	for (hop = 0; hop <= CF_HTTP_MAX_REDIRECT; hop++) {
		cf_url u;
		s32 s = -1;
		reader r;
		char loc[512];
		int e;

		e = url_split(cur, &u);
		if (e < 0)
			return e;

		e = tcp_open(&u, &s);
		if (e < 0)
			return e;

		memset(&r, 0, sizeof(r));
		r.s = s;
		r.cancel = cancel;

		e = send_request(s, &u, method, offset, end, 0);
		if (e < 0) {
			net_close(s);
			return e;
		}

		e = read_headers(&r, info, loc, sizeof(loc));
		if (e < 0) {
			net_close(s);
			return e;
		}

		snprintf(info->final_url, sizeof(info->final_url), "%s", cur);

		if (info->status >= 300 && info->status < 400 && loc[0]) {
			char next[1024];

			net_close(s);
			e = url_resolve(next, sizeof(next), &u, loc);
			if (e < 0)
				return e;
			memcpy(cur, next, sizeof(cur));
			continue;
		}

		if (info->status != 200 && info->status != 206) {
			net_close(s);
			return info->status;
		}

		if (!strcmp(method, "HEAD")) {
			net_close(s);
			return info->status;
		}

		if (info->chunked)
			e = body_chunked(&r, sink, ctx);
		else
			e = body_identity(&r, info->length, info->length != 0,
			                  sink, ctx);

		net_close(s);
		if (e < 0)
			return e;
		return info->status;
	}

	return CF_HTTP_EREDIR;
}

/* ------------------------------------------------------------------ */
/* Oeffentliche Schnittstelle                                         */
/* ------------------------------------------------------------------ */

typedef struct {
	char  *buf;
	size_t len;
	size_t cap;
	size_t limit;
} membuf;

static int mem_sink(void *vctx, const void *data, size_t len)
{
	membuf *m = vctx;

	if (m->len + len > m->limit)
		return -1;

	if (m->len + len + 1 > m->cap) {
		size_t cap = m->cap ? m->cap : 8192;
		char *p;

		while (cap < m->len + len + 1)
			cap *= 2;
		if (cap > m->limit + 1)
			cap = m->limit + 1;

		p = realloc(m->buf, cap);
		if (!p)
			return -1;
		m->buf = p;
		m->cap = cap;
	}

	memcpy(m->buf + m->len, data, len);
	m->len += len;
	m->buf[m->len] = '\0';
	return 0;
}

int cf_http_get_mem(const char *url, size_t limit,
                    char **out, size_t *out_len, cf_http_info *info)
{
	cf_http_info local;
	membuf m;
	int r;

	if (!info)
		info = &local;

	memset(&m, 0, sizeof(m));
	m.limit = limit;

	*out = NULL;
	if (out_len)
		*out_len = 0;

	r = fetch(url, "GET", 0, 0, mem_sink, &m, info, NULL);
	if (r != 200 && r != 206) {
		free(m.buf);
		return r;
	}

	if (!m.buf) {
		m.buf = malloc(1);
		if (!m.buf)
			return CF_HTTP_EMEM;
		m.buf[0] = '\0';
	}

	*out = m.buf;
	if (out_len)
		*out_len = m.len;
	return r;
}

int cf_http_get_range(const char *url, uint64_t offset,
                      cf_http_sink sink, void *ctx,
                      cf_http_info *info, volatile int *cancel)
{
	cf_http_info local;

	if (!info)
		info = &local;
	return fetch(url, "GET", offset, 0, sink, ctx, info, cancel);
}

static int null_sink(void *ctx, const void *buf, size_t len)
{
	(void)ctx;
	(void)buf;
	(void)len;
	return 0;
}

int cf_http_head(const char *url, cf_http_info *info)
{
	return fetch(url, "HEAD", 0, 0, null_sink, NULL, info, NULL);
}

const char *cf_http_strerror(int code)
{
	switch (code) {
	case CF_HTTP_EDNS:     return "Name nicht aufloesbar";
	case CF_HTTP_ECONN:    return "Verbindung fehlgeschlagen";
	case CF_HTTP_ETIMEOUT: return "Zeitueberschreitung";
	case CF_HTTP_EPROTO:   return "Antwort unverstaendlich";
	case CF_HTTP_EMEM:     return "Speicher voll";
	case CF_HTTP_ECANCEL:  return "Abgebrochen";
	case CF_HTTP_ETLS:     return "HTTPS wird nicht unterstuetzt";
	case CF_HTTP_EREDIR:   return "Zu viele Weiterleitungen";
	case CF_HTTP_ESINK:    return "Schreiben fehlgeschlagen";
	case CF_HTTP_EURL:     return "Adresse ungueltig";
	case CF_HTTP_ETOOBIG:  return "Antwort zu gross";
	case CF_HTTP_EDONE:    return "Fertig";
	case CF_HTTP_ENORANGE: return "Server bedient keine Bereiche";
	case CF_HTTP_EBADRANGE:return "Server schickt den falschen Bereich";
	default:               break;
	}
	return NULL;
}

int cf_http_get_slice(const char *url, uint64_t from, uint64_t to,
                      cf_http_sink sink, void *ctx, cf_http_info *info)
{
	cf_http_info local;

	if (!info)
		info = &local;
	if (to < from)
		return CF_HTTP_EURL;

	return fetch(url, "GET", from, to, sink, ctx, info, NULL);
}

/* ------------------------------------------------------------------ */
/* Offene Bereichsanfrage                                             */
/* ------------------------------------------------------------------ */

struct cf_http_req {
	reader   rd;
	uint64_t left;       /* noch erwartete Bytes */
	int      open_ended; /* ohne Content-Length: bis die Gegenstelle zumacht */
	int      done;

	/* Keep-Alive */
	cf_url   u;          /* Host und Pfad fuer die naechste Anfrage */
	int      keep;       /* Verbindung taugt fuer eine weitere Anfrage */
	int      waithdr;    /* Anfrage geschickt, Kopfzeilen stehen aus */
	uint64_t want_from;  /* zum Pruefen von Content-Range */
	int      want_range; /* es wurde ein Bereich angefragt */
};

int cf_http_req_open(const char *url, uint64_t from, uint64_t to,
                     cf_http_info *info, cf_http_req **out)
{
	char cur[1024];
	cf_http_req *q;
	int hop;

	*out = NULL;

	if (url_put(cur, sizeof(cur), "%s", url) < 0)
		return CF_HTTP_EURL;

	q = malloc(sizeof(*q));
	if (!q)
		return CF_HTTP_EMEM;

	for (hop = 0; hop <= CF_HTTP_MAX_REDIRECT; hop++) {
		cf_url u;
		s32 s = -1;
		char loc[512];
		int e;

		e = url_split(cur, &u);
		if (e < 0) {
			free(q);
			return e;
		}

		e = tcp_open(&u, &s);
		if (e < 0) {
			free(q);
			return e;
		}

		memset(q, 0, sizeof(*q));
		q->rd.s = s;

		e = send_request(s, &u, "GET", from, to, 1);
		if (e == 0)
			e = read_headers(&q->rd, info, loc, sizeof(loc));
		if (e < 0) {
			net_close(s);
			free(q);
			return e;
		}

		snprintf(info->final_url, sizeof(info->final_url), "%s", cur);
		q->u = u;

		if (info->status >= 300 && info->status < 400 && loc[0]) {
			char next[1024];

			net_close(s);
			e = url_resolve(next, sizeof(next), &u, loc);
			if (e < 0) {
				free(q);
				return e;
			}
			memcpy(cur, next, sizeof(cur));
			continue;
		}

		if (info->status != 200 && info->status != 206) {
			int st = info->status;
			net_close(s);
			free(q);
			return st;
		}

		/* Eine Bereichsanfrage, die mit 200 beantwortet wird, liefert
		 * die ganze Datei. Wer das weiterschreibt, setzt Byte 0 an die
		 * Bereichsgrenze - die Datei waere hin, und auffallen wuerde es
		 * erst Stunden spaeter beim Pruefsummenvergleich. */
		if ((from || to) && info->status == 200) {
			net_close(s);
			free(q);
			return CF_HTTP_ENORANGE;
		}

		/* 206, aber nicht der angefragte Bereich. Das verletzt das
		 * Protokoll; weiterschreiben wuerde die Datei verderben. */
		if (info->status == 206 &&
		    (!info->has_range || info->range_from != from)) {
			net_close(s);
			free(q);
			return CF_HTTP_EBADRANGE;
		}

		/* Chunked kommt bei Dateien nicht vor - so antworten nur
		 * Listen-Abrufe, und die laufen ueber
		 * cf_http_get_mem. Hier waere dafuer eine zweite
		 * Zustandsmaschine noetig; der Aufrufer meldet es stattdessen
		 * als Fehler. */
		if (info->chunked) {
			net_close(s);
			free(q);
			return CF_HTTP_EPROTO;
		}

		/* Ohne Content-Length laeuft der Koerper, bis die Gegenstelle
		 * zumacht. Das kann es nur bei einer einzigen Verbindung ueber
		 * die ganze Datei geben, denn ohne bekannte Groesse gibt es
		 * keine Bereiche. */
		q->open_ended = (info->length == 0);
		q->left = info->length;
		q->keep = !info->conn_close && !q->open_ended;
		*out = q;
		return info->status;
	}

	free(q);
	return CF_HTTP_EREDIR;
}

int cf_http_req_fd(const cf_http_req *r)
{
	return r ? (int)r->rd.s : -1;
}

/* Kopfzeilen einer Keep-Alive-Antwort lesen und pruefen. Gerufen erst,
 * wenn Daten anliegen; die Kopfzeilen einer Bereichsantwort sind ein paar
 * hundert Bytes und stehen praktisch immer im ersten Paket, das Lesen
 * blockiert also nicht spuerbar. */
static int req_headers(cf_http_req *r)
{
	cf_http_info info;
	char loc[512];
	int e;

	memset(&info, 0, sizeof(info));
	e = read_headers(&r->rd, &info, loc, sizeof(loc));
	if (e < 0)
		return e;

	if (info.status != 206 && !(info.status == 200 && !r->want_range))
		return info.status == 200 ? CF_HTTP_ENORANGE
		     : (info.status > 0 ? CF_HTTP_EPROTO : CF_HTTP_EPROTO);
	if (info.status == 206 &&
	    (!info.has_range || info.range_from != r->want_from))
		return CF_HTTP_EBADRANGE;
	if (info.chunked || info.length == 0)
		return CF_HTTP_EPROTO;

	r->left = info.length;
	r->keep = !info.conn_close;
	r->waithdr = 0;
	return 0;
}

int cf_http_req_again(cf_http_req *r, uint64_t from, uint64_t to)
{
	int e;

	if (!cf_http_req_reusable(r))
		return CF_HTTP_ECONN;

	e = send_request(r->rd.s, &r->u, "GET", from, to, 1);
	if (e < 0) {
		r->keep = 0;
		return e;
	}

	r->done = 0;
	r->left = 0;
	r->waithdr = 1;
	r->want_from = from;
	r->want_range = (from || to);
	return 0;
}

int cf_http_req_reusable(const cf_http_req *r)
{
	return r && r->keep && r->done && !r->waithdr && !r->open_ended &&
	       r->left == 0 && r->rd.pos >= r->rd.have;
}

int cf_http_req_read(cf_http_req *r, void *dst, size_t max)
{
	size_t n;

	if (!r || r->done)
		return CF_HTTP_EDONE;

	if (r->waithdr) {
		int e;

		if (r->rd.pos >= r->rd.have && wait_readable(r->rd.s, 0) <= 0)
			return 0;

		e = req_headers(r);
		if (e < 0) {
			r->keep = 0;
			return e;
		}
	}

	if (r->rd.pos >= r->rd.have) {
		int e;

		/* Null Millisekunden: nur nachsehen, nicht warten. Gewartet
		 * wird im Aufrufer, und zwar auf alle Verbindungen zugleich. */
		if (wait_readable(r->rd.s, 0) <= 0)
			return 0;

		e = rd_fill(&r->rd);
		if (e < 0)
			return e;
		if (e == 1) {
			/* Gegenstelle hat zugemacht. Fehlt noch etwas, ist das
			 * ein Abbruch und kein Dateiende. Weiterbenutzen geht
			 * danach in keinem Fall. */
			r->done = 1;
			r->keep = 0;
			if (r->open_ended)
				return CF_HTTP_EDONE;
			return r->left ? CF_HTTP_ECONN : CF_HTTP_EDONE;
		}
	}

	n = r->rd.have - r->rd.pos;
	if (n > max)
		n = max;
	if (!r->open_ended && (uint64_t)n > r->left)
		n = (size_t)r->left;

	memcpy(dst, r->rd.buf + r->rd.pos, n);
	r->rd.pos += n;

	if (!r->open_ended) {
		r->left -= n;
		if (r->left == 0)
			r->done = 1;
	}

	return (int)n;
}

void cf_http_req_close(cf_http_req *r)
{
	if (!r)
		return;
	if (r->rd.s >= 0)
		net_close(r->rd.s);
	free(r);
}
