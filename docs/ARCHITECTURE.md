# Aufbau

## Hardware und EXI-Belegung

Speicherkarten-Steckplätze und Serial Ports hängen beim GameCube alle am
EXI-Bus. Das ist der Grund, warum sich manche Adapter gegenseitig ausschließen.

| Anschluss             | EXI    | Netzwerk                          | Speicher                  |
|-----------------------|--------|-----------------------------------|---------------------------|
| Memory Card Slot A    | EXI0:0 | GCNET u. a. ENC28J60-Adapter      | SD Gecko                  |
| Memory Card Slot B    | EXI1:0 | GCNET u. a. ENC28J60-Adapter      | SD Gecko                  |
| Serial Port 1 (SP1)   | EXI0:2 | BBA DOL-015, ETH2SP1 (W5500)      | SD2SP1, M.2-Adapter (SP1) |
| Serial Port 2 (SP2)   | EXI2:0 | ETH2GC Lite/Sidecar (ENC28J60)    | SD2SP2, M.2 Loader (SP2)  |

Daraus folgt:

- **Netz an SP1 oder SP2 + SD2SP2 bzw. SD2SP1** — getrennte EXI-Kanäle, solange
  nicht beides am selben Port hängen soll. Empfohlen.
- **Netz im Memory-Card-Slot + SD2SP2 (SP2)** — EXI0 oder EXI1 gegen EXI2.
  Getrennte Kanäle. Auf echter Hardware geprüft.
- **Netz in Slot A + SD Gecko in Slot B** — EXI0 gegen EXI1, funktioniert.
- **Netz an SP1 + SD Gecko in Slot A** — beide auf EXI0. Nur ein Gerät darf
  gleichzeitig ausgewählt sein; in der Praxis fehleranfällig. Swiss hat zu genau
  dieser Kombination Fixes im Changelog. CubeFetch erkennt den Fall und warnt,
  statt still langsam oder instabil zu werden.
- **Netz und Speicher im selben Steckplatz** — physisch unmöglich. Mit ETH2GC am
  SP2 muss der Speicher in einen Memory-Card-Slot, mit einem Netzwerkadapter im
  Memory-Card-Slot umgekehrt.
- **DOL-101 und Panasonic Q** haben kein SP2. Dort bleibt SP1 bzw. der
  Memory-Card-Slot.

Kein Adapter außer dem echten BBA ist ein BBA-Klon: ETH2GC und die Adapter für
den Memory-Card-Slot tragen einen **ENC28J60**, ETH2SP1 einen **W5500**. Swiss
emuliert darüber für Spiele einen BBA, aber nur für Programme gegen das
Dolphin-SDK — Homebrew braucht laut Swiss' eigener Dokumentation native Treiber.
Darum baut CubeFetch gegen **libogc2** statt gegen devkitPros libogc, siehe
unten.

## Werkzeugkette: libogc2 statt libogc

devkitPros `libogc` bringt genau einen Netzwerktreiber mit, den für den echten
Broadband Adapter, und der sucht ihn ausschließlich an **EXI0:2** (SP1) und
akzeptiert nur die Gerätekennung `0x04020200`. Ein Adapter im Memory-Card-Slot
wird damit nie gefunden — unabhängig davon, dass dort auch der falsche Chip
sitzt.

[libogc2](https://github.com/extremscorner/libogc2) (der Fork, gegen den auch
Swiss gebaut wird) bringt eigene Treiber für **ENC28J60**, **W5500** und
**W6x00** mit. Sein `if_configex` probiert der Reihe nach BBA, W6x00, W5500 und
ENC28J60, und jeder Chiptreiber tastet dabei vier Steckplätze ab: EXI0:2 (SP1),
EXI2:0 (SP2), EXI0:0 (Slot A) und EXI1:0 (Slot B). Erst damit läuft ein Adapter
im Memory-Card-Slot.

Unterschiede, die den Quelltext betreffen:

- `if_config` hat vier Parameter, nicht fünf (kein `max_retries`).
- Es gibt kein `netinet/in.h`; `in_addr` und Verwandte stehen in `network.h`.
- `fatMountSimple` nimmt ein `DISC_INTERFACE *` ohne `const`.
- Namensauflösung fehlt auch hier, der eigene DNS-Client bleibt also nötig.

**Die devkitPro-portlibs (`ppc-zlib`, `ppc-libpng`, `ppc-freetype`) sind zu
libogc2 nicht ABI-kompatibel.** Sie werden darum nicht eingebunden, `LIBDIRS`
bleibt absichtlich leer. Deshalb steht CRC32 als `core/cf_crc32.c` im Projekt
statt aus zlib zu kommen. Für Phase 3 und 4 (FreeType, libpng) müssen die
passenden Pakete `ppc-*` aus dem libogc2-Repository daneben installiert werden —
die vorhandenen zu ersetzen wäre keine gute Idee, weil das jedes andere
libogc-Projekt auf demselben Rechner trifft.

