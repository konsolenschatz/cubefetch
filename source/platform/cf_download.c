#include "platform/cf_download.h"
#include "platform/cf_http.h"
#include "platform/cf_storage.h"

#include "core/cf_crc32.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gccore.h>
#include <network.h>
#include <ogc/cond.h>
#include <ogc/lwp.h>
#include <ogc/lwp_watchdog.h>
#include <ogc/mutex.h>


/* Die Datei wird in Bloecke geteilt, die gleichzeitig geladen, aber
 * streng in der Reihenfolge geschrieben werden.
 *
 * Der Weg dahin steht in docs/ARCHITECTURE.md; kurz:
 *
 *  - Eine einzelne Verbindung ist auf das Empfangsfenster von lwIP
 *    geteilt durch die Umlaufzeit begrenzt (gemessen 174 kB/s). Es
 *    muessen also mehrere gleichzeitig laufen.
 *  - Wer gleichzeitig an verschiedene Stellen einer Datei schreibt,
 *    muss sie vorher in voller Groesse anlegen, denn FAT kennt keine
 *    Loecher und libfat fuellt jede Luecke mit Nullen. Das waren
 *    gemessene 20 Minuten je Image, ein Drittel der Gesamtzeit.
 *  - Also wird vorwaerts geschrieben: der Block, der als naechster
 *    dran ist, geht auf die Karte, alle anderen warten im Speicher.
 *    Die Datei waechst damit lueckenlos, nichts wird vorbelegt, und
 *    sie ist zu jedem Zeitpunkt ein gueltiger Anfang der fertigen
 *    Datei. Das macht auch das Fortsetzen wieder einfach: ein Zaehler
 *    und eine Pruefsumme.
 *
 * Preis: je Block eine eigene Bereichsanfrage, also ein
 * Verbindungsaufbau. Bei 1 MB Bloecken sind das wenige Prozent.
 */

/* Blockgroesse. Gross genug, dass der Verbindungsaufbau nicht
 * auffaellt, klein genug, dass die wartenden Bloecke in den Speicher
 * passen. */
#define BLK_SIZE      (1024ull * 1024)

/* Ein Platz mehr als Verbindungen: einer darf gerade geschrieben
 * werden, waehrend die anderen laden. */
#define SLOT_SPARE    1

/* So oft darf ein Block seine Verbindung neu aufbauen. Der Zaehler
 * wird zurueckgesetzt, sobald Daten kommen. */
#define BLK_TRIES     6

/* Kommt auf einer Verbindung so lange nichts, gilt sie als haengend. */
#define IDLE_MS       30000

/* Wartezeit in net_select. */
#define SELECT_MS     200

/* Wie oft die Oberflaeche gerufen und der Stand gesichert wird. */
#define CB_EVERY      (256ull * 1024)
#define SIDECAR_EVERY (8ull * 1024 * 1024)

/* Der Schreib-Thread braucht Platz fuer libfat. */
#define WSTACK        (32 * 1024)

#define BLK_NONE      ((uint64_t)-1)

typedef struct {
	uint64_t idx;          /* Blocknummer, BLK_NONE wenn der Platz frei ist */
	unsigned char *buf;
	size_t   want;         /* Groesse dieses Blocks */
	size_t   fill;
	int      ready;        /* vollstaendig geladen, wartet auf den Schreiber */

	cf_http_req *req;      /* laufende Anfrage */
	cf_http_req *conn;     /* freie Keep-Alive-Verbindung fuer den naechsten
	                        * Block dieses Platzes */
	int      tries;
	u64      t_act;
} blkslot;

