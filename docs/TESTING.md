# Auf der Konsole testen

## Was auf die Karte muss

`make dist` legt die Struktur an. Der Inhalt von `dist/` gehoert in die
Wurzel der Speicherkarte:

```
<Karte>/apps/cubefetch/boot.dol      die Anwendung
<Karte>/cubefetch/sources.txt        Quellen, bearbeitbar
<Karte>/cubefetch/lang/de.lang       deutsche Texte
<Karte>/cubefetch/lang/en.lang       englische Texte
<Karte>/games/                       Zielordner, wird sonst selbst angelegt
```

Die Karte muss **FAT32 oder FAT16** sein. exFAT kann libfat nicht. Ein
volles GC-Image ist 1,36 GB und bleibt unter der 4-GB-Grenze von FAT32.

Gestartet wird `boot.dol` wie jede andere Homebrew, also ueber Swiss,
PicoBoot oder einen anderen Loader.

## Hardware

CubeFetch wird gegen **libogc2** gebaut, nicht gegen devkitPros libogc.
Nur libogc2 bringt eigene Treiber fuer ENC28J60, W5500 und W6x00 mit und
sucht den Adapter an vier Stellen: Serial Port 1, Serial Port 2 und in
beiden Memory-Card-Slots. devkitPros libogc kennt allein den echten
Broadband Adapter und allein an Serial Port 1.

Es laufen also: BBA und ETH2SP1 an Serial Port 1, ETH2GC an Serial
Port 2, und die Adapter fuer den Memory-Card-Slot (GCNET und Verwandte,
ENC28J60). Geprueft auf Hardware ist bisher ein ENC28J60-Adapter im
Memory-Card-Slot.