Beschafft wird libogc2 entweder aus dem Paketrepository von Extrems oder
projektlokal mit `tools/get-libogc2.sh` nach `toolchain/libogc2/` (signaturgeprüft,
nicht im Repository). Das Makefile nimmt eine Installation unter
`$DEVKITPRO/libogc2` bevorzugt und fällt sonst auf `toolchain/libogc2` zurück;
mit `make LIBOGC2=/pfad` lässt sich beides übergehen.

## Realistische Geschwindigkeit

Die Adapter sind 10 MBit, also rund 1,1 MB/s theoretisch und etwa 0,7–0,9 MB/s
in der Praxis. **Diese Zahl gilt aber nur im lokalen Netz.** Über das Internet
begrenzt etwas anderes, und zwar hart:

Das lwIP in libogc und libogc2 ist mit `TCP_WND = 2 * TCP_MSS = 2920` übersetzt
— im Maschinencode nachlesbar, `tcp_recved` klemmt `pcb->rcv_wnd` auf genau
2920. Window Scaling kennt dieses lwIP nicht. Mehr als 2920 Bytes dürfen also
nie gleichzeitig unterwegs sein, und daraus folgt eine Obergrenze, die nur von
der Umlaufzeit abhängt:

```
Durchsatz = 2920 Bytes / Umlaufzeit
```

| Gegenstelle                  | Umlaufzeit | Obergrenze  | 1,36-GB-Image |
|------------------------------|-----------:|------------:|--------------:|
| Server im eigenen Netz       |      ~1 ms | > Adapter   |      ~30 min  |
| Spiegel in Europa            |     ~30 ms |   97 kB/s   |      ~4 h     |
| Server an der US-Westküste   |    ~210 ms |   14 kB/s   |     ~29 h     |

Auf Hardware gemessen: 16 kB/s von einem Server an der US-Westküste. Das ist nicht der Adapter, nicht
die Karte und nicht der Code — das ist das Bandbreiten-Verzögerungs-Produkt
eines 2920-Byte-Fensters.

Dagegen hilft nur, mehrere Verbindungen gleichzeitig zu fahren: jede bekommt ihr
eigenes Fenster, der Durchsatz vervielfacht sich entsprechend. Das lwIP von
libogc2 bietet dafür Platz, nachgesehen in `memp_num`: 16 TCP-PCBs, 128 Segmente
und 128 Pool-Puffer.

**Das ist gebaut**, und gemessen: die Datei wird in Bereiche geteilt, jeder lädt
mit eigener `Range`-Anfrage, eine `net_select`-Schleife bedient alle.

Auf Hardware, ENC28J60 im Memory-Card-Slot, Quelle ein Server an der US-Westküste:

| Verbindungen | Rate      | je Verbindung | 1,36-GB-Image |
|--------------|----------:|--------------:|--------------:|
| 1            |  16 kB/s  |    16 kB/s    |      ~29 h    |
| 8            | 100 kB/s  |  12,5 kB/s    |      ~4 h     |

Es skaliert praktisch linear — die Gegenstelle bremst bei acht Verbindungen
nicht, und der Engpass ist weiter das Fenster, nicht die Hardware. Zum Vergleich:
die SD-Karte schreibt **1,2 MB/s** (aus der Dauer der Vorbelegung, 1,36 GB in
20 Minuten), der Adapter könnte mit 10 MBit etwa 1 MB/s. Beide sind um den
Faktor zehn von dem entfernt, was ankommt.

Die Decke liegt damit nicht beim Netz, sondern bei der Zahl der Sockel: das Feld
`sockets` in `network.o` ist 320 Bytes groß, bei 20 Bytes je Eintrag also **16
Sockel** für TCP und UDP zusammen. `CF_DL_MAX_CONN` ist deshalb 12 und lässt
vier frei, unter anderem für die Namensauflösung. Mehr als etwa **200 kB/s** sind
mit diesem lwIP von einer Quelle in Kalifornien nicht zu holen. Bei einer Quelle
in Europa (30 ms) reichen schon drei Verbindungen an die Adaptergrenze.

Vorgabe sind 8, weil nur die auf Hardware gelaufen sind.

## Wo die Grenze des GameCube wirklich liegt

Die Kette hat vier Glieder. Keines davon ist das, was heute bremst.

