/* Namensauflösung.
 *
 * libogc bringt fuer den GameCube keine mit. In den cube-Bibliotheken
 * existiert kein net_gethostbyname; das gethostbyname in libbba ist ein
 * Wrapper darauf und damit ebenfalls unaufloesbar. Es gibt auch kein
 * lwIP-DNS-Modul. Ohne eigenen Client waeren nur Zahlenadressen
 * erreichbar, und damit kein Server mit Namen.
 *
 * Also ein kleiner eigener Client ueber UDP: Anfrage bauen, an einen
 * Resolver schicken, A-Record auslesen. CNAME-Ketten in der Antwort
 * werden mitgelesen, Namenskompression beruecksichtigt.
 *
 * Als Resolver dient standardmaessig das Gateway aus dem DHCP-Lauf -
 * Heimrouter leiten DNS praktisch immer weiter. Wer das nicht hat, kann
 * in der Konfiguration einen anderen eintragen.
 */
#ifndef CF_DNS_H
#define CF_DNS_H

/* libogc2 hat kein netinet/in.h; in_addr und Verwandte stehen dort in
 * network.h. */
#include <network.h>

/* Setzt den Resolver. server ist eine Zahlenadresse wie "192.168.1.1". */
void cf_dns_set_server(const char *server);
const char *cf_dns_server(void);

/* Loest host auf. 1 bei Erfolg. Zahlenadressen werden direkt
 * uebernommen, ohne Anfrage. Ergebnisse werden klein zwischengespeichert,
 * weil beim Laden einer Quelle erst die Liste und dann der Datei-Server
 * gebraucht wird. */
int cf_dns_resolve(const char *host, struct in_addr *out);

void cf_dns_flush(void);

#endif
