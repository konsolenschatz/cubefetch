# CubeFetch

*[Deutsche Version](README.de.md)*

A network downloader for the Nintendo GameCube. It downloads files from sources
you define yourself straight over an Ethernet cable onto the storage attached
to the console — no PC in between. Meant for your own backups and homebrew.

Languages: English, Deutsch, Français, Español, Italiano, Русский, 日本語.
Fully controlled with the GameCube controller, text input via an on-screen
keyboard. Eight themes; the default is the Konsolenschatz theme.

## Features

- **User-defined sources** — as `sources.txt` on the card or typed in on the
  on-screen keyboard. Two types: HTML
  directory listings and a custom JSON manifest.
- **Catalog** with search, sorting by name, size and year (ascending and
  descending) and a genre filter built from the genres actually present.
  Long titles scroll as marquee text; the detail panel can be hidden.
- **Queue** of up to 32 titles, saved on the card and resumable across
  restarts. The download screen shows what comes next, and ↑↓ + A picks the
  next title.
- **Crash-safe downloads** with HTTP range requests, several blocks in
  parallel over keep-alive connections, and a running CRC32 check. Writing is
  strictly sequential in its own thread: no preallocation, and the file is
  always a valid prefix of the finished one. Progress lives next to the
  `.part` file and survives power loss.
- **Disc ID before downloading** — a 1 KB range request reads the real game
  ID and region from the image header without fetching 1.4 GB.
- **Own DNS client**, because libogc ships no name resolution for the
  GameCube.
- **EXI conflict warning** when the network adapter and the storage device
  share a channel.
- **GX + FreeType UI**: proportional font, double-buffered, eight themes
  (Konsolenschatz, Classic, Black, Light, Purple, Orange, Pink, Green).
- **Covers and game info from GameTDB** — box art, year, publisher,
  developer, genre, players. Matched exactly by disc ID, otherwise by name
  similarity (marked as such). The console fetches the database itself
  (2.3 MB, inflated while streaming); see [docs/METADATA.md](docs/METADATA.md).
- **Community page** with QR codes for the Konsolenschatz Discord, GitHub and
  YouTube.

Not yet supported: exFAT.

## Legal notice

CubeFetch is a generic HTTP downloader, made for **homebrew** and for
**backups of your own games served from your own server** (for example a NAS
or PC on your home network). It ships **no sources, links or game data**, and
it contains no Nintendo code or keys. **The project does not provide, endorse
or support piracy** — please don't ask for sources, and don't share them in
issues or on the community channels. Which address is entered and
whether the content found there may be used is decided by, and the sole
responsibility of, the person operating the device. Only download backups of
games you own, or content you have the rights to.

CubeFetch is not affiliated with Nintendo. "Nintendo" and "GameCube" are
trademarks of Nintendo.

## Speed

What matters is the **distance to the source**, not your line. lwIP in libogc2
uses a 2920-byte receive window without window scaling, so one connection is
capped at `2920 bytes / round-trip time` — about 14 kB/s from a US west coast
server.

CubeFetch therefore downloads **several blocks at once**, each on its own
connection with its own window (up to twelve, `conns=` in `config.txt`).

With the enlarged window (`tools/build-libogc2.sh`, see [Building](#building))
you reach the real limit of the hardware: **about 1 MB/s**, set by the
10 Mbit adapter — roughly 25 minutes per image. Details in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (German).

## Hardware

Network and storage share the same EXI bus on the GameCube; not every
combination works at the same time. See the compatibility matrix in
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

Supported network adapters: the original Broadband Adapter, ETH2SP1 (W5500)
and ENC28J60 adapters for Serial Port 2 or a memory card slot. All four slots
are probed: SP1, SP2, slot A, slot B.

Recommended: **network adapter in Serial Port 1** plus **SD2SP2 in Serial
Port 2** — separate EXI channels, no conflicts. Tested on real hardware so far:
**ENC28J60 adapter in a memory card slot** plus SD2SP2.

## Building

Requires devkitPro with devkitPPC, the portlibs `ppc-freetype`, `ppc-libpng`
and `ppc-zlib` with their dependencies (bzip2, brotli) — and **libogc2**, not
devkitPro's libogc: only libogc2 has drivers for ENC28J60, W5500 and W6x00 and
probes the memory card slots.

```
tools/get-libogc2.sh     # fetches libogc2 (signature-checked) into toolchain/
make                     # builds cubefetch.dol
make dist                # creates the SD card folder layout
```

For fast downloads additionally:

```
tools/build-libogc2.sh   # libogc2 with a larger TCP window, into toolchain/
```

The Makefile picks that build up automatically and reports which one it uses.

## Testing

The core in `source/core` is free of libogc and builds with a normal compiler:

```
make -C tests run
```

Console test guide: [docs/TESTING.md](docs/TESTING.md) (German).

## Documentation (German)

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — design, hardware, roadmap
- [docs/SOURCE-FORMAT.md](docs/SOURCE-FORMAT.md) — source definition format
- [docs/METADATA.md](docs/METADATA.md) — cover and metadata matching
- [docs/TESTING.md](docs/TESTING.md) — testing on the console

## Community

- Discord: https://discord.gg/EqUpCbGM5p
- GitHub: https://github.com/konsolenschatz
- YouTube: https://www.youtube.com/@Konsolenschatz — a subscription is the
  best way to support the project.

## License

CubeFetch is open source under the [MIT License](LICENSE) — use, modify and
share it freely, as long as the copyright notice (Konsolenschatz) stays with
it. Fonts and libraries keep their own licenses, and the Konsolenschatz name
and coin are excluded; see [THIRD-PARTY.md](THIRD-PARTY.md).