| Glied | Grenze | Rechnung |
|---|---:|---|
| **Leitung** | ~1,1 MB/s | 10 MBit sind 1,25 MB/s brutto; nach Ethernet-, IP- und TCP-Kopf bleiben 1460 von 1538 Bytes, also 95 %. Alle GameCube-Adapter sind 10 MBit — der echte BBA (MX98728EC) wie der ENC28J60. Nur W5500 und W6100 können 100 MBit, dann rückt die Grenze auf EXI. |
| **EXI-Bus** | ~4 MB/s | 32 MHz, ein Bit je Takt. Netz und Karte liegen bei getrennten Steckplätzen auf verschiedenen Kanälen und stören sich nicht. |
| **SD-Karte** | 1,2 MB/s | gemessen an der damaligen Vorbelegung: 1,36 GB in 20 Minuten. |
| **CPU** | ~50 MB/s | 486 MHz Gekko, CRC32 über Tabelle etwa 8 Takte je Byte. Bei 1 MB/s sind das 2 % Rechenzeit. |

**Also rund 1 MB/s**, gesetzt vom 10-MBit-Adapter, mit der Karte knapp darüber.
Ein volles Image wäre damit in etwa **25 Minuten** geladen statt in vier Stunden.
Mit einem W5500-Adapter an SP1 wäre die Karte das schwächste Glied, also etwa
20 Minuten — mehr gibt dieser Rechner für diese Aufgabe nicht her.

Zwischen 1 MB/s und den gemessenen 100 kB/s steht allein das Empfangsfenster.
Was gleichzeitig unterwegs sein muss, um eine Leitung mit der Umlaufzeit *t* zu
füllen, ist das Bandbreiten-Verzögerungs-Produkt:

```
1 MB/s × 0,209 s = 209 KB müssen unterwegs sein
```

Vorhanden sind 8 × 2920 Bytes = 23 KB. Genau im Verhältnis dazu kommen 100 kB/s
an statt 1 MB/s — die Messung bestätigt die Rechnung auf zehn Prozent.

### Eigene libogc2

`tools/build-libogc2.sh` baut libogc2 mit vier geänderten Konstanten
(`tools/libogc2-window.patch`):

```
TCP_WND            2 -> 24 * TCP_MSS   2920 -> 35040 Bytes
PBUF_POOL_SIZE     128 -> 512          Platz für acht solche Fenster
MEMP_NUM_TCP_SEG   128 -> 256
MEM_SIZE           192 -> 512 KB
```

Damit sind es 35040 / 0,209 s = **168 kB/s je Verbindung**, und sechs Verbindungen
reichen für die 209 KB, die der Adapter braucht. Kosten: rund 1,4 MB statischer
Speicher für lwIP statt 400 KB, bei 24 MB im Gerät.

Dass das Fenster so klein ist, ist kein Speichergeiz: `TCP_SND_BUF` steht in
derselben Datei auf 36 × `TCP_MSS`. Der Empfangswert ist einfach auf dem
lwIP-Standard geblieben — für eine Bibliothek, die vor allem Netzwerkspiele im
lokalen Netz bedient, völlig ausreichend. Im eigenen Netz (Umlaufzeit ~1 ms)
trägt eine einzige Verbindung schon 2,9 MB/s und damit mehr, als der Adapter kann.

Mehr als 65535 Bytes Fenster gehen nicht: Window Scaling kennt dieses lwIP
nicht. Das ist aber keine Fessel, denn bei 64 KB je Verbindung wären schon vier
Verbindungen über der Adaptergrenze.

Das Makefile nimmt `toolchain/libogc2-fast` automatisch, sobald es da ist, und
sagt beim Bauen, welche Bibliothek es verwendet. Ohne sie baut alles
unverändert — der Quelltext von CubeFetch ändert sich für die schnelle Fassung
mit keiner Zeile. Nur die Vorgabe für die Zahl der Verbindungen richtet sich
danach: das Makefile setzt `CF_BIG_WINDOW=1`, und dann sind es 6 statt 8, weil
sechs große Fenster schon über der Adaptergrenze liegen und jede weitere
Verbindung nur den 8-KB-Puffer des ENC28J60 belastet. Welche Fassung in einem
DOL steckt, steht auf dem Diagnosebildschirm.

Gebaut wird dabei nicht `make cube`, sondern nur `libogc.a` und `libbba.a`
(über `LIBRARIES` auf der Kommandozeile). `make cube` würde auch `libasnd` und
`libaesnd` bauen, und deren MP3-Player braucht `mad.h` aus dem portlib
`ppc-libmad` — das hat kaum jemand installiert, und gebraucht wird es hier
nicht. `libfat.a` kommt samt Kopfdateien aus dem signaturgeprüften Paket; es
hängt nicht am lwIP.

