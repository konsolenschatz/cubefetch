/* Netzwerk hochfahren.
 *
 * Welcher Adapter benutzt wird, entscheidet libogc2: Broadband Adapter,
 * ENC28J60, W5500 oder W6x00, gesucht an Serial Port 1, Serial Port 2
 * und in beiden Memory-Card-Slots. Siehe docs/ARCHITECTURE.md. */
#ifndef CF_NET_H
#define CF_NET_H

typedef struct {
	int  up;
	char ip[16];
	char mask[16];
	char gw[16];
	char err[96];
} cf_net_info;

/* DHCP. Blockiert bis zu einigen Sekunden. 1 bei Erfolg. */
int  cf_net_up(cf_net_info *out);
void cf_net_down(void);

#endif