typedef struct {
	FILE    *f;

	uint64_t total;
	uint64_t nblocks;
	uint64_t next_assign;  /* naechster zu verteilender Block */

	/* Vom Schreib-Thread gefuehrt, nur unter wmux lesen: */
	uint64_t next_write;   /* naechster zu schreibender Block */
	uint64_t written;      /* geschriebene Bytes */
	uint32_t crc;          /* Pruefsumme darueber, in Reihenfolge */
	int      werr;

	uint64_t done;         /* Abzug von written fuer die Anzeige */
	int      crc_usable;

	char     part[480];
	char     side[496];
	const char *url;
	char     src[1024];    /* Adresse nach den Redirects */

	int      nslots;
	blkslot  slot[CF_DL_MAX_CONN + SLOT_SPARE];

	uint64_t next_sidecar;
	uint64_t next_cb;

	/* Rate */
	u64      t_start;
	uint32_t rate;
	u64      t_last;
	uint64_t last_done;

	int      resumed;
	int      saw_data;    /* erstes Byte ist da: keine Aufbau-Meldungen mehr */

	mutex_t  wmux;
	cond_t   wjobs;        /* Arbeit da oder Schluss */
	cond_t   wfree;        /* Platz frei geworden */
	lwp_t    wthread;
	void    *wstack;
	int      wrunning;
	int      wstop;

	const cf_dl_opts *o;
} dlstate;

/* ------------------------------------------------------------------ */
/* Meldung an die Oberflaeche                                         */
/* ------------------------------------------------------------------ */

static void report(dlstate *st, int phase)
{
	cf_dl_progress p;
	int i;

	if (!st->o->cb)
		return;

	memset(&p, 0, sizeof(p));
	p.phase = phase;
	p.total = st->total;
	p.done = st->done;
	p.rate = st->rate;
	p.resumed = st->resumed;
	p.crc_usable = st->crc_usable;
	p.conns = st->nslots > 1 ? st->nslots - SLOT_SPARE : 1;

	for (i = 0; i < st->nslots; i++)
		if (st->slot[i].req)
			p.conns_active++;

	if (st->rate && st->total > st->done)
		p.eta = (uint32_t)((st->total - st->done) / st->rate);

	st->o->cb(st->o->ctx, &p);
}

static void update_rate(dlstate *st)
{
	u64 now = gettime();
	u32 dt = ticks_to_millisecs(diff_ticks(st->t_last, now));

	if (dt < 500)
		return;

	{
		uint64_t db = st->done - st->last_done;
		uint32_t inst = (uint32_t)((db * 1000ull) / dt);

		st->rate = st->rate ? (uint32_t)((st->rate * 3 + inst) / 4) : inst;
	}

	st->t_last = now;
	st->last_done = st->done;
}

/* ------------------------------------------------------------------ */
/* Seitenwagen                                                        */
/* ------------------------------------------------------------------ */

/* Format 3. Weil vorwaerts geschrieben wird, ist die .part immer ein
 * gueltiger Anfang der fertigen Datei - es genuegen also wieder ein
 * Zaehler und eine Pruefsumme. Format 2 (ein Eintrag je Bereich) und
 * Format 1 werden verworfen; dann faengt der Download von vorn an. */
static void sidecar_write(dlstate *st)
{
	char buf[1200];
	uint64_t done;
	uint32_t crc;
	int ok;

	if (st->o->nofile)
		return;

	/* Nur was wirklich auf der Karte steht. */
	if (st->wrunning) {
		LWP_MutexLock(st->wmux);
		done = st->written;
		crc = st->crc;
		LWP_MutexUnlock(st->wmux);
	} else {
		done = st->written;
		crc = st->crc;
	}

	ok = snprintf(buf, sizeof(buf),
	              "CFPART3\n"
	              "total=%llu\n"
	              "done=%llu\n"
	              "crc=%08x\n"
	              "crcok=%d\n"
	              "url=%s\n",
	              (unsigned long long)st->total,
	              (unsigned long long)done,
	              (unsigned)crc,
	              st->crc_usable,
	              st->url ? st->url : "");
	if (ok < 0 || (size_t)ok >= sizeof(buf))
		return;

	/* Erst die Daten auf die Karte, dann den Stand darueber. Anders
	 * herum wuerde der Seitenwagen Bytes versprechen, die nach einem
	 * Absturz nicht da sind. */
	if (st->f) {
		fflush(st->f);
		fsync(fileno(st->f));
	}

	cf_write_text(st->side, buf);
}