Gegengeprüft wird das Ergebnis im Maschinencode, dieselbe Stelle wie vorher:

```
cmplwi  r9,35040        ; tcp_recved, vorher 2920
```

Und zwar im fertigen `cubefetch.elf`, nicht nur in der Bibliothek — sonst wäre
nicht bewiesen, dass auch dagegen gelinkt wurde.

### Zwei Fehler im ENC28J60-Treiber

Mit dem großen Fenster kamen 358 kB/s an statt der gerechneten 1 MB/s — also
45 kB/s je Verbindung, wo 168 möglich wären. Am Hoster lag es nicht, derselbe
Download läuft am PC mit 15 MB/s. In `lwip/netif/enc28j60if.c` standen zwei
Dinge, die mit dem kleinen Fenster nie aufgefallen sind:

**Der Unterbrechungsbehandler holte genau ein Paket ab.** Keine Schleife über
`EPKTCNT`, obwohl der Chip mehrere Rahmen gepuffert haben kann. Mit einem
Fenster von zwei Rahmen kam ohnehin nie mehr als eines nach; mit 24 Rahmen
Fenster bleibt der Rest im Chip liegen, bis die nächste Unterbrechung kommt.

**Der Empfangsspeicher des Chips war auf 4 KB eingestellt**, die andere Hälfte
der 8 KB auf Senden. 4 KB sind knapp drei Rahmen. Gesendet wird aber höchstens
ein Rahmen auf einmal (`MAMXFL` steht auf 2048), also sind 2 KB für Senden
reichlich und der Empfang bekommt 6 KB.

Was nicht in den Chip passt, ist verloren; TCP schickt es nach, und der
Durchsatz bleibt bei dem Bruchteil stehen, bei dem sich Verlust und
Nachschicken die Waage halten. Genau das waren die 358 kB/s.

Die Rechnung dahinter: bei 10 MBit braucht ein Rahmen 1,2 ms auf der Leitung,
der Treiber muss also 833 Rahmen je Sekunde schaffen. Ein Rahmen über EXI bei
16 MHz sind 1460 × 8 / 16 MHz = 0,73 ms reine Buszeit plus etwa ein Dutzend
Registerzugriffe. Das reicht, aber ohne viel Luft — und 6 KB Puffer sind nur
5 ms Leitungszeit. Deshalb ist die Abholschleife nötig: sie muss den Rückstand
aus einer verzögerten Unterbrechung in einem Durchgang aufholen können.

Höher als 16 MHz geht EXI für diesen Chip nicht: der ENC28J60 ist nach
Datenblatt bis 20 MHz spezifiziert, die nächste EXI-Stufe wäre 32 MHz.

Gebracht hat der Patch wenig — 380 kB/s statt 358. Die beiden Fehler waren echt,
aber sie waren nicht die Grenze.

## Gemessen: wo die 606 kB/s herkommen

Statt weiter zu vermuten, misst der Netztest (Diagnose, Taste X) ohne jedes
Schreiben auf die Karte, der Reihe nach mit mehr Verbindungen:

| Verbindungen | Rate | je Verbindung | gegen linear |
|---|---:|---:|---:|
| 1 | 163 kB/s | 163 | — |
| 2 | 283 kB/s | 142 | 87 % |
| 4 | 465 kB/s | 116 | 71 % |
| 6 | 537 kB/s | 90 | 55 % |
| 8 | 606 kB/s | 76 | 46 % |

Zwei Dinge stehen damit fest.

**Das große Fenster wirkt genau wie gerechnet.** Eine Verbindung bringt 163 kB/s,
vorhergesagt waren 35040 / 0,209 s = 168. Die Rechnung über das
Bandbreiten-Verzögerungs-Produkt war richtig, und mit dem Paket-lwIP wären es
14 kB/s.

**Darüber liegt eine gemeinsame Grenze bei etwa 600–700 kB/s.** Die Reihe flacht
ab, je mehr Verbindungen dazukommen — das ist die Form, die ein geteilter
Engpass macht, nicht die eines Fensterproblems. 606 kB/s sind 415 Rahmen je
Sekunde, also 2,4 ms je Rahmen, davon 0,73 ms Nutzlast über EXI.

### Der Bus wurde zu 83 % für Verwaltung benutzt

Nachgezählt, was der Treiber pro empfangenen Rahmen über EXI schickt — jeder
Punkt ist ein eigener Vorgang mit `EXI_Select` und `EXI_Deselect` drumherum:

