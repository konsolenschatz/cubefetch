#---------------------------------------------------------------------------------
# CubeFetch - GameCube network downloader
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "DEVKITPPC ist nicht gesetzt. Bitte devkitPro installieren und die Umgebung laden.")
endif

include $(DEVKITPPC)/gamecube_rules

#---------------------------------------------------------------------------------
# libogc2 statt devkitPros libogc
#
# devkitPros libogc kennt nur den echten Broadband Adapter und sucht ihn
# ausschliesslich an EXI0:2, also an Serial Port 1: BBA_Init ruft
# EXI_GetID(0, 2) und verlangt die Kennung 0x04020200. Ein Adapter im
# Memory-Card-Slot wird damit nie gefunden, egal welcher.
#
# libogc2 bringt eigene Treiber fuer ENC28J60, W5500 und W6x00 mit und
# tastet in if_configex vier Steckplaetze ab: EXI0:2 (SP1), EXI2:0 (SP2),
# EXI0:0 (Slot A) und EXI1:0 (Slot B). Das deckt die Adapter im
# Memory-Card-Slot ab. Naeheres in docs/ARCHITECTURE.md.
#
# Gesucht wird in dieser Reihenfolge:
#
#   1. toolchain/libogc2-fast  - selbst gebaut mit groesserem
#      TCP-Empfangsfenster (tools/build-libogc2.sh). Das fertige Paket
#      hat TCP_WND = 2920 Bytes und kein Window Scaling, womit eine
#      Verbindung nie mehr als 2920 Bytes je Umlaufzeit schafft. Das ist
#      bei entfernten Quellen der Engpass, nicht Adapter oder Karte.
#   2. $DEVKITPRO/libogc2       - systemweit installiertes Paket
#   3. toolchain/libogc2        - projektlokales Paket (get-libogc2.sh)
#
# Mit "make LIBOGC2=/pfad" laesst sich das uebergehen.
#
# CURDIR taugt hier nicht: dieses Makefile laeuft fuer den zweiten
# Durchgang mit -C build, und dann zeigt CURDIR dorthin.
CF_ROOT := $(abspath $(dir $(firstword $(MAKEFILE_LIST))))

ifeq ($(strip $(LIBOGC2)),)
ifneq ($(wildcard $(CF_ROOT)/toolchain/libogc2-fast/gamecube/lib/libbba.a),)
LIBOGC2 := $(CF_ROOT)/toolchain/libogc2-fast
else ifneq ($(wildcard $(DEVKITPRO)/libogc2/gamecube_rules),)
LIBOGC2 := $(DEVKITPRO)/libogc2
else
LIBOGC2 := $(CF_ROOT)/toolchain/libogc2
endif
endif

ifeq ($(wildcard $(LIBOGC2)/gamecube/lib/libbba.a),)
$(error libogc2 fehlt. tools/get-libogc2.sh holt es nach toolchain/, oder \
das Paket libogc2 installieren. Hintergrund in docs/ARCHITECTURE.md.)
endif

export LIBOGC2
export LIBOGC_INC := $(LIBOGC2)/gamecube/include
export LIBOGC_LIB := $(LIBOGC2)/gamecube/lib

# Mit dem grossen Empfangsfenster tragen schon sechs Verbindungen so
# viel, wie der Adapter hergibt; mehr brauchen nur mehr Puffer und
# belasten den 8-KB-Speicher des ENC28J60 unnoetig. Mit dem fertigen
# Paket sind dagegen acht besser als sechs. Die Vorgabe richtet sich
# darum danach, wogegen gebaut wird. Wer LIBOGC2 selbst setzt und eine
# gepatchte Fassung meint, gibt CF_BIG_WINDOW=1 mit.
ifeq ($(LIBOGC2),$(CF_ROOT)/toolchain/libogc2-fast)
CF_BIG_WINDOW ?= 1
endif
CF_BIG_WINDOW ?= 0
export CF_BIG_WINDOW

#---------------------------------------------------------------------------------
TARGET      := cubefetch
BUILD       := build
SOURCES     := source source/core source/platform
INCLUDES    := include
# Die Schrift wird ins DOL eingebettet (bin2o aus base_tools), damit die
# Oberflaeche nicht von einer Datei auf der Karte abhaengt.
DATA        := data/fonts data/lang

