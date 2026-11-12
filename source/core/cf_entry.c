#include "core/cf_entry.h"
#include "core/cf_str.h"

#include <string.h>

void cf_entry_title_from_path(cf_entry *e)
{
	const char *base;
	const char *slash;
	size_t len;

	base = e->path;
	slash = strrchr(e->path, '/');
	if (slash)
		base = slash + 1;

	len = strlen(base);

	/* Endung abschneiden, aber nur eine echte Endung am Ende. */
	{
		const char *dot = strrchr(base, '.');
		if (dot && dot != base && (size_t)(len - (size_t)(dot - base)) <= 6)
			len = (size_t)(dot - base);
	}

	if (len > CF_MAX_TITLE - 1)
		len = CF_MAX_TITLE - 1;
	memcpy(e->title, base, len);
	e->title[len] = '\0';
}

void cf_entry_mark_image(cf_entry *e)
{
	static const char *exts[] = { ".iso", ".gcm", ".rvz", ".ciso", ".nkit.iso" };
	size_t i, plen = strlen(e->path);

	e->is_image = 0;
	for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
		size_t el = strlen(exts[i]);
		if (plen < el)
			continue;
		{
			const char *tail = e->path + plen - el;
			size_t k;
			int same = 1;
			for (k = 0; k < el; k++) {
				if (cf_lower(tail[k]) != exts[i][k]) {
					same = 0;
					break;
				}
			}
			if (same) {
				e->is_image = 1;
				return;
			}
		}
	}
}