| Zugriff | Anzahl | |
|---|---:|---|
| `RBM` Statuskopf (6 Bytes) | 1 | |
| `RBM` Nutzlast | 1 | **Daten** |
| `ERDPT` schreiben | 2 | zwei 8-Bit-Register |
| `ERXRDPT` schreiben | 2 | gibt Platz im Chip frei |
| `ECON2.PKTDEC` | 1 | |
| `EPKTCNT` lesen | 1 | |
| Bankumschaltung | 4 | zweimal hin und zurück |
| **zusammen** | **12** | davon 2 mit Daten |

Der Übeltäter ist `EPKTCNT`: es liegt in Bank 1, `ERDPT` und `ERXRDPT` in Bank 0.
Jeder Rahmen schaltete also die Bank hin und wieder zurück — fünf Zugriffe, nur
um zu erfahren, wie viele Rahmen noch warten. Der Chip hatte das aber schon zu
Beginn der Unterbrechung gesagt.

Jetzt wird `EPKTCNT` **einmal je Unterbrechung** gelesen und dient als Zähler für
die Abholschleife. Die Schleife bleibt damit vollständig in Bank 0:
**7 Zugriffe je Rahmen statt 12.** Nebenbei ist das auch korrekter, denn laut
Errata des ENC28J60 ist `PKTIF` unzuverlässig und `EPKTCNT` die empfohlene
Quelle.

`ERXRDPT` bleibt bewusst bei jedem Rahmen stehen. Es ist die Schranke, hinter die
der Chip nicht schreibt; einmal je Schub zu aktualisieren würde bedeuten, dass
der Empfangsspeicher während des Abholens voll bleibt — bei 1,2 ms Rahmenabstand
wären das genau die Verluste, die vorher das Problem waren.

### Gebracht hat es nichts — und das ist die Antwort

Gemessen nach der Umstellung: 174 / 298 / 432 / 495 / 611 kB/s. Vorher waren es
163 / 283 / 465 / 537 / 606. Innerhalb der Streuung also unverändert. **Die Zahl
der Buszugriffe war nicht die Grenze**, damit ist auch die Annahme von 167 µs je
Zugriff widerlegt.

Was die Reihe aber verrät, wenn man sie durchrechnet:

| Verbindungen | Rate | je Verbindung |
|---|---:|---:|
| 1 | 174 kB/s | 174 |
| 8 | 611 kB/s | 76 |

Eine Verbindung trifft die Vorhersage aus dem Fenster (168 kB/s) genau. Mit
jeder weiteren fällt der Anteil — und das passiert, wenn Pakete verloren gehen
und TCP sein Fenster zurücknimmt. Dazu die Zeitrechnung:

```
611 kB/s = 429 Rahmen/s  ->  2,33 ms je Rahmen im Treiber
10 MBit                  ->  1,23 ms je Rahmen auf der Leitung
1,23 / 2,33              =  53 %
gemessene Leitungsauslastung: 53 %
```

Der Treiber braucht also **doppelt so lange pro Rahmen, wie die Leitung
braucht, um den nächsten zu liefern**. Der Empfangsspeicher des Chips (6 KB,
vier Rahmen) überbrückt das für ein paar Millisekunden, dann läuft er über. Das
System pendelt sich genau dort ein, wo Verlust und Nachschicken sich die Waage
halten — und das sind die 53 %.

Von den 2,33 ms sind 0,73 ms die Nutzlast über EXI. Die Zugriffszahl ist
ausgeschlossen (sieben statt zwölf änderte nichts), echtes DMA ist bestätigt
(`EXI_DmaEx` schickt Kopf und Rest einzeln, den 32-Byte-ausgerichteten Rumpf per
DMA). Wo die restlichen **1,6 ms je Rahmen** bleiben, lässt sich durch Lesen
nicht mehr klären. Dafür bräuchte es eine Zeitmessung im Treiber selbst — einen
Zähler über Rahmen und Verweildauer in `ExiHandler`, ausgelesen über einen
Zugang in der Bibliothek.

### Was es nicht ist: Halbduplex

Naheliegende Vermutung, weil 606 kB/s genau 51 % der Nutzlastgrenze von 10 MBit
sind und der Treiber den Chip halbduplex betreibt (`MACON3` ohne `FULDPX`,
`MABBIPG` 0x12, `PHCON1` unberührt). Bei Halbduplex kollidiert jede gesendete
Bestätigung mit dem eingehenden Strom.

Die Zahl ist aber Zufall. Nachgesehen in `tcp_recved`: lwIP schickt eine
Bestätigung erst, wenn `TCP_WND/2` Fenster frei geworden ist — und das ist mit
dem großen Fenster mitgewachsen. Im fertigen Binärcode steht `cmplwi r4,17519`,
also **eine Bestätigung je 17,5 KB oder zwölf Rahmen** statt je Rahmen. So wenig
Gegenverkehr kann keine 50 % kosten.