Was nicht geht oder klemmt, steht in
[ARCHITECTURE.md](ARCHITECTURE.md#hardware-und-exi-belegung). Kurz:

- Karte in **Memory Card Slot A** plus Netzwerk an SP1 liegen beide auf
  EXI-Kanal 0. Die Anwendung erkennt das und warnt beim Start. Slot B
  oder SD2SP2 sind unproblematisch.
- **ETH2GC** funktioniert noch nicht. Es ist kein BBA-Klon, sondern ein
  ENC28J60, fuer den libogc keinen Treiber hat. Steht unter Phase 5.

## Bedienung

| Taste | Wirkung |
|-------|---------|
| Steuerkreuz | Auswahl bewegen |
| L / R | Seitenweise blaettern |
| A | Auswaehlen, bestaetigen |
| B | Zurueck, in der Tastatur loeschen |
| X | Sortierung wechseln, in der Tastatur die Ebene |
| Y | Suche, Quelle hinzufuegen, in der Tastatur Leerzeichen |
| START | Warteschlange starten, Eingabe uebernehmen |
| Z | In der Tastatur abbrechen |
| START + Z | Anwendung beenden (im Hauptmenue) |

## Ein Durchgang zum Pruefen

1. **Starten.** Es erscheint erst die Geraetesuche, dann der
   Verbindungsaufbau. DHCP dauert ein paar Sekunden.

2. **Diagnose** im Hauptmenue. Hier stehen die belegten EXI-Steckplaetze,
   die eingehaengten Speichergeraete mit freiem Platz und die
   IP-Konfiguration. Wenn hier keine IP steht, bringt der Rest nichts.

3. **Quellen.** Trage mit `Y` deinen eigenen Server ein, zum Beispiel
   ein Verzeichnis-Listing im Heimnetz. `A` laedt ihn. Bei Erfolg steht
   die Anzahl der Eintraege rechts in der Zeile.

4. **Katalog.** Die Liste sollte nach Namen sortiert erscheinen. `Y`
   oeffnet die Suche, `X` wechselt die Sortierung durch die vier
   Varianten.

5. **Details** mit `A`. Hier ist der interessante Teil: `X` liest den
   **Disc-Header ueber einen 1-KB-Range-Request** und zeigt die echte
   Spiel-ID samt Region, ohne die 1,4 GB zu laden. Der vierte Buchstabe
   ist die Region, `D` etwa fuer die deutsche Fassung.

6. **In die Warteschlange** mit `A`.

7. **Warteschlange**, `START`. Jetzt laeuft der Download mit
   Fortschritt, Rate und Restzeit.

8. **Abbrechen mit `B`** — und das ist der wichtigste Test. Danach
   `START` erneut: es muss auf der letzten Blockgrenze weitergehen, nicht
   von vorn. Auf der Karte liegen dafuer `<name>.iso.part` und
   `<name>.iso.part.st`. Der Seitenwagen (Format `CFPART3`) enthaelt
   jetzt nur noch `done=` und `crc=`, weil vorwaerts geschrieben wird und
   die `.part` damit immer ein gueltiger Anfang der fertigen Datei ist.
   Die Groesse der `.part` muss zu `done=` passen.

9. **Durchlaufen lassen.** Am Ende wird die mitgelaufene Pruefsumme gegen
   den CRC32 aus den Quellmetadaten geprueft, die `.part` umbenannt und
   der Seitenwagen geloescht. Stimmt sie, dann hat auch die Reihenfolge
   gestimmt, in der der Schreib-Thread die Bloecke angehaengt hat.

## Was realistisch zu erwarten ist

Das haengt fast nur davon ab, **wie weit die Quelle entfernt steht**, und
nicht an der Leitung dahinter. Das lwIP in libogc2 arbeitet mit einem
Empfangsfenster von 2920 Bytes und kennt kein Window Scaling, eine
einzelne Verbindung schafft also nie mehr als 2920 Bytes geteilt durch
die Umlaufzeit:

| Quelle                     | Umlaufzeit | eine Verbindung | 1,36-GB-Image |
|----------------------------|-----------:|----------------:|--------------:|
| Server im eigenen Netz     |      ~1 ms | Adaptergrenze   |      ~30 min  |
| Spiegel in Europa          |     ~30 ms |     97 kB/s     |       ~4 h    |
| Server US-Westkueste       |    ~210 ms |     14 kB/s     |      ~29 h    |

Gemessen wurden mit einer Verbindung 16 kB/s, das passt
auf die Zahl genau.

Darum laufen standardmaessig **acht Bereiche gleichzeitig**. Auf Hardware
gemessen, ENC28J60 im Memory-Card-Slot, Quelle ein Server an der US-Westkueste:

| Verbindungen | Rate      | 1,36-GB-Image |
|--------------|----------:|--------------:|
| 1            |  16 kB/s  |      ~29 h    |
| 8            | 100 kB/s  |      ~4 h     |

Das skaliert also praktisch linear; die Gegenstelle bremst nicht. Die
Hardware ist dabei nicht der Engpass: die Karte schreibt 1,2 MB/s und der
Adapter kann etwa 1 MB/s.

### Mit eigener libogc2

`tools/build-libogc2.sh` baut libogc2 mit einem Fenster von 35040 statt
2920 Bytes. Damit traegt eine Verbindung 168 kB/s, und **sechs reichen fuer
die Adaptergrenze** - Vorgabe ist dann auch 6 statt 8. Zu erwarten sind
rund **1 MB/s und etwa 25 Minuten** fuer ein volles Image.

Welche Fassung in einem DOL steckt, steht oben auf dem Diagnosebildschirm:

```
TCP-Fenster: 35040 (eigene libogc2)   Verbindungen: 6
TCP-Fenster: 2920 (Paket)             Verbindungen: 8
```

Gemessen wurde damit zunaechst 358 kB/s statt 1 MB/s. Ursache waren zwei
Fehler im ENC28J60-Treiber von libogc2, die mit dem kleinen Fenster nie
auffallen konnten und jetzt mit im Patch stehen:

- Der Unterbrechungsbehandler holte **genau ein Paket** ab, ohne Schleife
  ueber die Zahl der wartenden. Was nicht abgeholt wurde, blieb im Chip
  liegen oder ging verloren.
- Der **Empfangsspeicher des Chips** war auf 4 KB eingestellt, also knapp
  drei Rahmen. Jetzt 6 KB; gesendet wird hoechstens ein Rahmen, dafuer
  reichen die anderen 2 KB.

Gebracht hat das wenig: 380 kB/s im Spitzenwert statt 358. Die beiden
Treiberfehler waren echt, aber sie waren nicht die Grenze.

## Netztest

Darum gibt es jetzt eine Messung statt weiterer Vermutungen: in der
**Diagnose die Taste X**. Der Netztest laedt von derselben Adresse wie
der Download, aber **ohne irgendetwas auf die Karte zu schreiben**, und
zwar der Reihe nach mit 1, 2, 4, 6 und 8 Verbindungen, je zwoelf
Sekunden. Angezeigt wird die geglaettete Rate der Engine, nicht Bytes
durch Gesamtzeit - sonst wuerden die Sekunden fuer den Verbindungsaufbau
gerade die hohen Werte kleinrechnen.

Gebraucht wird dafuer eine geladene Quelle oder ein Titel in der
Warteschlange, denn der Test braucht eine echte Datei als Gegenstelle.
Abbrechen mit B.

So ist das Ergebnis zu lesen:

| Beobachtung | Was es heisst |
|---|---|
| Rate steigt mit der Zahl der Verbindungen | Jede einzelne Verbindung ist die Grenze, also Fenster mal Umlaufzeit. Mehr Verbindungen helfen. |
| Rate bleibt ab 2 oder 4 gleich | Etwas Gemeinsames begrenzt: Adapter, EXI-Bus oder Treiber. Mehr Verbindungen helfen dann nicht. |
| Rate deutlich hoeher als beim echten Download | Das Schreiben auf die Karte kostet den Unterschied. |

Die Zahlen bitte alle fuenf melden, nicht nur den Spitzenwert - die Form
der Reihe sagt mehr als jeder einzelne Wert.

### Was dabei herauskam

| Verbindungen | 1 | 2 | 4 | 6 | 8 |
|---|---:|---:|---:|---:|---:|
| ohne Schreiben | 163 | 283 | 465 | 537 | 606 kB/s |

Eine Verbindung bringt 163 kB/s, gerechnet waren 168. Das grosse Fenster
wirkt also genau wie vorhergesagt. Darueber liegt eine gemeinsame Grenze
bei etwa 600 bis 700 kB/s, erkennbar daran, dass die Reihe abflacht.

Der echte Download lief gleichzeitig mit 380 kB/s - die Differenz von
37 Prozent war das Schreiben auf die Karte, das abwechselnd mit dem Lesen
lief statt gleichzeitig. **Geschrieben wird jetzt in einem eigenen
Thread**, damit laufen Karte und Netz parallel.

Zu erwarten sind damit **um 600 kB/s und knapp 40 Minuten** fuer ein
Image - und seit dem Umbau auf Bloecke **ohne** die 20 Minuten
Vorbelegung davor.

Dazu ist der Treiber noch einmal entlastet worden: `EPKTCNT` wird nur
noch einmal je Unterbrechung gelesen statt nach jedem Rahmen. Das Register
liegt in Bank 1, der Rest der Schleife in Bank 0 - es kostete also zwei
Bankumschaltungen je Rahmen. Statt **12 Buszugriffen je Rahmen sind es
jetzt 7**, davon zwei mit Daten. Ob das die 606 kB/s hebt, muss der
Netztest zeigen.

Worauf beim Test zu achten ist:

- **Erst den Netztest** (Diagnose, X), dann den echten Download. Nur so
  ist zu sehen, welche der beiden Aenderungen was gebracht hat: der
  Netztest misst den Treiber, der Download zusaetzlich den
  Schreib-Thread.

- Steht die Rate des echten Downloads jetzt nahe am Netztest, hat der
  Thread getan, was er soll.
- **Die Pruefsumme am Ende ist der wichtige Teil.** Sie prueft nicht nur
  den Download, sondern auch, dass der Schreib-Thread jeden Puffer an die
  richtige Stelle gelegt hat.
- Abbrechen und Fortsetzen bitte auch noch einmal: der Stand im
  Seitenwagen kommt jetzt vom Thread, nicht mehr aus der Hauptschleife.

In der Anzeige steht mit, wie viele der Verbindungen gerade tragen.
Bricht eine weg, baut sie sich neu auf und der Balken laeuft weiter; nur
wenn eine Verbindung sechsmal hintereinander nicht zustande kommt, gilt
der Download als gescheitert.

Eine **Aenderung der Zahl greift nicht** fuer eine schon angefangene
`.part`: deren Aufteilung steht im Seitenwagen und wird beim Fortsetzen
genau so weitergefuehrt.

Bleibt die Rate auch im eigenen Netz niedrig, dann lohnt der Blick auf
die EXI-Kombination.

Nach `START` kommt nur noch ein kurzer Augenblick ohne Datenverkehr: die
Datei anlegen und die Verbindungen aufbauen. Dann laeuft der Balken.

**Die Vorbelegung ist weg.** Sie war unvermeidlich, solange gleichzeitig
an verschiedene Stellen der Datei geschrieben wurde: libfat fuellt beim
Verlaengern alles davor mit Nullen (`_FAT_file_extend_r` ruft `memset`
und `_FAT_cache_writeSectors`), FAT kennt keine Loecher. Das kostete
1,36 GB Schreibarbeit, gemessen 20 Minuten, und daran war durch eine
andere Aufteilung nicht zu drehen.

Jetzt wird **streng vorwaerts geschrieben**: die Datei wird in Bloecke
von 1 MB geteilt, fertige Bloecke warten im Speicher, und der
Schreib-Thread haengt immer nur den an, der als naechster dran ist. Die
Datei waechst damit lueckenlos. Auch die Einstellung "Datei vorbelegen"
ist weg - sie haette nichts mehr getan.

Die Schreibgeschwindigkeit der Karte (1,2 MB/s, gemessen an der
damaligen Vorbelegung) ist damit nicht mehr sichtbar, aber auch nicht
mehr wichtig: sie ist doppelt so hoch wie alles, was ueber das Netz
hereinkommt.

## Wenn etwas nicht geht

**Keine IP.** Adapter sitzt nicht richtig, Kabel, oder kein DHCP im Netz.
Die Anwendung kann keine feste Adresse, nur DHCP.

**"Name nicht aufloesbar".** libogc bringt fuer den GameCube keine
Namensauflösung mit, CubeFetch hat darum einen eigenen DNS-Client. Als
Resolver nimmt er das Gateway aus dem DHCP-Lauf. Wenn der Router kein DNS
weiterleitet, in `cubefetch/config.txt` eintragen:

```
dns=192.168.1.1
```

Ein oeffentlicher Resolver geht genauso. Zum Pruefen, ob es wirklich DNS
ist: eine Quelle mit Zahlenadresse eintragen, die wird ohne Anfrage
verwendet.

**"HTTPS wird nicht unterstuetzt".** Richtig, absichtlich. Der GameCube
hat keine TLS-Implementierung. Die Quelle muss ueber `http://`
erreichbar sein. Jeder einfache Webserver im Heimnetz kann das.

**Bild flackert.** War ein Fehler in der Oberflaeche und ist behoben: die
Menues bauen sich jedes Bild neu auf, und das Loeschen des Fensters fiel
dabei mitten in den Bildaufbau. Gezeichnet wird jetzt in einen
Schattenpuffer. Flackert es trotzdem noch, dann bitte melden, mit welchem
Menue.

**Ueber der unteren Trennlinie stand Text halb abgeschnitten doppelt.**
Auch behoben, und auch das war libogc: `__console_clear` schreibt
`con_xres * con_yres / 2` Woerter linear ab dem Fensteranfang und
beachtet die Zeilenbreite des Bildpuffers nicht. Bei einem Fenster, das
so breit ist wie der Bildpuffer, geht das zufaellig auf - mit unserem
Rand von 20 Pixeln deckt es nur 412 der 440 Zeilen ab, und die unteren
27 blieben stehen, mitten in einer Zeichenzeile aufhoerend. Dort stand
die Fusszeile des vorigen Bildes. Geloescht wird jetzt der
Schattenpuffer selbst, ohne die Bibliothek.

**Oberes Bilddrittel schwarz oder verschoben.** Das ist kein Fehler der
Anwendung, sondern die Synchronisation des Fernsehers; CubeFetch nimmt den
Modus, den die Konsole als bevorzugt meldet. Eine Umschaltung zwischen
576i, 480i und 480p ist vorgesehen, aber noch nicht gebaut.

**Download bricht immer an derselben Stelle ab.** Die `.part` und die
`.part.st` loeschen und neu anfangen.

**Pruefsumme stimmt nicht.** Die Datei bleibt als `.part` liegen, damit
nichts Falsches unter dem richtigen Namen landet. Tritt das nach einem
harten Absturz auf, ist eher der mitgefuehrte Zwischenstand schuld als
die Datei; die Anwendung zeigt in dem Fall an, dass die Pruefsumme nicht
verwendbar ist.

## In Dolphin statt auf Hardware

Dolphin emuliert den Broadband Adapter und kann ein SD-Karten-Image
einhaengen. Damit laeuft der groesste Teil ohne Hardware.

- Unter Config, GameCube: Slot A oder SP1 auf **Broadband Adapter (HLE)**
- SD-Karten-Image einhaengen, damit `carda:` erscheint

Was Dolphin **nicht** zeigt: EXI-Konflikte, echte Durchsatzraten und
Fragmentierung. Dafuer braucht es die Konsole.

## Wenn eine Verbindung abbricht

Das soll vorkommen duerfen, und es wird abgefangen - je Block einzeln:

- **Verbindung weg** → Sockel zu, der Block behaelt, was schon
  angekommen ist, und die naechste Anfrage holt genau den Rest
  (`Range: bytes=<anfang+geladen>-<ende>`). Kein Byte doppelt, keines
  uebersprungen.
- **Nichts kommt mehr** (30 Sekunden still) → gilt als haengend und wird
  genauso behandelt.
- **Aufbau scheitert** → eine halbe Sekunde Pause, dann wieder. Sechs
  Versuche hintereinander, dann gilt der Download als gescheitert.
- **Der Zaehler wird auf Null gesetzt, sobald wieder Daten kommen.** Die
  sechs Versuche sind fuer eine Verbindung, die nicht zustande kommt,
  nicht als Budget fuer Stunden Betrieb.
- Scheitert es doch: der Seitenwagen behaelt den Stand, `START` macht an
  der letzten Blockgrenze weiter.

Zu sehen ist das an der Zeile "x von n Verbindungen aktiv" im
Fortschritt. Steht da zwischendurch eine weniger, hat sich gerade eine
neu aufgebaut.

## Oberflaeche und GameTDB

Die Oberflaeche ist jetzt gezeichnet (GX, FreeType). Zwei Dinge, die
dabei zu pruefen sind:

- **Lesbarkeit auf der Roehre.** Text ist 18 Punkte, Hinweise 14. Wenn
  die Umlautpunkte im Zeilensprung flimmern oder die kleine Schrift
  nicht zu lesen ist, bitte melden - dann gehen die Groessen hoch.
- **Rand.** Der sichtbare Bereich laesst 28 Pixel links und rechts und
  20 oben und unten frei. Schneidet der Fernseher trotzdem etwas ab,
  bitte sagen, welche Seite.

**GameTDB laden:** Einstellungen, Zeile *GameTDB-Datenbank*, `A`. Es
werden 2,3 MB von www.gametdb.com geholt und dabei entpackt und geparst;
das dauert ein bis zwei Minuten und zeigt einen Balken. Ergebnis ist
`/cubefetch/gametdb/games-DE.tsv` (bzw. `-EN`) und die Zahl der
uebernommenen GameCube-Spiele - erwartet werden 1.708 (Stand November
2026, gegen die echte Datei am PC geprueft).

Danach zeigt das Detailfeld im Katalog zu jedem Eintrag mit Disc-ID
Jahr, Herausgeber, Entwickler, Genre und Spielerzahl. Fuer Eintraege ohne
ID greift die Namenszuordnung; sie steht dann als *Zuordnung ueber den
Namen, n % Aehnlichkeit* dabei. Mit `X` in der Detailansicht liest man
die echte Disc-ID aus dem Header, dann wird die Zuordnung exakt.

**Cover** erscheinen, wenn die Auswahl etwa eine halbe Sekunde steht -
beim Durchrollen bleibt der Rahmen mit der Kennung stehen, das ist so
gewollt. Geholt wird von art.gametdb.com, abgelegt unter
`/cubefetch/covers/<ID>.png`; beim zweiten Mal kommt es von der Karte.
Gibt es unter keiner Sprache ein Bild, steht *kein Cover* im Rahmen.

**Sortieren und Filtern nach Jahr und Genre:** erst die GameTDB-Datenbank
laden (Einstellungen), dann im Katalog `X` fuer die Sortierung - neu sind
*Jahr, aelteste zuerst* und *Jahr, neueste zuerst*; Eintraege ohne Jahr
stehen in beiden Faellen am Ende. `Z` schaltet durch die Genres, die im
Katalog vorkommen, und am Ende zurueck auf alle. Das aktive Genre steht
oben neben der Suche. Das Jahr steht in der Liste vor der Groesse.

Nach dem Laden einer Quelle erscheint kurz *Spielinfos werden
zugeordnet*. Bei ~750 Eintraegen sollte das unter zehn Sekunden dauern;
wenn es deutlich laenger braucht, bitte die Zeit melden.

## Keep-Alive und die springende Anzeige

Gemeldet: bei 12 Verbindungen sprang die Anzeige staendig zwischen
Balken und "Verbindungen werden aufgebaut", und es kamen Spitzen ueber
1,4 MB/s. Beides hatte dieselbe Ursache. Jeder Block (1 MB) oeffnete eine
neue Verbindung - DNS, Handshake, Anfrage, Warten auf die Kopfzeilen,
rund 0,4 s -, und dabei blockierte die Schleife fuer **alle**
Verbindungen. Die Daten stauten sich in lwIP und kamen danach in einem
Rutsch: daher die Spitzen. Ein 10-MBit-Adapter schafft hoechstens
1,19 MB/s Nutzlast; was darueber angezeigt wurde, waren aufgestaute
Daten nach einer Pause.

Jetzt bleibt jede Verbindung offen und traegt den naechsten Block mit
einer neuen Anfrage (Keep-Alive, auf Hardware geprueft). Die
Anfrage wird nur geschickt, die Kopfzeilen werden gelesen, wenn sie da
sind - nichts blockiert mehr. Die Anzeige "Verbindung wird aufgebaut"
erscheint nur noch vor dem ersten Byte.

Zu erwarten: eine ruhigere Rate ohne Spitzen ueber 1,2 MB/s, aber ein
hoeherer **Mittelwert**. Der zaehlt, nicht die Spitze - am besten an der
Gesamtdauer eines Images ablesen.

## Katalog: Lauftext, Blaettern, Ruckeln

- **Lange Titel laufen durch**, sobald ein Eintrag angewaehlt ist: kurze
  Pause, dann langsam nach links, Pause am Ende, wieder von vorn. So in
  der Liste, im Detailfeld, in der Detailansicht und in der
  Warteschlange. Nicht angewaehlte Zeilen bleiben gekuerzt.
- **Links/Rechts blaettern seitenweise**, oben steht *Seite x / y*.
  Hoch/Runter geht weiter zeilenweise, L/R springt wie bisher um eine
  Seite ohne auf den Seitenanfang zu setzen.
- **Ruckeln beim Rollen**: lag nicht an der Laenge der Liste - gezeichnet
  werden ohnehin nur die zwoelf sichtbaren Zeilen. Zwei Dinge liefen bei
  jedem Schritt: das Cover wurde von der Karte gelesen und dekodiert
  (100 bis 150 ms), und fuer Eintraege ohne Disc-ID lief eine Suche ueber
  alle 1.708 Datenbanktitel. Jetzt kommt das Cover beim Rollen nur aus
  dem Speicher, von der Karte erst nach 250 ms Stillstand, vom Netz nach
  400 ms; die Zuordnung steht seit dem Nachtragen im Eintrag.

Wenn es danach noch ruckelt, bitte sagen, wo - beim schnellen Halten der
Taste oder bei einzelnen Schritten.
