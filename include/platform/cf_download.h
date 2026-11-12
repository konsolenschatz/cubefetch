/* Eine Datei abbruchsicher auf das Zielgeraet schreiben.
 *
 * Bei knapp einer halben Stunde pro Image ist Fortsetzen keine Zugabe.
 * Drei Dinge macht dieses Modul darum anders als ein einfaches "lade und
 * schreibe" (Begruendung in docs/ARCHITECTURE.md):
 *
 *  - Die Datei wird in Bloecke geteilt, die gleichzeitig geladen werden.
 *    Nicht aus Ungeduld: das lwIP von libogc2 begrenzt eine einzelne
 *    Verbindung auf 2920 Bytes unterwegs, siehe CF_DL_MAX_CONN.
 *  - Geschrieben wird streng in der Reihenfolge der Bloecke, fertige
 *    Bloecke warten im Speicher. Die Datei waechst damit lueckenlos und
 *    muss nicht vorbelegt werden - FAT kennt keine Loecher, und das
 *    Vorbelegen von 1,36 GB kostete gemessene 20 Minuten.
 *  - Die Pruefsumme laeuft beim Schreiben mit, in Reihenfolge, also eine
 *    einzige. Ein zweiter Lesedurchgang ueber 1,4 GB von der Karte waere
 *    teurer als der Download selbst.
 *  - Geschrieben wird in einem eigenen Thread, damit die Karte parallel
 *    zum Netz laeuft statt abwechselnd. In derselben Schleife kostete
 *    das Schreiben 37 Prozent Durchsatz.
 *  - Neben der .part liegt ein Seitenwagen mit Stand und
 *    Zwischen-Pruefsumme. Weil vorwaerts geschrieben wird, ist die .part
 *    immer ein gueltiger Anfang der fertigen Datei.
 */
#ifndef CF_DOWNLOAD_H
#define CF_DOWNLOAD_H

#include <stdint.h>

#include "core/cf_queue.h"
#include "platform/cf_http.h"

#define CF_DL_EOPEN     (-20)
#define CF_DL_EWRITE    (-21)
#define CF_DL_ESPACE    (-22)
#define CF_DL_ECRC      (-23)
#define CF_DL_ERENAME   (-24)
#define CF_DL_EPREALLOC (-25)
#define CF_DL_EFAT32    (-26)
#define CF_DL_ENORANGE  (-27)
#define CF_DL_ECHUNKED  (-28)
#define CF_DL_ESIDECAR  (-29)

/* Wie viele Verbindungen hoechstens gleichzeitig laufen.
 *
 * Warum ueberhaupt mehrere: das lwIP von libogc2 hat ein Empfangsfenster
 * von 2920 Bytes und kein Window Scaling, der Durchsatz je Verbindung ist
 * also 2920 Bytes geteilt durch die Umlaufzeit. Zu einem Server an der
 * US-Westkueste (gemessen 209 ms) sind das 14 kB/s, und daran aendert
 * auch eine schnelle Leitung nichts. Jede Verbindung bringt ihr eigenes
 * Fenster mit.
 *
 * Die Obergrenze ist nicht willkuerlich. In libbba.a nachgesehen:
 *
 *  - memp_num nennt 16 TCP-PCBs, 16 netconns, 128 Segmente und 128
 *    Pool-Puffer.
 *  - Das Feld "sockets" in network.o ist 320 Bytes gross, bei 20 Bytes
 *    je Eintrag also genau 16 Sockel. Mehr kann die Socket-Schicht von
 *    libogc2 nicht gleichzeitig offen halten, TCP und UDP zusammen.
 *
 * Zwoelf lassen damit vier frei - fuer die Namensauflösung, die bei
 * jedem Verbindungsaufbau einen UDP-Sockel braucht, und fuer Luft.
 *
 * Auf Hardware gemessen (entfernter Server, Umlaufzeit 209 ms): mit acht
 * Verbindungen 100 kB/s, also 12,5 je Verbindung gegen 14 in der
 * Rechnung. Es skaliert praktisch linear, die Gegenstelle bremst nicht.
 * Der Adapter selbst koennte etwa 1 MB/s. */
#define CF_DL_MAX_CONN  12

/* Woran gerade gearbeitet wird.
 *
 * Es gab hier zuerst nur den Datenstrom, und damit blieb der Bildschirm
 * von "Warteschlange starten" bis zum ersten Datenpaket unveraendert
 * stehen - also waehrend Platzbelegung, Namensauflösung, Verbindung und
 * Kopfzeilen. Auf der Konsole sah das aus, als sei nichts passiert. */
typedef enum {
	CF_DL_P_PREPARE = 0,  /* Datei anlegen, Groesse vorbelegen */
	CF_DL_P_CONNECT,      /* Namen aufloesen, verbinden, Kopf lesen */
	CF_DL_P_TRANSFER      /* Daten laufen */
} cf_dl_phase;

typedef struct {
	int      phase;       /* cf_dl_phase */
	uint64_t total;
	uint64_t done;
	uint32_t rate;        /* Bytes je Sekunde, geglaettet */
	uint32_t eta;         /* Sekunden, 0 wenn unbekannt */
	int      resumed;     /* Download wurde fortgesetzt */
	int      crc_usable;  /* Pruefsumme kann am Ende geprueft werden */
	int      conns;       /* Bereiche insgesamt */
	int      conns_active;/* davon gerade mit offener Verbindung */
} cf_dl_progress;

/* Wird gelegentlich waehrend des Ladens gerufen, damit die Oberflaeche
 * sich auffrischen und Tasten abfragen kann. */
typedef void (*cf_dl_cb)(void *ctx, const cf_dl_progress *p);

typedef struct {
	char         dir[256];   /* Zielordner, z. B. "sd2:/games/" */
	cf_dl_cb     cb;
	void        *ctx;
	volatile int *cancel;
	int          verify;     /* Pruefsumme am Ende vergleichen */

	/* Gewuenschte Zahl gleichzeitiger Verbindungen, 1 bis
	 * CF_DL_MAX_CONN. 0 heisst "Standard". Bei mehr als einer wird die
	 * Datei zwangsweise vorbelegt: die Bereiche schreiben an weit
	 * auseinander liegende Stellen, und libfat fuellt beim Verlaengern
	 * einer Datei alles dazwischen mit Nullen. Einmal am Anfang und
	 * sichtbar ist besser als unvorhersehbar mitten im Download. */
	int          conns;

	/* Fuer die Messung: nur laden, nichts auf die Karte schreiben, und
	 * nach limit_ms aufhoeren. Damit laesst sich trennen, ob das Netz
	 * oder das Schreiben die Grenze setzt - sonst bleibt es beim Raten.
	 * Es wird keine .part angelegt und kein Stand gespeichert. */
	int          nofile;
	uint32_t     limit_ms;

	/* Wird in jedem Durchlauf der Ladeschleife gerufen, also mehrmals je
	 * Sekunde - im Gegensatz zu cb, das nur alle 256 KB kommt. Gedacht
	 * fuer das Abfragen des Controllers: ein kurzer Tastendruck zwischen
	 * zwei cb-Aufrufen war sonst gar nicht zu sehen. Darf nicht
	 * zeichnen, ausser bei einer Eingabe - jedes Bild wartet auf den
	 * Bildwechsel. */
	void       (*tick)(void *ctx);
} cf_dl_opts;

/* 0 bei Erfolg, sonst ein Fehlercode aus CF_DL_* oder CF_HTTP_*. */
int cf_download_run(cf_qitem *it, const cf_dl_opts *o);

const char *cf_download_strerror(int code);

#endif