Vollduplex wäre auch riskant: der ENC28J60 kennt keine Aushandlung, ein Switch
stellt bei 10BASE-T ohne Aushandlung auf Halbduplex, und eine erzwungene
Vollduplex-Seite ergibt genau den Duplex-Fehlstand, der alles schlimmer macht.
Dafür gibt es keinen Grund, solange die Zahlen woanders hinzeigen.

## Karte und Netz gleichzeitig

Der echte Download lief mit **380 kB/s**, der Netztest mit **606**. Die
Differenz von 37 % ist das Schreiben auf die Karte — und zwar nicht, weil die
Karte zu langsam wäre (sie schafft 1,2 MB/s), sondern weil abwechselnd statt
gleichzeitig gearbeitet wurde:

```
380 kB/s geschrieben bei 1,2 MB/s Kartentempo = 31 % der Zeit im Schreiben
69 % von 606 kB/s = 417 kB/s    gemessen: 380
```

In diesen 31 % holt niemand Daten von den Sockeln ab. Darum füllt die
Hauptschleife jetzt nur noch Puffer und hängt sie in eine Warteschlange;
**geschrieben wird in einem eigenen Thread.** Die Karte läuft damit parallel zum
Netz, und die Grenze ist das langsamere der beiden statt der Summe:

```
min(Netz 606 kB/s, Karte 1220 kB/s) = 606 kB/s   -> 39 statt 63 Minuten
```

## Blöcke statt Bereiche: die Vorbelegung fällt weg

Solange gleichzeitig an verschiedene Stellen der Datei geschrieben wird, muss
sie vorher in voller Größe angelegt werden — der letzte Bereich schreibt ab dem
ersten Augenblick weit hinten, und libfat füllt beim Verlängern alles davor mit
Nullen. Das waren gemessene **20 Minuten je Image**, und nach dem Sprung auf
600 kB/s war das ein Drittel der Gesamtzeit.

Daran ist auch nicht zu drehen: jedes Byte wird einmal mit Nullen überschrieben,
wenn das Dateiende darüber hinwegläuft, bevor die echten Daten da sind. Ob acht
große Bereiche oder tausend kleine, ob gestreift oder gemischt — die Summe ist
immer einmal die ganze Datei extra. **Der einzige Ausweg ist, streng vorwärts zu
schreiben.**

Also:

- Die Datei wird in **Blöcke von 1 MB** geteilt. Jede Verbindung holt einen
  Block mit einer eigenen `Range`-Anfrage.
- Ein fertiger Block wartet im Speicher, bis **er** an der Reihe ist. Der
  Schreib-Thread nimmt immer nur den Block mit der Nummer `next_write` und
  hängt ihn an die Datei an — kein `fseeko`, kein Sprung, kein Loch.
- Plätze gibt es `Verbindungen + 1`, jeder mit 1 MB Puffer. Sind alle voll und
  der nächste fehlt noch, wartet das Laden. Das ist die richtige Stelle zu
  warten: ohne den nächsten Block kann die Datei nicht wachsen.
- Die Datei ist damit **zu jedem Zeitpunkt ein gültiger Anfang** der fertigen
  Datei. Das macht den Seitenwagen wieder einfach: ein Zähler und eine
  Prüfsumme (Format 3). `cf_crc32_combine` braucht es dafür nicht mehr.
- Fortgesetzt wird auf der letzten Blockgrenze. Ein angefangener Block wird neu
  geladen; die `.part` wird dafür auf den Stand **verkürzt**, und Verkürzen
  kostet nichts — es gibt nur Cluster frei. Genau das Gegenteil des
  Verlängerns, das die Vorbelegung so teuer gemacht hat.

Der Preis ist ein Verbindungsaufbau je Block statt einer langen Verbindung je
Bereich: rund 0,4 Sekunden auf etwa zehn Sekunden Übertragung, also wenige
Prozent. Mit `Connection: keep-alive` ließe sich auch das noch halbieren — das
ist der nächste offensichtliche Schritt, aber erst nach der Messung.

Gegen Fragmentierung hilft das Vorwärtsschreiben übrigens von selbst: libfat
vergibt fortlaufende Cluster, und niemand sonst schreibt gleichzeitig auf die
Karte.

### Was der Schreib-Thread dabei beachten muss

- Er allein verändert `next_write`, `written` und `crc`. Die Hauptschleife liest
  sie nur unter dem Mutex, und den Stand für den Seitenwagen erst nach
  `writer_drain`.
- `fflush` und `fwrite` auf derselben `FILE*` aus zwei Threads wäre genau die
  Art Fehler, die sich erst nach Stunden als falsche Prüfsumme zeigt. Vor jedem
  Schreiben des Seitenwagens läuft die Warteschlange darum leer.
