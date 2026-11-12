#include "platform/cf_dns.h"

#include <stdio.h>
#include <string.h>

#include <gccore.h>
#include <network.h>
#include <ogc/lwp_watchdog.h>

#define DNS_PORT      53
#define DNS_TIMEOUT   3000   /* Millisekunden je Versuch */
#define DNS_TRIES     3
#define DNS_CACHE     8
#define DNS_HOST_MAX  160

static char g_server[16] = "";

struct cache_ent {
	char host[DNS_HOST_MAX];
	struct in_addr addr;
};

static struct cache_ent g_cache[DNS_CACHE];
static int              g_cache_next;

void cf_dns_set_server(const char *server)
{
	snprintf(g_server, sizeof(g_server), "%s", server ? server : "");
}

const char *cf_dns_server(void) { return g_server; }

void cf_dns_flush(void)
{
	memset(g_cache, 0, sizeof(g_cache));
	g_cache_next = 0;
}

/* Erkennt und wandelt eine reine Zahlenadresse. 1 wenn es eine war. */
static int parse_dotted(const char *s, struct in_addr *out)
{
	unsigned a, b, c, d;
	char extra;

	if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &extra) != 4)
		return 0;
	if (a > 255 || b > 255 || c > 255 || d > 255)
		return 0;

	out->s_addr = htonl((a << 24) | (b << 16) | (c << 8) | d);
	return 1;
}

static int cache_get(const char *host, struct in_addr *out)
{
	int i;

	for (i = 0; i < DNS_CACHE; i++) {
		if (g_cache[i].host[0] && !strcmp(g_cache[i].host, host)) {
			*out = g_cache[i].addr;
			return 1;
		}
	}
	return 0;
}

static void cache_put(const char *host, struct in_addr addr)
{
	struct cache_ent *e = &g_cache[g_cache_next];

	g_cache_next = (g_cache_next + 1) % DNS_CACHE;

	snprintf(e->host, sizeof(e->host), "%s", host);
	e->addr = addr;
}

/* Schreibt host als Folge von Laengen-Labels. Gibt die Laenge zurueck
 * oder 0, wenn der Name nicht passt. */
static size_t encode_qname(unsigned char *out, size_t outsz, const char *host)
{
	size_t o = 0;
	const char *p = host;

	while (*p) {
		const char *dot = strchr(p, '.');
		size_t l = dot ? (size_t)(dot - p) : strlen(p);

		if (l == 0 || l > 63)
			return 0;
		if (o + 1 + l + 1 > outsz)
			return 0;

		out[o++] = (unsigned char)l;
		memcpy(out + o, p, l);
		o += l;

		if (!dot)
			break;
		p = dot + 1;
	}

	if (o + 1 > outsz)
		return 0;
	out[o++] = 0;
	return o;
}

/* Ueberspringt einen Namen an Position pos. Gibt die Position dahinter
 * zurueck oder 0 bei Unsinn. Namenskompression: ein Zeiger beendet den
 * Namen, danach folgt direkt das naechste Feld. */
static size_t skip_name(const unsigned char *buf, size_t len, size_t pos)
{
	int guard = 0;

	while (pos < len && guard++ < 128) {
		unsigned char l = buf[pos];

		if (l == 0)
			return pos + 1;

		if ((l & 0xC0) == 0xC0)
			return pos + 2;

		pos += 1 + l;
	}
	return 0;
}

