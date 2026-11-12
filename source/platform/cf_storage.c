#include "platform/cf_storage.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <gccore.h>
#include <fat.h>
#include <sdcard/gcsd.h>

static cf_device g_dev[CF_MAX_DEVICES];
static size_t    g_n;

struct candidate {
	const char *mount;
	const char *label;
	/* Ohne const: libogc2 nimmt in fatMountSimple ein
	 * DISC_INTERFACE* ohne const. */
	DISC_INTERFACE *io;
	int exi_channel;
};

/* Reihenfolge ist die Vorschlagsreihenfolge in der Oberflaeche. SD2SP2
 * steht vorn, weil es sich mit einem Netzwerkadapter an Serial Port 1
 * nicht um den EXI-Kanal streitet. */
static const struct candidate cands[] = {
	{ "sd2",   "SD2SP2",            &__io_gcsd2, 2 },
	{ "cardb", "SD Gecko (Slot B)", &__io_gcsdb, 1 },
	{ "carda", "SD Gecko (Slot A)", &__io_gcsda, 0 },
};
#define CAND_COUNT (sizeof(cands) / sizeof(cands[0]))

void cf_storage_refresh(size_t i)
{
	struct statvfs st;
	cf_device *d;

	if (i >= g_n)
		return;
	d = &g_dev[i];

	d->total = 0;
	d->avail = 0;

	if (statvfs(d->root, &st) == 0) {
		uint64_t unit = st.f_frsize ? st.f_frsize : st.f_bsize;
		d->total = (uint64_t)st.f_blocks * unit;
		d->avail = (uint64_t)st.f_bavail * unit;
	}
}

size_t cf_storage_scan(void)
{
	size_t i;

	g_n = 0;

	for (i = 0; i < CAND_COUNT && g_n < CF_MAX_DEVICES; i++) {
		cf_device *d = &g_dev[g_n];

		if (!fatMountSimple(cands[i].mount, cands[i].io))
			continue;

		memset(d, 0, sizeof(*d));
		snprintf(d->mount, sizeof(d->mount), "%s", cands[i].mount);
		snprintf(d->root, sizeof(d->root), "%s:/", cands[i].mount);
		snprintf(d->label, sizeof(d->label), "%s", cands[i].label);
		d->exi_channel = cands[i].exi_channel;
		d->mounted = 1;


		g_n++;
		cf_storage_refresh(g_n - 1);
	}

	return g_n;
}

size_t cf_storage_count(void) { return g_n; }

const cf_device *cf_storage_at(size_t i)
{
	if (i >= g_n)
		return NULL;
	return &g_dev[i];
}

int cf_mkdir_p(const char *path)
{
	char tmp[320];
	size_t n;
	char *p;

	n = strlen(path);
	if (n == 0 || n >= sizeof(tmp))
		return 0;
	memcpy(tmp, path, n + 1);

	/* Abschliessenden Schraegstrich entfernen */
	while (n > 1 && tmp[n - 1] == '/')
		tmp[--n] = '\0';

	/* Hinter "sd2:/" anfangen, damit der Geraeteteil nicht als
	 * Verzeichnis angelegt wird. */
	p = strchr(tmp, ':');
	p = p ? p + 1 : tmp;
	if (*p == '/')
		p++;

	for (; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
			*p = '/';
			return 0;
		}
		*p = '/';
	}

	if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
		return 0;
	return 1;
}

uint64_t cf_file_size(const char *path)
{
	struct stat st;

	if (stat(path, &st) != 0)
		return 0;
	return (uint64_t)st.st_size;
}

int cf_file_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

char *cf_read_text(const char *path, size_t *len_out)
{
	FILE *f;
	long sz;
	char *buf;
	size_t rd;

	if (len_out)
		*len_out = 0;

	f = fopen(path, "rb");
	if (!f)
		return NULL;

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	sz = ftell(f);
	if (sz < 0 || sz > 4 * 1024 * 1024) {
		fclose(f);
		return NULL;
	}
	rewind(f);

	buf = malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}

	rd = fread(buf, 1, (size_t)sz, f);
	fclose(f);

	buf[rd] = '\0';
	if (len_out)
		*len_out = rd;
	return buf;
}

int cf_write_text(const char *path, const char *text)
{
	FILE *f;
	size_t n, w;

	f = fopen(path, "wb");
	if (!f)
		return 0;

	n = strlen(text);
	w = fwrite(text, 1, n, f);

	if (fclose(f) != 0)
		return 0;
	return w == n;
}
