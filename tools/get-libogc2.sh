#!/bin/sh
# Holt libogc2 und libogc2-libfat projektlokal nach toolchain/libogc2.
#
# Warum nicht einfach das Paketrepository von Extrems eintragen? Weil das
# System damit libogc2-Pakete an die Stelle der devkitPro-Pakete setzt und
# damit jedes andere libogc-Projekt auf demselben Rechner betrifft. Wer das
# bewusst will, folgt der Anleitung unter https://packages.libogc2.org/ -
# das Makefile nimmt eine Installation unter $DEVKITPRO/libogc2 bevorzugt.
#
# Aufruf:  tools/get-libogc2.sh [zielordner]
#
# Geprueft wird beides: die Signatur des Herausgebers gegen den bekannten
# Fingerabdruck und die Pruefsumme aus der Paketdatenbank. Ohne gueltige
# Signatur bricht das Skript ab.
set -eu

DEST=${1:-toolchain/libogc2}
KEY_FPR=C8A2759C315CFBC3429CC2E422B803BA8AA3D7CE
KEY_URL="https://keyserver.ubuntu.com/pks/lookup?op=get&options=mr&search=0x$KEY_FPR"
PKGS="libogc2-git libogc2-libfat-git"

case $(uname -s) in
	Linux)  OS=linux  ;;
	Darwin) OS=macos  ;;
	*)      OS=windows ;;
esac

# Die Pakete sind architekturunabhaengig ("-any"), liegen aber im Ordner der
# Wirtsarchitektur. Mit MIRROR laesst sich eine feste Adresse vorgeben.
HOSTS="https://packages.libogc2.org/devkitpro https://packages.extremscorner.org/devkitpro"
ARCHS="$(uname -m) x86_64 aarch64 arm64"

for t in curl gpg gpgv tar sha256sum awk; do
	command -v "$t" >/dev/null 2>&1 || { echo "fehlt: $t" >&2; exit 1; }
done

case $DEST in
	/*) OUT=$DEST ;;
	*)  OUT=$PWD/$DEST ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
cd "$WORK"

echo "== Schluessel des Herausgebers =="
# Ueber HTTP statt gpg --recv-keys: dirmngr laeuft unter msys2 nicht
# zuverlaessig. Ein eigenes GNUPGHOME, damit der Schluesselbund des
# Benutzers unberuehrt bleibt.
GNUPGHOME=$WORK/gnupg
export GNUPGHOME
mkdir -p "$GNUPGHOME"
chmod 700 "$GNUPGHOME"

curl -fsSL "$KEY_URL" -o key.asc
gpg --batch --dearmor < key.asc > key.gpg
gpg --batch --show-keys --with-colons key.asc \
	| awk -F: '/^fpr:/ { print $10 }' | grep -qx "$KEY_FPR" \
	|| { echo "Fingerabdruck des geholten Schluessels passt nicht" >&2; exit 1; }
echo "  $KEY_FPR"

echo "== Paketdatenbank =="
if [ -n "${MIRROR:-}" ]; then
	curl -fsSL "$MIRROR/libogc2-devkitpro.db" -o db.tar
else
	for h in $HOSTS; do
		for a in $ARCHS; do
			if curl -fsSL "$h/$OS/$a/libogc2-devkitpro.db" -o db.tar; then
				MIRROR="$h/$OS/$a"
				break
			fi
		done
		if [ -n "${MIRROR:-}" ]; then break; fi
	done
fi
[ -n "${MIRROR:-}" ] || { echo "kein Spiegel erreichbar" >&2; exit 1; }
echo "  $MIRROR"
mkdir db
tar -xf db.tar -C db

for p in $PKGS; do
	file=
	sum=
	for d in db/*/desc; do
		[ "$(awk '/^%NAME%$/ { getline; print; exit }' "$d")" = "$p" ] || continue
		file=$(awk '/^%FILENAME%$/   { getline; print; exit }' "$d")
		sum=$(awk  '/^%SHA256SUM%$/ { getline; print; exit }' "$d")
		break
	done
	[ -n "$file" ] || { echo "$p steht nicht in $MIRROR" >&2; exit 1; }

	echo "== $file =="
	curl -fsSL "$MIRROR/$file"     -o "$file"
	curl -fsSL "$MIRROR/$file.sig" -o "$file.sig"

	echo "$sum  $file" | sha256sum -c - >/dev/null \
		|| { echo "Pruefsumme weicht ab" >&2; exit 1; }
	# Der Rueckgabewert von gpgv entscheidet, nicht der Text: der ist
	# uebersetzt. Im Schluesselbund steht ausschliesslich der oben
	# gegen den Fingerabdruck gepruefte Schluessel.
	if gpgv --keyring "$WORK/key.gpg" "$file.sig" "$file" >gpgv.log 2>&1; then
		echo "  Signatur in Ordnung"
	else
		sed 's/^/  /' gpgv.log >&2
		echo "Signatur nicht in Ordnung: $file" >&2
		exit 1
	fi
done

echo "== auspacken nach $DEST =="
mkdir -p "$OUT"
for f in *.pkg.tar.*; do
	case $f in *.sig) continue ;; esac
	tar -xf "$f" -C "$OUT" --strip-components=3 opt/devkitpro/libogc2
done

[ -f "$OUT/gamecube/lib/libbba.a" ] \
	|| { echo "libbba.a fehlt im Ergebnis" >&2; exit 1; }
echo "fertig. Jetzt baut make gegen $DEST"
