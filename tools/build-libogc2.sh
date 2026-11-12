#!/bin/sh
# Baut libogc2 mit groesserem TCP-Empfangsfenster nach
# toolchain/libogc2-fast. Das Makefile nimmt diese Fassung dann
# bevorzugt; ohne sie baut alles unveraendert gegen das fertige Paket.
#
# Warum ueberhaupt:
#
# Das lwIP von libogc2 ist mit TCP_WND = 2 * TCP_MSS = 2920 Bytes
# uebersetzt und kennt kein Window Scaling. Eine Verbindung schafft damit
# nie mehr als 2920 Bytes geteilt durch die Umlaufzeit - zu einem Server
# in Kalifornien (gemessen 209 ms) also 14 kB/s, egal wie schnell Leitung,
# Adapter und Karte sind. Gegen dieselbe Datei gehalten, die der GameCube
# mit 1,2 MB/s auf die SD-Karte schreibt, ist das der eigentliche Engpass.
#
# TCP_SND_BUF steht in derselben Datei auf 36 * TCP_MSS. Das Fenster ist
# also nicht aus Speichernot klein, sondern einfach auf dem lwIP-Standard
# geblieben - fuer eine Bibliothek, die sonst vor allem Spielstaende und
# Netzwerkspiele im lokalen Netz bedient, voellig in Ordnung.
#
# Was der Patch macht (tools/libogc2-window.patch):
#
# lwipopts.h
#   TCP_WND            2 -> 24 * TCP_MSS   (2920 -> 35040 Bytes)
#   PBUF_POOL_SIZE     128 -> 512          (reicht fuer 8 solcher Fenster)
#   MEMP_NUM_TCP_SEG   128 -> 256
#   MEMP_NUM_TCPIP_MSG 128 -> 384          (Warteschlange zum Netz-Thread)
#   MEM_SIZE           192 -> 512 KB
#
# enc28j60if.c - zwei Fehler, die erst mit grossem Fenster weh tun:
#   Empfangsspeicher des Chips 4 -> 6 KB (von 8 KB; gesendet wird
#     hoechstens ein Rahmen, 2 KB reichen dafuer)
#   Der Unterbrechungsbehandler holt alle wartenden Pakete ab, vorher
#     genau eines je Unterbrechung
#
# Kosten: rund 1,4 MB statischer Speicher fuer lwIP statt 400 KB. Der
# GameCube hat 24 MB.
#
# Erwartung: 35040 / 0,209 s = 168 kB/s je Verbindung. Sechs davon reichen
# damit fuer rund 1 MB/s - und das ist die Grenze des 10-MBit-Adapters.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$ROOT/toolchain/libogc2-src}
DEST=${DEST:-$ROOT/toolchain/libogc2-fast}
REPO=https://github.com/extremscorner/libogc2.git

for t in git make; do
	command -v "$t" >/dev/null 2>&1 || { echo "fehlt: $t" >&2; exit 1; }
done

: "${DEVKITPPC:?DEVKITPPC ist nicht gesetzt - in der devkitPro-Shell starten}"

if [ ! -d "$SRC/.git" ]; then
	echo "== libogc2 holen =="
	git clone --depth 1 "$REPO" "$SRC"
fi

echo "== Stand =="
git -C "$SRC" log -1 --format='%H %cd' --date=short

echo "== Patch anwenden =="
# Erst zuruecksetzen, damit ein zweiter Lauf nicht scheitert.
git -C "$SRC" checkout -- lwip/include/lwip/lwipopts.h lwip/netif/enc28j60if.c
git -C "$SRC" apply "$ROOT/tools/libogc2-window.patch"
grep -n "define TCP_WND\b" "$SRC/lwip/include/lwip/lwipopts.h"
grep -n "define ENC28J60_INIT_ERXND" "$SRC/lwip/netif/enc28j60if.c"

echo "== bauen (nur libogc und libbba) =="
# Nicht "make cube": das baut auch libasnd und libaesnd, und deren
# MP3-Player braucht mad.h aus dem portlib ppc-libmad. Das hat hier
# niemand, und gebraucht wird es auch nicht - CubeFetch linkt gegen
# libogc, libbba und libfat.
#
# LIBRARIES von der Kommandozeile ueberschreibt die Zuweisung im
# Makefile, auch das spaetere "+=" (so ist es bei make vorgesehen). Die
# Zeichenkette wird erst im inneren Durchgang aufgeloest, wo LIBDIR
# definiert ist - darum die einfachen Anfuehrungszeichen.
make -C "$SRC" cube -j"$(nproc 2>/dev/null || echo 4)" \
	LIBRARIES='$(LIBDIR)/libogc.a $(LIBDIR)/libbba.a'

echo "== einsammeln nach $DEST =="
[ -f "$SRC/lib/cube/libogc.a" ] || { echo "libogc.a wurde nicht gebaut" >&2; exit 1; }
[ -f "$SRC/lib/cube/libbba.a" ] || { echo "libbba.a wurde nicht gebaut" >&2; exit 1; }

rm -rf "$DEST"
mkdir -p "$DEST/gamecube/lib" "$DEST/gamecube/include"
cp -r "$SRC/lib/cube/." "$DEST/gamecube/lib/"
cp -r "$SRC/include/." "$DEST/gamecube/include/"
cp "$SRC/gamecube_rules" "$DEST/"

# libfat gehoert nicht zu libogc2 und wird aus dem Paket uebernommen, das
# tools/get-libogc2.sh geholt hat. Es haengt nicht am lwIP.
#
# Mit -n, also ohne Ueberschreiben: so kommen die Kopfdateien von libfat
# mit (fat.h zieht libfatversion.h nach), waehrend die eben gebauten
# Kopfdateien von libogc2 stehen bleiben.
PKG=$ROOT/toolchain/libogc2
if [ -f "$PKG/gamecube/lib/libfat.a" ]; then
	cp "$PKG/gamecube/lib/libfat.a" "$DEST/gamecube/lib/"
	cp -rn "$PKG/gamecube/include/." "$DEST/gamecube/include/" 2>/dev/null || true
else
	echo "Hinweis: libfat fehlt. Erst tools/get-libogc2.sh laufen lassen." >&2
fi

[ -f "$DEST/gamecube/lib/libbba.a" ] || { echo "libbba.a fehlt" >&2; exit 1; }
[ -f "$DEST/gamecube/lib/libfat.a" ] || { echo "libfat.a fehlt" >&2; exit 1; }

echo
echo "fertig. 'make clean && make' baut jetzt gegen $DEST"
