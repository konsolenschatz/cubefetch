/* Was steckt in welchem EXI-Steckplatz.
 *
 * Gebraucht wird das zweimal: in der Diagnose, und wenn das Netzwerk
 * nicht hochkommt - dann ist die Steckplatzliste die eigentliche
 * Fehlermeldung.
 *
 * Die Kennungen sind nicht eindeutig. Ein SD-Adapter und ein
 * Netzwerkadapter mit ENC28J60 oder W5500 melden beide 0xFFFFFFFF, also
 * "keine Kennung": nachgeprueft an sdgecko_initIO und ENC28J60_Init,
 * die genau diesen Wert verlangen. Darum wird die rohe Kennung
 * angezeigt und nur so weit gedeutet, wie es belegt ist.
 */
#ifndef CF_EXI_H
#define CF_EXI_H

#include <stddef.h>
#include <stdint.h>

#define CF_EXI_SLOTS 5

typedef struct {
	const char *name;
	int         ch;
	int         dev;
	int         probed;    /* Steckplatz liess sich belegen und abfragen */
	uint32_t    id;        /* rohe EXI-Kennung */
} cf_exi_slot;

/* Fuellt bis zu max Eintraege und gibt deren Zahl zurueck. */
size_t cf_exi_scan(cf_exi_slot *out, size_t max);

/* Kurze Deutung der Kennung, immer ein gueltiger Zeiger. */
const char *cf_exi_meaning(const cf_exi_slot *s);

/* 1 wenn an diesem Steckplatz ein echter Broadband Adapter sitzt. */
int cf_exi_is_bba(const cf_exi_slot *s);

/* 1 wenn hier ueberhaupt ein Netzwerkadapter sitzen koennte, also die
 * Kennung zu einem der von libogc2 unterstuetzten Chips passt. */
int cf_exi_could_be_nic(const cf_exi_slot *s);

#endif
