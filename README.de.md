# CubeFetch

*[English version](README.md)*

Netzwerk-Downloader für den Nintendo GameCube. Lädt Dateien von selbst
eingetragenen Quellen direkt über ein Netzwerkkabel auf die am Gerät
angeschlossenen Speichermedien — ohne PC in der Mitte. Gedacht für eigene
Sicherungskopien und Homebrew.

Sprachen: Deutsch, English, Français, Español, Italiano, Русский, 日本語.
Oberfläche per GameCube-Controller bedienbar, Texteingabe über eine
Bildschirmtastatur. Acht Designs, Standard ist das Konsolenschatz-Design.

## Was funktioniert

- **Quellen frei definierbar** — als `sources.txt` auf der Karte oder per
  Bildschirmtastatur eingegeben. Zwei Typen:
  HTML-Verzeichnislisting und ein eigenes JSON-Manifest.
- **Katalog** mit Suchfeld und vier Sortierungen (Name auf-/absteigend,
  Größe auf-/absteigend).
- **Warteschlange** bis 32 Titel, auf der Karte gespeichert und über Neustarts
  hinweg fortsetzbar.
- **Abbruchsicherer Download** mit HTTP-Range, mehreren Blöcken gleichzeitig
  und mitlaufender CRC32-Prüfung. Geschrieben wird streng vorwärts und in einem
  eigenen Thread: die Datei muss darum nicht vorbelegt werden und ist jederzeit
  ein gültiger Anfang der fertigen. Der Stand liegt neben der `.part` und
  übersteht Ausschalten und Absturz.
- **Disc-ID vor dem Download** — ein 1-KB-Range-Request liest die echte
  Spiel-ID samt Region aus dem Image-Header, ohne 1,4 GB zu laden.
- **Eigener DNS-Client**, weil libogc für den GameCube keine Namensauflösung
  mitbringt.
- **EXI-Konfliktwarnung**, wenn Netzwerkadapter und Speicherkarte auf demselben
  Kanal liegen.

- **Gezeichnete Oberfläche** mit GX und FreeType: Proportionalschrift mit
  Umlauten, Farben, Liste mit Detailfeld, doppelt gepuffert.
- **Cover und Spielinfos aus GameTDB** — Packungsbild, Jahr, Herausgeber,
  Entwickler, Genre, Spielerzahl. Exakt über die Disc-ID, sonst über
  Namensähnlichkeit mit Kennzeichnung. Die Datenbank holt die Konsole selbst
  (2,3 MB, im Strom entpackt); siehe [docs/METADATA.md](docs/METADATA.md).

- **Sortieren und filtern** nach Name, Größe und Jahr, auf- und absteigend,
  dazu ein Genre-Filter aus den Genres, die im Katalog wirklich vorkommen.