static int sidecar_read(dlstate *st, uint64_t *done_out)
{
	char *txt = cf_read_text(st->side, NULL);
	char *p;
	uint64_t total = 0, done = 0;
	uint32_t crc = 0;
	int crcok = 1, url_ok = 0;

	*done_out = 0;

	if (!txt)
		return 0;

	if (strncmp(txt, "CFPART3", 7)) {
		free(txt);
		return 0;
	}

	p = txt;
	while (*p) {
		char *line = p;

		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			*p++ = '\0';

		if (!strncmp(line, "total=", 6))
			total = (uint64_t)strtoull(line + 6, NULL, 10);
		else if (!strncmp(line, "done=", 5))
			done = (uint64_t)strtoull(line + 5, NULL, 10);
		else if (!strncmp(line, "crc=", 4))
			crc = (uint32_t)strtoul(line + 4, NULL, 16);
		else if (!strncmp(line, "crcok=", 6))
			crcok = (int)strtol(line + 6, NULL, 10);
		else if (!strncmp(line, "url=", 4))
			url_ok = st->url && !strcmp(line + 4, st->url);
	}

	free(txt);

	if (!url_ok || !total || done > total)
		return 0;

	/* Geschrieben wird blockweise, der Stand muss also auf einer
	 * Blockgrenze liegen - oder am Dateiende. */
	if (done % BLK_SIZE && done != total)
		return 0;

	st->total = total;
	st->crc = crc;
	st->crc_usable = crcok;
	*done_out = done;
	return 1;
}

/* ------------------------------------------------------------------ */
/* Schreib-Thread                                                     */
/*                                                                    */
/* Er schreibt nur den Block, der als naechster dran ist, und haengt   */
/* damit die Karte parallel ans Netz: gemessen kostete das Schreiben   */
/* in derselben Schleife 37 Prozent Durchsatz.                        */
/* ------------------------------------------------------------------ */

static int find_ready(dlstate *st)
{
	int i;

	for (i = 0; i < st->nslots; i++)
		if (st->slot[i].ready && st->slot[i].idx == st->next_write)
			return i;
	return -1;
}

static void *writer_main(void *arg)
{
	dlstate *st = arg;

	LWP_MutexLock(st->wmux);

	for (;;) {
		blkslot *b;
		unsigned char *buf;
		size_t len;
		int s, err = 0;
		uint32_t crc;

		while ((s = find_ready(st)) < 0 && !st->wstop)
			LWP_CondWait(st->wjobs, st->wmux);

		if (s < 0)
			break;

		b = &st->slot[s];
		buf = b->buf;
		len = b->fill;

		LWP_MutexUnlock(st->wmux);

		/* Angehaengt, nicht gesucht: die Datei waechst lueckenlos,
		 * der Schreibzeiger steht immer schon richtig. */
		if (fwrite(buf, 1, len, st->f) != len)
			err = CF_DL_EWRITE;

		crc = err ? 0 : cf_crc32(st->crc, buf, len);

		LWP_MutexLock(st->wmux);

		if (err) {
			st->werr = err;
		} else {
			st->crc = crc;
			st->written += len;
			st->next_write++;
		}

		b->idx = BLK_NONE;
		b->ready = 0;
		b->fill = 0;

		LWP_CondBroadcast(st->wfree);
	}

	LWP_MutexUnlock(st->wmux);
	return NULL;
}

/* Wartet, bis alles Fertige geschrieben ist. */
static int writer_drain(dlstate *st)
{
	int err;

	if (!st->wrunning)
		return st->werr;

	LWP_MutexLock(st->wmux);
	while (!st->werr && find_ready(st) >= 0)
		LWP_CondWait(st->wfree, st->wmux);
	err = st->werr;
	st->done = st->written;
	LWP_MutexUnlock(st->wmux);

	return err;
}

static int writer_start(dlstate *st)
{
	if (LWP_MutexInit(&st->wmux, false) != 0)
		return CF_HTTP_EMEM;
	if (LWP_CondInit(&st->wjobs) != 0 || LWP_CondInit(&st->wfree) != 0)
		return CF_HTTP_EMEM;

	st->wstack = memalign(32, WSTACK);
	if (!st->wstack)
		return CF_HTTP_EMEM;

	/* Der Netz-Thread von libogc laeuft auf 220 und bleibt damit immer
	 * vorn. 64 ist die normale Stufe, dieselbe wie die Hauptschleife. */
	if (LWP_CreateThread(&st->wthread, writer_main, st,
	                     st->wstack, WSTACK, 64) != 0)
		return CF_HTTP_EMEM;

	st->wrunning = 1;
	return 0;
}

