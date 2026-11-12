#include "platform/cf_net.h"
#include "platform/cf_dns.h"
#include "platform/cf_exi.h"

#include <stdio.h>
#include <string.h>

#include <gccore.h>
#include <network.h>

static int g_up;

int cf_net_up(cf_net_info *out)
{
	cf_exi_slot slots[CF_EXI_SLOTS];
	size_t n, i, found = (size_t)-1;
	s32 r;

	memset(out, 0, sizeof(*out));

	if (g_up) {
		out->up = 1;
		return 1;
	}

	/* if_config macht bei use_dhcp=true die komplette Einrichtung und
	 * waehlt dabei auch den Adapter aus: libogc2 probiert in
	 * if_configex Broadband Adapter, W6x00, W5500 und ENC28J60 durch
	 * und tastet je Chip Serial Port 1, Serial Port 2 sowie Memory
	 * Card Slot A und B ab.
	 *
	 * Den max_retries-Parameter von devkitPros libogc gibt es hier
	 * nicht mehr, die Wiederholungen macht if_config selbst. */
	r = if_config(out->ip, out->mask, out->gw, true);
	if (r < 0) {
		/* Die nackte Zahl hilft niemandem. Entscheidend ist, ob
		 * ueberhaupt etwas steckt, das ein Adapter sein kann. */
		n = cf_exi_scan(slots, CF_EXI_SLOTS);
		for (i = 0; i < n; i++) {
			if (cf_exi_could_be_nic(&slots[i])) {
				found = i;
				break;
			}
		}

		if (found == (size_t)-1)
			snprintf(out->err, sizeof(out->err),
			         "kein Adapter gefunden (if_config %ld)",
			         (long)r);
		else
			snprintf(out->err, sizeof(out->err),
			         "%s belegt, Netzwerk kam nicht hoch "
			         "(if_config %ld)",
			         slots[found].name, (long)r);
		return 0;
	}

	g_up = 1;
	out->up = 1;

	/* Der DHCP-Lauf von if_config gibt uns den DNS-Server nicht heraus.
	 * Das Gateway ist aber in Heimnetzen praktisch immer auch ein
	 * DNS-Forwarder, also dient es als Standard. Ueberschreibbar in der
	 * Konfiguration. */
	if (!cf_dns_server()[0] && out->gw[0])
		cf_dns_set_server(out->gw);

	return 1;
}

void cf_net_down(void)
{
	if (!g_up)
		return;
	/* net_deinit gibt es in libogc2 nur fuer die Wii; auf dem Cube
	 * bleibt die Verbindung bis zum Neustart offen. */
	g_up = 0;
}