Noch nicht dabei: exFAT. Siehe
[Phasenplan](docs/ARCHITECTURE.md#phasenplan).

- **Warteschlange im Download-Bildschirm**: die nächsten Titel sind sichtbar,
  mit ↑↓ und A wird gewählt, was als Nächstes lädt.
- **Community-Seite** mit QR-Codes zum Konsolenschatz-Discord, zu GitHub und
  YouTube.

## Rechtlicher Hinweis

CubeFetch ist ein allgemeiner HTTP-Downloader, gedacht für **Homebrew** und
für **Sicherungskopien eigener Spiele vom eigenen Server** (zum Beispiel ein
NAS oder PC im Heimnetz). Es werden **keine Quellen, Links oder Spieledaten
mitgeliefert**, und das Programm enthält keinen Code oder Schlüssel von
Nintendo. **Das Projekt stellt keine Raubkopien bereit und unterstützt sie
nicht** — bitte keine Quellen anfragen und keine in Issues oder den
Community-Kanälen teilen. Welche Adresse eingetragen wird und ob die dort
liegenden Inhalte genutzt werden dürfen, entscheidet und verantwortet allein
die Person, die das Gerät bedient. Lade nur Sicherungen von Spielen, die du
besitzt, oder Inhalte, für die du die Rechte hast.

CubeFetch steht in keiner Verbindung zu Nintendo. „Nintendo“ und „GameCube“
sind Marken von Nintendo.

## Geschwindigkeit

Entscheidend ist die **Entfernung zur Quelle**, nicht die Leitung. Das lwIP in
libogc2 arbeitet mit einem Empfangsfenster von 2920 Bytes und ohne Window
Scaling, eine einzelne Verbindung ist damit auf `2920 Bytes / Umlaufzeit`
begrenzt — von einem Server an der US-Westküste rund 14 kB/s, also über einen
Tag pro Image.

Darum lädt CubeFetch **in mehreren Blöcken gleichzeitig**, jeder mit eigener
Verbindung und eigenem Fenster. Auf Hardware gemessen: mit acht Verbindungen
100 kB/s statt 16, also vier Stunden statt 29. Einstellbar sind bis zu zwölf;
im eigenen Netz läuft es ohnehin mit voller Adaptergeschwindigkeit. Die Zahl
steht in den Einstellungen und in `config.txt` als `conns=`.

Wer das Fenster selbst vergrößert (`tools/build-libogc2.sh`, siehe
[Bauen](#bauen)), kommt an die eigentliche Grenze des Geräts: **rund 1 MB/s**,
gesetzt vom 10-MBit-Adapter — ein Image in etwa 25 Minuten. Die vier Glieder der
Kette samt Rechnung stehen in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#wo-die-grenze-des-gamecube-wirklich-liegt).
Hintergrund in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#realistische-geschwindigkeit).

## Hardware

Netzwerk und Speicher teilen sich beim GameCube denselben EXI-Bus. Nicht jede
Kombination funktioniert gleichzeitig — siehe die Kompatibilitätsmatrix in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#hardware-und-exi-belegung).

Netzwerkseitig laufen der echte Broadband Adapter, ETH2SP1 (W5500) und die
ENC28J60-Adapter für Serial Port 2 oder für einen Memory-Card-Slot. Gesucht wird
an allen vier Steckplätzen: SP1, SP2, Slot A, Slot B.

Empfohlene Kombination: **Netzwerkadapter an Serial Port 1** plus **SD2SP2 an
Serial Port 2**. Getrennte EXI-Kanäle, keine Konflikte. Auf echter Hardware
geprüft ist bisher **ENC28J60-Adapter im Memory-Card-Slot** plus SD2SP2.

## Bauen

Benötigt devkitPro mit devkitPPC und den portlibs `ppc-freetype`, `ppc-libpng`
und `ppc-zlib` samt deren Abhängigkeiten (bzip2, brotli) — und **libogc2**, nicht
das libogc von devkitPro: nur libogc2 bringt Treiber für ENC28J60, W5500 und W6x00
mit und sucht den Adapter auch in den Memory-Card-Slots. Begründung in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#werkzeugkette-libogc2-statt-libogc).

```
tools/get-libogc2.sh   # holt libogc2 signaturgeprüft nach toolchain/
make                   # erzeugt cubefetch.dol
make dist              # legt die fertige Ordnerstruktur für die Karte an
```

Für den schnellen Download zusätzlich:

```
tools/build-libogc2.sh   # libogc2 mit größerem TCP-Fenster, nach toolchain/
```

Das baut libogc2 aus dem Quelltext mit vier geänderten lwIP-Konstanten. Der
Grund steht unter [Geschwindigkeit](#geschwindigkeit); der Quelltext von
CubeFetch ändert sich dafür mit keiner Zeile. Das Makefile nimmt die Fassung
automatisch, sobald sie da ist, und sagt beim Bauen, welche es verwendet.

Wer libogc2 über das Paketrepository von Extrems installiert hat, braucht das
Skript nicht; das Makefile nimmt `$DEVKITPRO/libogc2` bevorzugt. Die portlibs
`ppc-zlib`, `ppc-libpng` und `ppc-freetype` von devkitPro sind zu libogc2 **nicht**
ABI-kompatibel und werden darum nicht eingebunden.

## Testen

Der Kern unter `source/core` ist frei von libogc und lässt sich mit einem
normalen Compiler prüfen:

```
make -C tests run
```

Anleitung für die Konsole: [docs/TESTING.md](docs/TESTING.md).

## Dokumentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — Aufbau, Hardware, Phasenplan
- [docs/SOURCE-FORMAT.md](docs/SOURCE-FORMAT.md) — Format der Quellen-Definition
- [docs/METADATA.md](docs/METADATA.md) — Zuordnung von Bildern und Spielinfos
- [docs/TESTING.md](docs/TESTING.md) — auf der Konsole testen

## Credits

- **Konsolenschatz** — Autor
- **[Nightmare3500](https://github.com/Nightmare3500)** — Betatest

## Lizenz

CubeFetch ist Open Source unter der [MIT-Lizenz](LICENSE): frei nutzbar,
änderbar und weitergebbar, solange der Urheberhinweis (Konsolenschatz)
erhalten bleibt. Schriften und Bibliotheken behalten ihre eigenen Lizenzen,
Name und Münze von Konsolenschatz sind ausgenommen; siehe
[THIRD-PARTY.md](THIRD-PARTY.md).