static void writer_stop(dlstate *st)
{
	if (st->wrunning) {
		LWP_MutexLock(st->wmux);
		st->wstop = 1;
		LWP_CondBroadcast(st->wjobs);
		LWP_MutexUnlock(st->wmux);

		LWP_JoinThread(st->wthread, NULL);
		st->wrunning = 0;

		LWP_CondDestroy(st->wjobs);
		LWP_CondDestroy(st->wfree);
		LWP_MutexDestroy(st->wmux);
	}

	free(st->wstack);
	st->wstack = NULL;
}

/* ------------------------------------------------------------------ */
/* Bloecke                                                            */
/* ------------------------------------------------------------------ */

static void slot_drop(blkslot *b)
{
	if (b->req) {
		cf_http_req_close(b->req);
		b->req = NULL;
	}

	/* fill bleibt stehen. Was angekommen ist, ist gueltig - TCP liefert
	 * keine halben Bytes -, und slot_open fragt ab idx * BLK + fill neu
	 * an. Sonst waere bei jedem Verbindungsabbruch ein ganzer Block
	 * umsonst geladen. */
}

static int slot_open(dlstate *st, blkslot *b)
{
	cf_http_info info;
	uint64_t from, to;
	int r;

	memset(&info, 0, sizeof(info));

	from = b->idx * BLK_SIZE + b->fill;
	to = b->idx * BLK_SIZE + b->want - 1;

	/* Erst die Verbindung vom vorigen Block weiterbenutzen. Das schickt
	 * nur die Anfrage und kehrt sofort zurueck; die Kopfzeilen liest
	 * cf_http_req_read, wenn sie da sind. Vorher kostete jeder Block
	 * DNS, Handshake und das Warten auf die Kopfzeilen - zwei
	 * Umlaufzeiten, in denen alle anderen Verbindungen stillstanden. */
	if (b->conn) {
		cf_http_req *c = b->conn;

		b->conn = NULL;
		if (cf_http_req_again(c, from, to) == 0) {
			b->req = c;
			b->t_act = gettime();
			return 0;
		}
		cf_http_req_close(c);
	}

	r = cf_http_req_open(st->src[0] ? st->src : st->url,
	                     from, to, &info, &b->req);
	if (r < 0)
		return r;

	/* Beim ersten Mal die Adresse nach den Redirects festhalten: alle
	 * weiteren Bloecke gehen direkt an den Dateiknoten. */
	if (!st->src[0] && info.final_url[0])
		snprintf(st->src, sizeof(st->src), "%s", info.final_url);

	b->t_act = gettime();
	return 0;
}

/* ------------------------------------------------------------------ */
/* Uebertragung                                                       */
/* ------------------------------------------------------------------ */