#---------------------------------------------------------------------------------
# Der Kern (source/core) ist portables C99 ohne libogc und wird von tests/Makefile
# mit einem normalen Host-Compiler gegen echte Testdaten gebaut.
#---------------------------------------------------------------------------------
CFLAGS   = -g -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu99 \
           $(MACHDEP) $(INCLUDE) -DCUBEFETCH_PLATFORM_OGC=1 \
           -DCF_BIG_WINDOW=$(CF_BIG_WINDOW)
CXXFLAGS = $(CFLAGS)
LDFLAGS  = -g $(MACHDEP) -Wl,-Map,$(notdir $@).map

# -lfat   FAT16/32 auf SD Gecko, SD2SP2 und Memory-Card-Adaptern
# -lbba   Netzwerk-Stack samt Treibern fuer BBA, ENC28J60, W5500, W6x00
# -logc   EXI, Video, PAD
#
# -lfreetype  Schriftatlas fuer die GX-Oberflaeche (Umlaute, Groessen)
# -lpng, -lz  Cover von GameTDB dekodieren; zlib auch fuer wiitdb.zip
# -lbz2, -lbrotli*  braucht libfreetype aus den portlibs beim Linken
#
# Diese portlibs stammen von devkitPro und sind gegen dessen libogc
# gebaut - das ist hier aber unerheblich: zlib, libpng, FreeType, bz2
# und brotli sind reine C-Bibliotheken ohne eine einzige Referenz auf
# libogc (nachgesehen mit nm: nur libc-Symbole wie malloc, setjmp, pow).
# Die Vorsicht von vorher, sie wegen ABI-Fragen draussen zu lassen, war
# zu viel des Guten. cf_crc32 bleibt trotzdem im Kern: es ist getestet
# und am PC baubar, zlib waere es nicht.
LIBS := -lfat -lbba -logc -lfreetype -lpng -lbz2 -lbrotlidec \
        -lbrotlicommon -lz -lm

LIBDIRS := $(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT  := $(CURDIR)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.ttf))) en.lang

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES_BIN     := $(addsuffix .o,$(BINFILES))
export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
                         $(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES_BIN     := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include/freetype2) \
                  -I$(CURDIR)/$(BUILD) \
                  -I$(LIBOGC_INC)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) -L$(LIBOGC_LIB)

.PHONY: $(BUILD) clean all run dist test

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@echo "libogc2: $(LIBOGC2)"
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(OUTPUT).map dist
	@[ -f tests/Makefile ] && $(MAKE) --no-print-directory -C tests clean || true

# Kern-Tests auf dem Host, ohne devkitPPC
test:
	@[ -f tests/Makefile ] && $(MAKE) --no-print-directory -C tests run || echo "noch keine Host-Tests vorhanden"

# Legt die fertige Ordnerstruktur fuer die Speicherkarte an.
dist: $(BUILD)
	@rm -rf dist
	@mkdir -p dist/apps/cubefetch dist/cubefetch/lang dist/games
	@cp $(OUTPUT).dol dist/apps/cubefetch/boot.dol
	@cp data/lang/*.lang dist/cubefetch/lang/
	@mkdir -p dist/cubefetch/fonts
	@cp data/extra-fonts/mplus1p.ttf data/extra-fonts/OFL-MPLUS.txt dist/cubefetch/fonts/
	@cp data/sources.example.txt dist/cubefetch/sources.txt
	@cp docs/TESTING.md dist/cubefetch/TESTING.txt
	@cp LICENSE dist/cubefetch/LICENSE.txt
	@cp THIRD-PARTY.md dist/cubefetch/THIRD-PARTY.txt
	@echo "dist/ ist fertig. Inhalt in die Wurzel der Karte kopieren:"
	@find dist -type f | sort | sed 's|^dist|  <Karte>|'

run: $(BUILD)
	@echo "Starte $(TARGET).dol in Dolphin (Broadband Adapter HLE aktivieren)"
	@dolphin-emu -e $(OUTPUT).dol

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).dol

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES): $(HFILES_BIN)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
# Schriftdateien aus data/fonts als Objekt: erzeugt <name>_ttf.h mit
# <name>_ttf und <name>_ttf_size. bin2o kommt aus base_tools.
#---------------------------------------------------------------------------------
%.ttf.o %_ttf.h : %.ttf
	@echo $(notdir $<)
	$(bin2o)

# Englische Texte als Ersatz fuer fehlende Schluessel
%.lang.o %_lang.h : %.lang
	@echo $(notdir $<)
	$(bin2o)

endif
