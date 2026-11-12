# Vorschaubilder und Spielinfos

## Das Zuordnungsproblem

Dateinamen eigener Sicherungen sehen oft so aus:

```
Mein Spiel.iso
Mein Spiel (Germany).iso
Anderes Spiel (Germany, France) (v1.01).iso
```

Ein Ähnlichkeitsvergleich über den Namen trifft hier oft daneben. Viele Dateien
tragen **gar keinen** Regionszusatz, obwohl sie PAL sind, und Varianten wie `(Germany)`, `(Germany, France)`,
`(Player's Choice)`, `(v1.01)` oder `(Disc 1)` stehen alle im selben Feld.
Welche Fassung eines Spiels vorliegt, lässt sich dem Namen nicht verlässlich
entnehmen.

## Die Lösung: Disc-ID vor dem Download lesen

Jedes GameCube-Image beginnt mit einem Header:

| Offset | Länge | Inhalt                       |
|--------|-------|------------------------------|
| 0x00   | 6     | Game-ID, sechs Zeichen      |
| 0x1C   | 4     | Magic `0xC2339F3D`           |
| 0x20   | 64    | Interner Spieltitel          |

Ein einzelner `Range`-Request über die ersten 1024 Bytes liefert das.

Damit ist die Zuordnung **exakt statt geschätzt**, und die Region fällt als
Nebenprodukt ab: eine ID mit `D` an vierter Stelle ist die deutsche Fassung,
auch wenn der Dateiname nichts dazu sagt. Das Magic bestätigt zugleich, dass
es überhaupt ein gültiges GC-Image ist, bevor eine halbe Stunde Download
beginnt.

Kosten: 1 KB und eine Laufzeit pro Titel. Deshalb wird die Probe **träge**
ausgeführt — nur für den gerade markierten Eintrag, nicht für die ganze Liste —
und das Ergebnis dauerhaft auf der Karte vermerkt.

Regionsbuchstabe ist Stelle 4 der ID: `E` USA, `P` Europa, `D` Deutschland,
`F` Frankreich, `S` Spanien, `I` Italien, `J` Japan, `U` Australien.

## Rückfallweg

Wenn eine Quelle kein `Range` beherrscht oder die Datei kein ISO ist, greift
der Namensvergleich: Titel normalisieren (Kleinschreibung, Klammerzusätze und
Interpunktion entfernen, Artikel am Ende wie `, The` nach vorn ziehen), dann
Token-Set-Ähnlichkeit gegen die Titelliste. Ab etwa 0,85 wird automatisch
zugeordnet, zwischen 0,70 und 0,85 als Vorschlag markiert, darunter verworfen.
Ein Treffer über diesen Weg wird in der Oberfläche als unsicher gekennzeichnet.

## Datenquelle: GameTDB

GameTDB ist für genau diesen Zweck gemacht und die Grundlage von USB Loader GX
und WiiFlow. Zwei Teile:

**Bilder** — `http://art.gametdb.com/wii/<art>/<REGION>/<ID>.png`. GameCube-
Titel liegen im `wii`-Zweig. Geprüft und bestätigt, **über reines HTTP**:

| Adresse                                       | Ergebnis         |
|-----------------------------------------------|------------------|
| `wii/cover/US/GALE01.png`                     | 200, 76 KB       |
| `wii/coverfull/US/GALE01.png`                 | 200, 347 KB      |
| `wii/cover3D/US/GALE01.png`                   | 200, 85 KB       |
| `wii/disc/US/GALE01.png`                      | 200, 48 KB       |
| `wii/cover/EN/GW7P69.png`                     | 200, 70 KB       |
| `wii/cover/DE/GW7P69.png`                     | 200, 74 KB       |

Der Regionsordner ist **nicht** der Regionsbuchstabe der ID, sondern ein
Sprachkürzel. `wii/cover/US/GW7P69.png` gibt 404, `…/EN/…` und `…/DE/…` geben
200. Also wird eine Kette durchprobiert, abgeleitet aus der ID:

| ID-Region | Kette               |
|-----------|---------------------|
| `E`       | `US` → `EN`         |
| `P`       | `EN` → `US`         |
| `D`       | `DE` → `EN` → `US`  |
| `F`       | `FR` → `EN` → `US`  |
| `S`       | `ES` → `EN` → `US`  |
| `I`       | `IT` → `EN` → `US`  |
| `J`       | `JA` → `US` → `EN`  |

Bei Oberflächensprache Deutsch wird `DE` zusätzlich vorangestellt, damit die
deutsche Packung gezeigt wird, wenn es sie gibt.

Geladen wird `cover` (klein, für die Liste) und bei Bedarf `coverfull` für die
Detailansicht. Dekodiert mit libpng und zlib aus den devkitPro-portlibs, dann
als GX-Textur. Das Ergebnis wird in der dekodierten Form auf der Karte
zwischengespeichert, damit Umblättern in der Liste nicht jedes Mal erneut lädt
und dekodiert.

**Textdaten** — `wiitdb.txt` ist eine Liste `ID = Titel`, 10.172 Zeilen und
338 KB für die GameCube-Auswahl. Trivial zu parsen. Verlag, Erscheinungsjahr,
Genre, Spielerzahl und Beschreibung stecken in `wiitdb.xml` im Zip-Archiv.

Der Haken von damals ist weg: `www.gametdb.com` antwortet inzwischen **auch über
reines HTTP** — geprüft am 9. November 2026, `titles.txt` 200 OK mit 340 KB und
`Accept-Ranges`, `wiitdb.zip` 206 auf eine Bereichsanfrage, 1,98 MB. Die Konsole
kann die Datenbank also selbst holen.

## Umsetzung