- Beim Aufräumen wird **erst der Thread beendet, dann werden die Puffer
  freigegeben** — er liest aus ihnen, solange er läuft. Im Fehlerpfad gibt es
  kein vorheriges `writer_drain`, dort war die falsche Reihenfolge ein
  Zugriff auf freigegebenen Speicher.
- Hat die Hauptschleife keine offene Verbindung mehr, muss sie unterscheiden:
  hat der Schreiber Arbeit, wartet sie auf ihn (er weckt nach jedem Block, und
  der Mutex liegt über Prüfung und Warten, es geht also kein Signal verloren);
  hat er keine, ist gerade kein Verbindungsaufbau gelungen, und dann hilft nur
  kurz schlafen und oben neu versuchen. Warten würde dort nie enden.

Das prägt das Design mehr als alles andere:

- **Resume ist Pflicht**, nicht Komfort. Der Zustand der Warteschlange liegt auf
  der Karte und übersteht Abstürze und Ausschalten.
- **Streng vorwärts schreiben**, nie vorbelegen (siehe oben). Hält die Datei
  zusammenhängend und erspart 1,36 GB Schreibzyklen pro Spiel.
- **Nur die Blöcke im RAM**, nicht die Datei: 24 MB Hauptspeicher, Images sind
  1,4 GB. Bei sechs Verbindungen sind das sieben Puffer à 1 MB; reicht der
  Speicher nicht, arbeitet die Engine mit weniger Plätzen weiter. stdio puffert
  nicht mit (`setvbuf` auf `_IONBF`), das wäre nur eine zweite Kopie.
- Die Prüfsumme läuft **während** des Schreibens mit, nicht in einem zweiten
  Durchgang. Ein zweiter Lesedurchgang über 1,4 GB kostet sonst unnötig Zeit.

## Speicher

`libfat` über die EXI-Treiber von libogc2. FAT32 und FAT16. Ein GC-Image ist
1,36 GB und bleibt damit unter der 4-GB-Grenze von FAT32.

exFAT kann libfat nicht. Für Phase 5 vorgemerkt — im DolphinDepot-Register gibt
es mit `gcmm-exfat` einen Beleg, dass es machbar ist.

Geräte werden beim Start abgetastet und mit ihrem freien Platz angezeigt. Der
Zielordner ist pro Gerät frei wählbar und wird gespeichert.

## Module

Der Kern ist bewusst frei von libogc, damit er sich auf dem PC mit einem
normalen Compiler bauen und testen lässt. Nur die Plattformschicht braucht die
GameCube-Bibliotheken.

```
core/        portables C99, ohne libogc, am PC testbar
  sources      Quellen-Definition lesen und schreiben
  autoidx      HTML-Verzeichnislisting -> Einträge
  manifest     eigenes JSON-Format -> Einträge
  catalog      Einträge halten, suchen, sortieren, filtern
  fuzzy        Titel normalisieren, Token-Set-Ähnlichkeit
  gametdb      ID -> Titel, ID -> Bildadresse samt Regionskette
  queue        Warteschlange, Zustandsübergänge, Persistenz
  i18n         Sprachdateien laden, Schlüssel auflösen

platform/    libogc2
  net          Netzwerk hochfahren, DHCP, Verbindungstest
  dns          eigener DNS-Client, libogc2 bringt keinen mit
  http         HTTP/1.1: Range, Redirects, Keep-Alive, Timeouts
  exi          Steckplätze abtasten, Kennungen deuten
  storage      libfat einhängen, Geräte erkennen, Platz prüfen
  download     abbruchsicher schreiben, CRC32 mitlaufen lassen
  ui           Konsolenoberfläche, Bildschirmtastatur
```

Dazu `gametdb` und `fuzzy` im Kern sowie `gfx` (GX, FreeType) und `meta`
(GameTDB auf der Konsole: Datenbank holen, Cover dekodieren) in der
Plattformschicht.

Die Trennung kostet etwas Disziplin bei den Schnittstellen, zahlt sich aber
sofort aus: Parser und Suchlogik sind die fehleranfälligsten Teile und lassen
sich so am PC gegen echte Daten prüfen, statt bei jedem Versuch ein DOL zu
bauen und in Dolphin zu starten.

## Oberfläche

Geplant ist GX im 2D-Modus mit eigenem Sprite- und Textrenderer. FreeType, weil
Umlaute und Akzente in Spieletiteln und in der deutschen Übersetzung gebraucht
werden — die Konsolenschrift von libogc reicht dafür nicht.