static int transfer(dlstate *st)
{
	int inflight_max = st->o->conns > 0 ? st->o->conns : CF_DL_MAX_CONN;

	if (inflight_max > st->nslots)
		inflight_max = st->nslots;

	for (;;) {
		fd_set rd;
		struct timeval tv;
		int i, maxfd = -1, nopen = 0, e;
		u64 now;
		uint64_t nw;

		if (st->o->tick)
			st->o->tick(st->o->ctx);

		if (st->o->cancel && *st->o->cancel) {
			writer_drain(st);
			sidecar_write(st);
			return CF_HTTP_ECANCEL;
		}

		/* Stand vom Schreib-Thread abholen. */
		if (st->wrunning) {
			int werr;

			LWP_MutexLock(st->wmux);
			st->done = st->written;
			nw = st->next_write;
			werr = st->werr;
			LWP_MutexUnlock(st->wmux);

			if (werr < 0)
				return werr;
		} else {
			nw = st->next_write;
		}

		if (nw >= st->nblocks)
			return 0;

		if (st->o->limit_ms &&
		    ticks_to_millisecs(diff_ticks(st->t_start, gettime()))
		    >= st->o->limit_ms)
			return 0;

		/* --- freie Plaetze belegen und Verbindungen aufbauen --- */
		for (i = 0; i < st->nslots; i++) {
			blkslot *b = &st->slot[i];

			if (b->req)
				nopen++;
		}

		for (i = 0; i < st->nslots && nopen < inflight_max; i++) {
			blkslot *b = &st->slot[i];

			if (b->req || b->ready)
				continue;

			if (b->idx == BLK_NONE) {
				uint64_t off;

				if (st->next_assign >= st->nblocks)
					continue;

				b->idx = st->next_assign++;
				off = b->idx * BLK_SIZE;
				b->want = (size_t)((st->total - off < BLK_SIZE)
				                   ? st->total - off : BLK_SIZE);
				b->fill = 0;
				b->tries = 0;
			}

			if (st->o->cancel && *st->o->cancel)
				break;

			e = slot_open(st, b);
			if (e < 0) {
				if (e == CF_HTTP_ENORANGE)
					return CF_DL_ENORANGE;
				if (e == CF_HTTP_EPROTO)
					return CF_DL_ECHUNKED;

				if (++b->tries >= BLK_TRIES)
					return e;

				/* Nicht sofort wieder, sonst sind die Versuche
				 * in Millisekunden verbraucht. */
				usleep(500 * 1000);
				continue;
			}

			nopen++;

			/* "Verbindung wird aufgebaut" nur vor dem ersten Byte.
			 * Danach geht jeder Block mit einer neuen Anfrage los, und
			 * die Anzeige sprang dann jedes Mal vom Balken weg. */
			if (!st->saw_data)
				report(st, CF_DL_P_CONNECT);
		}

		/* --- warten --- */
		FD_ZERO(&rd);
		for (i = 0; i < st->nslots; i++) {
			int fd = cf_http_req_fd(st->slot[i].req);

			if (fd < 0)
				continue;
			FD_SET(fd, &rd);
			if (fd > maxfd)
				maxfd = fd;
		}

		if (maxfd < 0) {
			/* Keine Verbindung offen. Zwei Faelle, und sie muessen
			 * unterschieden werden, sonst dreht die Schleife leer
			 * oder bleibt haengen:
			 *
			 *  - Der Schreiber hat Arbeit: auf ihn warten. Er weckt
			 *    nach jedem Block, und wir halten den Mutex ueber
			 *    Pruefung und Warten, es geht also kein Signal
			 *    verloren.
			 *  - Der Schreiber hat nichts: dann ist gerade kein
			 *    Verbindungsaufbau gelungen. Kurz schlafen und oben
			 *    neu versuchen - warten wuerde hier nie endeen. */
			int waiting = 0;

			if (st->wrunning) {
				LWP_MutexLock(st->wmux);
				if (!st->werr && find_ready(st) >= 0) {
					LWP_CondWait(st->wfree, st->wmux);
					waiting = 1;
				}
				LWP_MutexUnlock(st->wmux);
			}

			if (!waiting)
				usleep(20 * 1000);
			continue;
		}

		tv.tv_sec = 0;
		tv.tv_usec = SELECT_MS * 1000;
		net_select(maxfd + 1, &rd, NULL, NULL, &tv);

		/* --- abholen --- */
		now = gettime();
		for (i = 0; i < st->nslots; i++) {
			blkslot *b = &st->slot[i];
			int fd = cf_http_req_fd(b->req);

			if (fd < 0)
				continue;

			if (!FD_ISSET(fd, &rd)) {
				if (ticks_to_millisecs(diff_ticks(b->t_act, now))
				    > IDLE_MS) {
					slot_drop(b);
					if (++b->tries >= BLK_TRIES)
						return CF_HTTP_ETIMEOUT;
				}
				continue;
			}

			/* Das Fenster vollstaendig leerlesen: jedes gelesene
			 * Byte gibt Fenster frei, und das bestimmt den
			 * Durchsatz. */
			for (;;) {
				int n = cf_http_req_read(b->req, b->buf + b->fill,
				                         b->want - b->fill);

				if (n > 0) {
					b->fill += (size_t)n;
					b->t_act = now;
					b->tries = 0;
					st->saw_data = 1;

					if (b->fill < b->want)
						continue;
				} else if (n == 0) {
					break;
				} else if (n != CF_HTTP_EDONE) {
					slot_drop(b);
					if (++b->tries >= BLK_TRIES)
						return n;
					break;
				}

				/* Block voll, oder die Gegenstelle hat zugemacht.
				 * Taugt die Verbindung fuer eine weitere Anfrage, bleibt
				 * sie beim Platz und traegt dessen naechsten Block. */
				if (cf_http_req_reusable(b->req))
					b->conn = b->req;
				else
					cf_http_req_close(b->req);
				b->req = NULL;

				if (b->fill < b->want) {
					/* Zu wenig geliefert: Rest neu holen. */
					if (++b->tries >= BLK_TRIES)
						return CF_HTTP_EPROTO;
					break;
				}

				if (st->o->nofile) {
					/* Messbetrieb: zaehlen, nicht schreiben,
					 * und die Reihenfolge ist einerlei. */
					st->crc = cf_crc32(st->crc, b->buf, b->fill);
					st->written += b->fill;
					st->next_write++;
					st->done = st->written;
					b->idx = BLK_NONE;
					b->fill = 0;
				} else {
					LWP_MutexLock(st->wmux);
					b->ready = 1;
					LWP_CondSignal(st->wjobs);
					LWP_MutexUnlock(st->wmux);
				}
				break;
			}
		}

		/* --- Anzeige und Stand --- */
		if (st->done >= st->next_cb) {
			update_rate(st);
			report(st, CF_DL_P_TRANSFER);
			st->next_cb = st->done + CB_EVERY;
		}

		if (!st->o->nofile && st->done >= st->next_sidecar) {
			sidecar_write(st);
			st->next_sidecar = st->done + SIDECAR_EVERY;
		}
	}
}

