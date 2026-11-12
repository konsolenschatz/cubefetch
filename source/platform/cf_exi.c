#include "platform/cf_exi.h"

#include <string.h>

#include <gccore.h>
#include <ogc/exi.h>

/* EXI0:1 ist fest verbaut (Uhr, SRAM, Font-ROM) und steht nur mit in der
 * Liste, damit beim Lesen klar ist, dass der Kanal nicht uebersehen
 * wurde. */
static const struct {
	const char *name;
	int ch, dev;
} g_slots[CF_EXI_SLOTS] = {
	{ "Memory Card Slot A", EXI_CHANNEL_0, EXI_DEVICE_0 },
	{ "Memory Card Slot B", EXI_CHANNEL_1, EXI_DEVICE_0 },
	{ "Serial Port 1",      EXI_CHANNEL_0, EXI_DEVICE_2 },
	{ "Serial Port 2",      EXI_CHANNEL_2, EXI_DEVICE_0 },
	{ "intern (Uhr/SRAM)",  EXI_CHANNEL_0, EXI_DEVICE_1 },
};

size_t cf_exi_scan(cf_exi_slot *out, size_t max)
{
	size_t i, n = 0;

	for (i = 0; i < CF_EXI_SLOTS && n < max; i++) {
		cf_exi_slot *s = &out[n++];

		memset(s, 0, sizeof(*s));
		s->name = g_slots[i].name;
		s->ch = g_slots[i].ch;
		s->dev = g_slots[i].dev;
		s->id = 0;

		/* Kein eigenes EXI_Lock und kein EXI_Attach drumherum:
		 * EXI_GetID macht beides selbst (es ruft intern EXI_LockEx,
		 * __exi_attach und EXI_Detach). Wer den Kanal vorher sperrt,
		 * laesst genau diese innere Sperre scheitern - dann meldet
		 * EXI_GetID einen Fehler und jeder Steckplatz sieht leer aus.
		 * Diesen Fehler hatte der erste Diagnosebildschirm. */
		if (EXI_GetID(s->ch, s->dev, &s->id) != 0)
			s->probed = 1;
	}

	return n;
}

int cf_exi_is_bba(const cf_exi_slot *s)
{
	/* 0x04020200 ist die Kennung des Nintendo Broadband Adapter.
	 * Nachgeprueft an BBA_Init, das genau auf diesen Wert vergleicht. */
	return s && s->probed && s->id == 0x04020200u;
}

int cf_exi_could_be_nic(const cf_exi_slot *s)
{
	if (!s || !s->probed)
		return 0;
	if (cf_exi_is_bba(s))
		return 1;
	/* ENC28J60, W5500 und W6x00 melden keine Kennung. Dasselbe tun
	 * SD-Adapter, darum nur "koennte". */
	return s->id == 0xFFFFFFFFu;
}

const char *cf_exi_meaning(const cf_exi_slot *s)
{
	if (!s || !s->probed)
		return "leer";
	if (s->id == 0x04020200u)
		return "Broadband Adapter";
	if (s->id == 0xFFFFFFFFu)
		return "ohne Kennung: SD- oder Netzwerkadapter";
	if (s->id == 0)
		return "keine Antwort";
	return "Memory Card oder anderes EXI-Geraet";
}
