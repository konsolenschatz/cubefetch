# Kern-Tests auf dem Rechner, ohne devkitPPC.
#
# Der Kern unter source/core ist bewusst frei von libogc, damit genau das
# hier moeglich ist: Parser und Suchlogik gegen echte Antworten pruefen,
# statt bei jedem Versuch ein DOL zu bauen und in Dolphin zu starten.

CC      ?= gcc
CFLAGS  := -O1 -g -Wall -Wextra -std=gnu99 -I../include
BIN     := run_tests

CORE := \
	../source/core/cf_crc32.c \
	../source/core/cf_fuzzy.c \
	../source/core/cf_gametdb.c \
	../source/core/cf_str.c \
	../source/core/cf_json.c \
	../source/core/cf_entry.c \
	../source/core/cf_catalog.c \
	../source/core/cf_sources.c \
	../source/core/cf_srcparse.c \
	../source/core/cf_queue.c \
	../source/core/cf_qr.c

.PHONY: run clean qrbmp

qrbmp: qrbmp.c ../source/core/cf_qr.c
	$(CC) $(CFLAGS) -o qrbmp $^

$(BIN): test_core.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

run: $(BIN)
	./$(BIN)

clean:
	@rm -f $(BIN) $(BIN).exe