/* ------------------------------------------------------------------ */

static int ensure_space(const char *dir, uint64_t need)
{
	size_t i, n = cf_storage_count();

	for (i = 0; i < n; i++) {
		const cf_device *d = cf_storage_at(i);

		if (!d)
			continue;
		if (!strncmp(dir, d->root, strlen(d->mount) + 1)) {
			cf_storage_refresh(i);
			d = cf_storage_at(i);
			if (d && d->avail && d->avail < need)
				return 0;
			return 1;
		}
	}
	return 1;   /* Geraet nicht zuordenbar: nicht blockieren */
}

/* Belegt die Blockpuffer. Reicht der Speicher nicht fuer alle, wird mit
 * weniger Plaetzen gearbeitet - lieber langsamer als gar nicht. */
static int slots_alloc(dlstate *st, int want)
{
	int i;

	if (want < 2)
		want = 2;
	if (want > CF_DL_MAX_CONN + SLOT_SPARE)
		want = CF_DL_MAX_CONN + SLOT_SPARE;

	for (i = 0; i < want; i++) {
		st->slot[i].buf = memalign(32, BLK_SIZE);
		if (!st->slot[i].buf)
			break;
		st->slot[i].idx = BLK_NONE;
	}

	st->nslots = i;
	return st->nslots >= 2 ? 0 : CF_HTTP_EMEM;
}

static void slots_free(dlstate *st)
{
	int i;

	for (i = 0; i < st->nslots; i++) {
		slot_drop(&st->slot[i]);
		if (st->slot[i].conn) {
			cf_http_req_close(st->slot[i].conn);
			st->slot[i].conn = NULL;
		}
		free(st->slot[i].buf);
		st->slot[i].buf = NULL;
	}
}

static int run_once(cf_qitem *it, const cf_dl_opts *o, int force_fresh);

int cf_download_run(cf_qitem *it, const cf_dl_opts *o)
{
	int r = run_once(it, o, 0);

	/* Der Server bedient keine Bereiche. Ohne Bereiche gibt es keine
	 * Bloecke, also von vorn mit einer einzigen Anfrage ueber alles. */
	if (r == CF_DL_ENORANGE)
		r = run_once(it, o, 1);

	return r;
}

