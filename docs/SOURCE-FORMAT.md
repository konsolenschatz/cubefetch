# Quellen-Definition

Eine Quelle ist eine Adresse, hinter der CubeFetch eine Liste herunterladbarer
Dateien ermitteln kann. Quellen werden **nicht** mitgeliefert.

## Eingabewege

1. **`sources.txt`** im Ordner `/cubefetch/` auf einem erkannten Speichergerät.
2. **Bildschirmtastatur** in der App. Neu eingetragene Quellen werden in dieselbe
   `sources.txt` zurückgeschrieben.

## Dateiformat

Eine Zeile pro Eintrag, UTF-8. `#` am Zeilenanfang ist ein Kommentar, Leerzeilen
werden übersprungen.

```
# Format:  <typ> | <anzeigename> | <url>
autoidx | Eigener Server           | http://192.168.1.50/gc/
manifest| Kuratierte Liste         | http://192.168.1.50/gc/index.json
```

Wird der Typ weggelassen, versucht CubeFetch ihn selbst zu erkennen: eine
Adresse auf `.json` wird zu `manifest`, alles andere zu `autoidx`.

## Typ `autoidx` — Verzeichnislisting

Für die automatisch erzeugten Index-Seiten von Apache, nginx, lighttpd und
Caddy. CubeFetch liest nur `href`-Attribute aus `<a>`-Elementen und verwirft
Einträge, die auf `/` enden (Unterordner) oder mit `?` beginnen
(Sortier-Links). Größen werden, wenn vorhanden, aus dem Text hinter dem Link
gelesen, sonst per `HEAD`-Request nachgeholt.

Das ist bewusst eine Heuristik. Wer verlässliche Ergebnisse will, nutzt
`manifest`.

## Typ `manifest` — eigenes Format

Schlank gehalten, damit es auf dem GameCube ohne großen Parser auskommt und von
Hand oder per Skript erzeugt werden kann.

```json
{
  "cubefetch": 1,
  "name": "Kuratierte Liste",
  "base": "http://192.168.1.50/gc/",
  "entries": [
    {
      "file": "Chibi-Robo.iso",
      "title": "Chibi-Robo!",
      "size": 1459978240,
      "id": "GCBP01",
      "crc32": "a1b2c3d4"
    }
  ]
}
```

Nur `file` ist verpflichtend. `base` wird jedem relativen `file` vorangestellt.
Ist `id` angegeben, entfällt die Header-Probe und die Metadaten werden direkt
zugeordnet. Alle weiteren Felder sind optional und überschreiben, was sonst
ermittelt würde.

## Anforderungen an den Server

- **HTTP ohne TLS.** Der GameCube bringt keine TLS-Implementierung mit. Ein einfacher
  Webserver im Heimnetz (Apache, nginx, lighttpd, Caddy) reicht.
- **`Range`-Unterstützung** für Resume. Erwartet wird
  `206 Partial Content` mit korrektem `Content-Range`; die genannten Webserver
  können das von Haus aus.
- **`Content-Length`** in der Antwort, damit die Zieldatei vorab in voller Größe
  angelegt werden kann (siehe Fragmentierung in ARCHITECTURE.md).

Fehlt `Range`, lädt CubeFetch trotzdem — aber ein Abbruch bedeutet, von vorn zu
beginnen. Das wird vor dem Start angezeigt.
