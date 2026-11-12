/* Uebersetzungen aus den Sprachdateien unter data/lang.
 *
 * Die Sprachdateien liegen in UTF-8 und behalten ihre Umlaute, damit sie
 * fuer eine spaetere GX-Oberflaeche mit FreeType unveraendert taugen. Die
 * libogc-Konsole kann nur ASCII, darum transliteriert cf_tc().
 */
#ifndef CF_I18N_H
#define CF_I18N_H

#include <stddef.h>

typedef enum {
	CF_LANG_EN = 0, CF_LANG_DE, CF_LANG_FR, CF_LANG_ES, CF_LANG_IT,
	CF_LANG_RU, CF_LANG_JA, CF_LANG_COUNT
} cf_lang;

const char *cf_lang_code(cf_lang l);

/* Name der Sprache in ihr selbst, fuer die Einstellungen. */
const char *cf_lang_name(cf_lang l);

/* Kuerzel -> Sprache; unbekannt ergibt Englisch. */
cf_lang     cf_lang_from_code(const char *code);

/* Uebernimmt den Inhalt einer Sprachdatei. Der Puffer muss dem Aufrufer
 * gehoeren und darf bis zum naechsten Laden nicht freigegeben werden -
 * die Tabelle zeigt hinein. Gibt die Anzahl der Schluessel zurueck. */
size_t cf_i18n_load(char *text);

/* Wie oben, aber fuer die Ersatztabelle, die cf_t fragt, wenn ein
 * Schluessel in der geladenen Sprache fehlt. */
size_t cf_i18n_load_fallback(char *text);

/* Eingebaute englische Texte, falls keine Sprachdatei gefunden wurde. */
void cf_i18n_builtin(void);

/* Uebersetzung oder, wenn unbekannt, der Schluessel selbst. So faellt im
 * Test sofort auf, was noch fehlt. */
const char *cf_t(const char *key);

/* Wie cf_t, aber nach ASCII transliteriert, in einem rotierenden
 * internen Puffer. Fuer die Konsolenausgabe. */
const char *cf_tc(const char *key);

/* Transliteriert beliebigen UTF-8-Text fuer die Konsole. */
const char *cf_ascii(const char *utf8);

#endif