static int run_once(cf_qitem *it, const cf_dl_opts *o, int force_fresh)
{
	dlstate st;
	char dest[464];
	uint64_t resume = 0;
	uint64_t actual;
	int fresh = 1;
	int i, r;

	memset(&st, 0, sizeof(st));
	st.o = o;
	st.crc_usable = 1;
	st.url = it->url;
	st.total = it->size;
	st.crc = (uint32_t)cf_crc32(0, NULL, 0);

	for (i = 0; i < CF_DL_MAX_CONN + SLOT_SPARE; i++)
		st.slot[i].idx = BLK_NONE;

	/* Abschneiden waere hier gefaehrlich: der Pfad zeigte dann auf eine
	 * andere Datei als gemeint. */
	if (snprintf(dest, sizeof(dest), "%s%s",
	             o->dir, it->filename) >= (int)sizeof(dest))
		return CF_DL_EOPEN;
	if (snprintf(st.part, sizeof(st.part), "%s.part",
	             dest) >= (int)sizeof(st.part))
		return CF_DL_EOPEN;
	if (snprintf(st.side, sizeof(st.side), "%s.part.st",
	             dest) >= (int)sizeof(st.side))
		return CF_DL_EOPEN;

	/* --- Messbetrieb: keine Datei, kein Stand --- */
	if (o->nofile) {
		st.nblocks = (st.total + BLK_SIZE - 1) / BLK_SIZE;

		r = slots_alloc(&st, (o->conns > 0 ? o->conns : CF_DL_MAX_CONN)
		                     + SLOT_SPARE);
		if (r < 0) {
			slots_free(&st);
			return r;
		}

		st.t_start = gettime();
		st.t_last = st.t_start;
		report(&st, CF_DL_P_CONNECT);

		r = transfer(&st);

		update_rate(&st);
		report(&st, CF_DL_P_TRANSFER);
		slots_free(&st);
		return r;
	}

	/* --- Fortsetzen? --- */
	actual = cf_file_size(st.part);
	if (!force_fresh && actual > 0 && sidecar_read(&st, &resume)) {
		if (resume > actual) {
			/* Nach einem harten Absturz liegt weniger auf der
			 * Karte, als der Seitenwagen verspricht. Dann ist die
			 * Pruefsumme der fehlenden Bytes nicht mehr gueltig. */
			resume = actual - (actual % BLK_SIZE);
			st.crc_usable = 0;
		}
		fresh = 0;
	}

	if (fresh) {
		remove(st.part);
		remove(st.side);
		resume = 0;
		st.total = it->size;
		st.crc = (uint32_t)cf_crc32(0, NULL, 0);
		st.crc_usable = 1;
	}

	st.resumed = !fresh;
	st.written = resume;
	st.done = resume;

	report(&st, CF_DL_P_PREPARE);

	/* FAT32 kann keine Datei ab 4 GB. */
	if (st.total >= 0x100000000ull)
		return CF_DL_EFAT32;

	if (!st.total) {
		/* Ohne bekannte Groesse gibt es keine Bloecke. Dann hilft nur
		 * ein Versuch, die Groesse zu erfragen. */
		cf_http_info hi;

		memset(&hi, 0, sizeof(hi));
		if (cf_http_head(it->url, &hi) == 200 && hi.total)
			st.total = hi.total;
		else
			return CF_HTTP_EPROTO;
	}

	if (!ensure_space(o->dir, st.total - st.done))
		return CF_DL_ESPACE;

	st.nblocks = (st.total + BLK_SIZE - 1) / BLK_SIZE;

	/* Erst jetzt, weil nblocks dafuer feststehen muss: ist schon alles
	 * geladen, dann ist der letzte Block auch bei einem Rest kleiner
	 * als BLK_SIZE fertig - resume / BLK_SIZE wuerde ihn sonst noch
	 * einmal anfordern. */
	st.next_write = (resume == st.total) ? st.nblocks : resume / BLK_SIZE;
	st.next_assign = st.next_write;

	/* --- Datei oeffnen --- */
	st.f = fopen(st.part, fresh ? "wb+" : "r+b");
	if (!st.f)
		return CF_DL_EOPEN;

	/* Gepuffert wird hier selbst, in Bloecken. */
	setvbuf(st.f, NULL, _IONBF, 0);

	/* Auf den Stand zurechtschneiden: ein angefangener Block vom
	 * letzten Mal wird neu geladen. Verkuerzen kostet nichts, es gibt
	 * nur Cluster frei - anders als das Verlaengern, das die
	 * Vorbelegung so teuer gemacht hat. */
	if (ftruncate(fileno(st.f), (off_t)resume) != 0)
		clearerr(st.f);
	if (fseeko(st.f, (off_t)resume, SEEK_SET) != 0) {
		fclose(st.f);
		return CF_DL_EOPEN;
	}

	r = slots_alloc(&st, (o->conns > 0 ? o->conns : CF_DL_MAX_CONN)
	                     + SLOT_SPARE);
	if (r < 0)
		goto out;

	r = writer_start(&st);
	if (r < 0)
		goto out;

	st.t_start = gettime();
	st.t_last = st.t_start;
	st.last_done = st.done;
	st.next_sidecar = st.done + SIDECAR_EVERY;
	st.next_cb = st.done;

	report(&st, CF_DL_P_CONNECT);

	/* --- Laden --- */
	r = transfer(&st);

	{
		int e = writer_drain(&st);
		if (e < 0 && r == 0)
			r = e;
	}

	sidecar_write(&st);

	if (r != 0)
		goto out;

	/* --- Abschliessen --- */
	if (st.done != st.total) {
		r = CF_HTTP_EPROTO;
		goto out;
	}

	/* Erst den Thread beenden, dann die Puffer freigeben: er liest aus
	 * ihnen, solange er laeuft. */
	writer_stop(&st);
	slots_free(&st);

	if (fclose(st.f) != 0) {
		st.f = NULL;
		return CF_DL_EWRITE;
	}
	st.f = NULL;

	if (o->verify && it->has_crc && st.crc_usable) {
		if (st.crc != it->crc32)
			return CF_DL_ECRC;
	}

	remove(dest);
	if (rename(st.part, dest) != 0)
		return CF_DL_ERENAME;

	remove(st.side);
	return 0;

out:
	/* Reihenfolge ist hier nicht beliebig: der Schreib-Thread liest aus
	 * den Blockpuffern. Wer sie vor dem Beenden freigibt, zieht sie ihm
	 * mitten im Schreiben weg - und dieser Pfad wird auch im Fehlerfall
	 * genommen, also ohne vorheriges writer_drain. */
	writer_stop(&st);
	slots_free(&st);
	if (st.f)
		fclose(st.f);
	st.f = NULL;

	/* Ein Abbruch ohne ein einziges geschriebenes Byte hinterlaesst
	 * nichts Nuetzliches. */
	if (r == CF_HTTP_ECANCEL && st.done == 0) {
		remove(st.part);
		remove(st.side);
	}
	return r;
}

const char *cf_download_strerror(int code)
{
	switch (code) {
	case CF_DL_EOPEN:     return "Datei kann nicht angelegt werden";
	case CF_DL_EWRITE:    return "Schreiben auf die Karte fehlgeschlagen";
	case CF_DL_ESPACE:    return "Nicht genug Platz auf dem Zielgeraet";
	case CF_DL_ECRC:      return "Pruefsumme stimmt nicht";
	case CF_DL_ERENAME:   return "Umbenennen fehlgeschlagen";
	case CF_DL_EPREALLOC: return "Datei kann nicht vorbelegt werden";
	case CF_DL_EFAT32:    return "Datei zu gross fuer FAT32 (max 4 GB)";
	case CF_DL_ENORANGE:  return "Server kann nicht fortsetzen";
	case CF_DL_ECHUNKED:  return "Server antwortet in Bloecken";
	case CF_DL_ESIDECAR:  return "Stand auf der Karte passt nicht";
	default:              break;
	}
	return cf_http_strerror(code);
}