static int query_once(s32 s, const struct sockaddr_in *srv,
                      const char *host, u16 id, struct in_addr *out)
{
	unsigned char req[DNS_HOST_MAX + 32];
	unsigned char rsp[768];
	size_t qn, reqlen, pos;
	u16 qd, an;
	int i;
	s32 n;
	fd_set rf;
	struct timeval tv;
	struct sockaddr_in from;
	socklen_t fromlen = sizeof(from);

	/* --- Anfrage --- */
	memset(req, 0, sizeof(req));
	req[0] = (unsigned char)(id >> 8);
	req[1] = (unsigned char)(id & 0xFF);
	req[2] = 0x01;   /* Standardanfrage, Rekursion erwuenscht */
	req[3] = 0x00;
	req[5] = 0x01;   /* qdcount = 1 */

	qn = encode_qname(req + 12, sizeof(req) - 12 - 4, host);
	if (!qn)
		return 0;

	reqlen = 12 + qn;
	req[reqlen++] = 0x00; req[reqlen++] = 0x01;   /* Typ A */
	req[reqlen++] = 0x00; req[reqlen++] = 0x01;   /* Klasse IN */

	if (net_sendto(s, req, (s32)reqlen, 0,
	               (struct sockaddr *)srv, sizeof(*srv)) < 0)
		return 0;

	/* --- Antwort --- */
	FD_ZERO(&rf);
	FD_SET(s, &rf);
	tv.tv_sec = DNS_TIMEOUT / 1000;
	tv.tv_usec = (DNS_TIMEOUT % 1000) * 1000;

	if (net_select(s + 1, &rf, NULL, NULL, &tv) <= 0)
		return 0;

	n = net_recvfrom(s, rsp, sizeof(rsp), 0,
	                 (struct sockaddr *)&from, &fromlen);
	if (n < 12)
		return 0;

	/* Gehoert die Antwort zu unserer Frage? */
	if (rsp[0] != (unsigned char)(id >> 8) ||
	    rsp[1] != (unsigned char)(id & 0xFF))
		return 0;
	if ((rsp[2] & 0x80) == 0)      /* kein Antwort-Bit */
		return 0;
	if ((rsp[3] & 0x0F) != 0)      /* RCODE ungleich 0 */
		return 0;

	qd = (u16)((rsp[4] << 8) | rsp[5]);
	an = (u16)((rsp[6] << 8) | rsp[7]);
	if (an == 0)
		return 0;

	pos = 12;
	for (i = 0; i < (int)qd; i++) {
		pos = skip_name(rsp, (size_t)n, pos);
		if (!pos || pos + 4 > (size_t)n)
			return 0;
		pos += 4;
	}

	for (i = 0; i < (int)an; i++) {
		u16 type, cls, rdlen;

		pos = skip_name(rsp, (size_t)n, pos);
		if (!pos || pos + 10 > (size_t)n)
			return 0;

		type  = (u16)((rsp[pos] << 8) | rsp[pos + 1]);
		cls   = (u16)((rsp[pos + 2] << 8) | rsp[pos + 3]);
		rdlen = (u16)((rsp[pos + 8] << 8) | rsp[pos + 9]);
		pos += 10;

		if (pos + rdlen > (size_t)n)
			return 0;

		/* Typ 1 ist A, Typ 5 ist CNAME. Bei CNAME weiterlesen: der
		 * passende A-Record steht ueblicherweise dahinter. */
		if (type == 1 && cls == 1 && rdlen == 4) {
			memcpy(&out->s_addr, rsp + pos, 4);
			return 1;
		}

		pos += rdlen;
	}

	return 0;
}

int cf_dns_resolve(const char *host, struct in_addr *out)
{
	struct sockaddr_in srv;
	s32 s;
	int try_i;
	int ok = 0;

	if (!host || !*host)
		return 0;

	if (parse_dotted(host, out))
		return 1;

	if (cache_get(host, out))
		return 1;

	if (!g_server[0])
		return 0;

	memset(&srv, 0, sizeof(srv));
	srv.sin_family = AF_INET;
	srv.sin_port = htons(DNS_PORT);
	if (!parse_dotted(g_server, &srv.sin_addr))
		return 0;

	s = net_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
		return 0;

	for (try_i = 0; try_i < DNS_TRIES && !ok; try_i++) {
		/* Die Kennung je Versuch wechseln, damit eine verspaetete
		 * Antwort auf den vorigen Versuch nicht durchgeht. */
		u16 id = (u16)(0x4346u + try_i * 7 + (unsigned)gettick());

		ok = query_once(s, &srv, host, id, out);
	}

	net_close(s);

	if (ok)
		cache_put(host, *out);
	return ok;
}