**Datenbank.** In den Einstellungen unter *GameTDB-Datenbank* mit `A` laden.
Geholt wird `wiitdb.zip?LANG=<DE|EN>&GAMECUBE=TRUE` (2,3 MB), **im Strom entpackt** (zlib, roher
Deflate aus dem Zip-Eintrag) und beim Entpacken gleich geparst: `cf_gametdb`
sammelt `<game>`-Blöcke und nimmt nur die mit `<type>GameCube</type>`.

**`GAMECUBE=TRUE` ist Pflicht.** Ohne den Parameter enthält das Archiv nur
Wii-Titel: 5.016 Einträge, kein einziger GameCube-Titel, `GALE01` und `GM8E01`
fehlen. Genau daran ist der erste Versuch auf der Konsole gescheitert. Seitdem
läuft der Parser einmal gegen die echte Datei (12 MB XML in 4-KB-Stücken wie auf
der Konsole): **1.708 GameCube-Spiele**, 1.689 davon mit Jahr, 1.597 mit Genre.
Die 12 MB
XML berühren weder die Karte noch den Speicher als Ganzes. Heraus kommt
`/cubefetch/gametdb/games-DE.tsv`, eine Zeile je Spiel:

```
ID  Jahr  Monat  Tag  Spieler  Herausgeber  Entwickler  Genre  Titel
```

Beim Start wird die Datei in einen nach ID sortierten Index geladen (Binärsuche,
1.708 GameCube-Titel, etwa 550 KB im Speicher). Der Titel steht in der
Sprache der Oberfläche, wenn GameTDB ihn hat, sonst englisch.

**Zuordnung.** Hat ein Eintrag eine Disc-ID (aus dem Header gelesen oder von der
Quelle mitgeliefert), ist der Treffer exakt. Sonst greift `cf_fuzzy`: beide Titel
normalisiert (Kleinschreibung, Umlaute zu ae/oe/ue, Klammerzusätze weg, Satzzeichen
weg, `, The` nach vorn), dann Wortmengen verglichen. Das Maß ist **halb Dice, halb
Überdeckung der kleineren Menge** — Dice allein bestraft Reihentitel
(„007 – Nightfire" gegen „James Bond 007: Nightfire" ergäbe 0,67), Überdeckung
allein ließe „Mario" auf alles mit Mario passen. Die Mischung gibt dem 007-Fall
0,83 und „Super Mario Sunshine" gegen „Super Mario Strikers" 0,67. Ab 0,70 wird
zugeordnet und in der Oberfläche als *Zuordnung über den Namen, n % Ähnlichkeit*
gekennzeichnet. Die Suche läuft über alle Titel und wird darum je Eintrag nur
einmal gemacht.

**Cover.** `http://art.gametdb.com/wii/cover/<SPRACHE>/<ID>.png`, 160 × 224, in
der Kette oben; bei deutscher Oberfläche `DE` zuerst. Die Datei landet unverändert
unter `/cubefetch/covers/<ID>.png`, dekodiert wird mit libpng nach RGBA8 und ins
Kachelformat des Grafikchips. Acht dekodierte Cover bleiben im Speicher. Ein Cover,
das unter keiner Sprache existiert, wird für die Sitzung gemerkt und nicht erneut
angefragt.

Geholt wird **nur, wenn die Auswahl 400 ms steht**. Jeder Abruf blockiert etwa
eine halbe Sekunde (Umlaufzeit plus 70 KB); beim Durchrollen würde das jede
Zeile stocken lassen. In der Detailansicht wird sofort geholt.

**Sortierung und Filter.** Nach dem Laden einer Quelle und nach dem Laden der
Datenbank werden Jahr und Genre einmal in jeden Katalogeintrag geschrieben —
exakt über die ID, sonst über den Namen. Der Katalogkern sortiert dann auch nach
Jahr (unbekanntes Jahr in beiden Richtungen ans Ende, sonst stünden bei
„älteste zuerst" alle Einträge ohne Daten oben) und filtert nach Genre. Ein Genre
passt nur als ganzes Feld: „action" trifft nicht „action-adventure". Im Katalog
schaltet `Z` durch die Genres, die tatsächlich vorkommen.

Damit das Zuordnen auf der Konsole nicht ewig dauert, normalisiert der Index alle
Datenbanktitel einmal beim Laden. Vorher hätte jeder der Hunderttausenden Vergleiche (Katalog × 1.708)
beide Seiten neu normalisiert — UTF-8 umsetzen, Klammern entfernen —, und das ist
der teure Teil.

## Warum nicht SteamGridDB

Für diesen Zweck der schlechtere Weg, aus vier Gründen:

1. **Nur HTTPS.** Dieselbe Hürde wie oben, aber diesmal für *jedes Bild*, nicht
   für eine einmalige Datei.
2. **API-Schlüssel nötig.** In einer quelloffenen Homebrew-Anwendung lässt sich
   ein Schlüssel nicht unterbringen, ohne dass er ausgelesen und missbraucht
   wird — mit dem Konto des Projekts als Haftendem.
3. **Steam-zentriert.** Die Zuordnung läuft über Steam-App-IDs. GameCube-Titel
   sind als Nicht-Steam-Einträge nur lückenhaft erfasst.
4. **Falsche Bildsorte.** Dort liegen moderne Store-Grafiken, Hero-Banner und
   Logos. Gebraucht wird die GameCube-Originalpackung — und genau die hat
   GameTDB, mit der Disc-ID als Schlüssel.

Sinnvoll bleibt es als **Ergänzung**: wer mag, trägt in Phase 5 einen eigenen
Schlüssel in die Konfiguration ein und bekommt zusätzlich Hero-Grafiken und
Logos für die Detailansicht. Als Grundlage ist GameTDB richtig.