Bis dahin läuft alles über die Konsolenschrift, mit zwei Eigenheiten, die
dokumentiert sein wollen, weil sie beide als Bildfehler auf der Konsole
aufgefallen sind:

- Die Menüs bauen sich in ihrer Tastenschleife jedes Bild neu auf, beginnend mit
  einem Löschen des Fensters. Direkt im angezeigten Bildpuffer fällt dieses
  Löschen mitten in den Bildaufbau und das Bild flackert. Die Konsole zeichnet
  darum in einen Schattenpuffer, der in `cf_ui_present` in den Bildpuffer kopiert
  wird, und nur dann, wenn seit dem letzten Mal etwas gezeichnet wurde.
- Die letzte Spalte des Fensters bleibt frei. Wird das letzte Zeichen der letzten
  Zeile beschrieben, läuft der Schreibzeiger aus dem Fenster und die Konsole
  schiebt den ganzen Inhalt eine Zeile hoch — bei einer Fußzeile über die volle
  Breite also jedes Bild.

Bildschirmtastatur über den Controller: Steuerkreuz bewegt, A übernimmt, B
löscht, Z wechselt die Belegung, Start bestätigt. Für Adressen gibt es eine
eigene Belegung mit `. / : -` auf der ersten Ebene.

## Testen am PC

Dolphin bringt eine Broadband-Adapter-Emulation mit, die echten Netzzugang
gibt, und kann ein SD-Karten-Image einhängen. Damit ist der größte Teil der
Funktion ohne Hardware prüfbar. Auf echter Hardware müssen danach vor allem die
Dinge gegengeprüft werden, die Dolphin zu gut wegabstrahiert: EXI-Konflikte,
echte Durchsatzraten, Fragmentierung und das Verhalten bei abbrechender
Verbindung.

## Phasenplan

**P0 — Grundlage.** devkitPPC und libogc2 einrichten, Makefile, ein DOL das
bootet und in Dolphin wie auf Hardware startet.

**P1 — Netz und Speicher.** Netzwerkadapter hochfahren, DHCP, Geräte erkennen. Eine Datei
per HTTP mit `Range` und Resume auf die Karte schreiben, mit
mitlaufender Prüfsumme. Das ist der Kern; wenn das trägt, trägt der Rest.

**P2 — Katalog.** Quellen lesen, Listen parsen, Liste mit Suche,
Sortierung und Filtern. Warteschlange mit Persistenz.

**P3 — Oberfläche.** GX-Oberfläche, Bildschirmtastatur, Deutsch und Englisch.
*Gebaut:* `cf_gfx` (GX in 2D, zwei Bildpuffer, Texturen im Kachelformat,
FreeType-Atlas aus eingebetteter DejaVu Sans), `cf_ui` darauf.

**P4 — Metadaten.** Header-Probe, GameTDB-Bilder und -Infos, Cache auf der
Karte. Details in [METADATA.md](METADATA.md). *Gebaut:* `cf_gametdb` und
`cf_fuzzy` im Kern (am PC getestet), `cf_meta` auf der Konsole. Dazu Sortierung
nach Jahr und Filter nach Genre im Katalogkern.

**P5 — Erweiterungen.** exFAT (libogc2 bringt es über libdvm mit), TLS, Einreichung
im DolphinDepot-Register.

## Verhältnis zu bestehender Software

**DolphinDepot** ist ein Homebrew-App-Store mit einem Register aus
`manifest.json`-Dateien pro Anwendung. Inhaltlich eine andere Sache — dort
liegen Homebrew-Programme, nicht Spiele-Images. Übernommen wird davon das
Muster: schlanke JSON-Manifeste als Quellenbeschreibung. Und CubeFetch sollte
selbst dort eingereicht werden, inklusive der Assets, die das Register erwartet
(`banner.png` 96×32, `screenshot0-2.png` 640×480, `hero.png` 640×160,
`opening.bnr`).

**CubeBrowser** im selben Register ist ein echter Webbrowser für den GameCube,
der HTTPS spricht und abbruchsichere Downloads auf die SD-Karte beherrscht. Das
ist der Beweis, dass BBA, HTTP, Resume und SD-Schreiben zusammen auf der
Hardware funktionieren — und dass TLS auf 486 MHz machbar ist. CubeFetch ist
kein Browser, sondern eine gezielte Katalog- und Warteschlangen-Oberfläche;
beim Thema TLS in Phase 5 lohnt aber ein Blick darauf, wie es dort gelöst ist.

**Swiss** ist das Ziel der Kette: es startet am Ende die heruntergeladenen
Images. Dessen Erwartungen an Dateinamen, Ordner und Fragmentierung geben die
Vorgaben für das Schreiben vor.
