#include "platform/cf_gfx.h"

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <ogc/gx.h>
#include <ogc/gu.h>

#include <ft2build.h>
#include FT_FREETYPE_H

/* ------------------------------------------------------------------ */
/* Bildschirm                                                         */
/* ------------------------------------------------------------------ */

#define FIFO_SIZE (256 * 1024)

static GXRModeObj *g_rmode;
static void       *g_xfb[2];
static int         g_fb;
static void       *g_fifo;
static int         g_ready;

/* Logische 640 x 480 auf die tatsaechliche EFB-Groesse: bei PAL mit 528
 * Zeilen wird vertikal leicht gestreckt, so wie es PAL-Spiele auch tun. */
static float g_sx = 1.0f, g_sy = 1.0f;

static FT_Library g_ft;

static inline void color_split(uint32_t rgba, GXColor *c)
{
	c->r = (rgba >> 24) & 0xFF;
	c->g = (rgba >> 16) & 0xFF;
	c->b = (rgba >> 8) & 0xFF;
	c->a = rgba & 0xFF;
}

int cf_gfx_init(void)
{
	Mtx44 proj;
	Mtx   mv;
	GXColor black = { 0, 0, 0, 255 };
	f32 yscale;
	u32 xfbh;

	VIDEO_Init();
	PAD_Init();

	g_rmode = VIDEO_GetPreferredMode(NULL);

	g_xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_rmode));
	g_xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_rmode));
	VIDEO_ClearFrameBuffer(g_rmode, g_xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(g_rmode, g_xfb[1], COLOR_BLACK);

	VIDEO_Configure(g_rmode);
	VIDEO_SetNextFramebuffer(g_xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (g_rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	g_fifo = memalign(32, FIFO_SIZE);
	if (!g_fifo)
		return 0;
	memset(g_fifo, 0, FIFO_SIZE);
	GX_Init(g_fifo, FIFO_SIZE);

	GX_SetCopyClear(black, GX_MAX_Z24);

	GX_SetViewport(0, 0, g_rmode->fbWidth, g_rmode->efbHeight, 0, 1);
	yscale = GX_GetYScaleFactor(g_rmode->efbHeight, g_rmode->xfbHeight);
	xfbh = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0, 0, g_rmode->fbWidth, g_rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, g_rmode->fbWidth, g_rmode->efbHeight);
	GX_SetDispCopyDst(g_rmode->fbWidth, xfbh);
	GX_SetCopyFilter(g_rmode->aa, g_rmode->sample_pattern, GX_TRUE,
	                 g_rmode->vfilter);
	GX_SetFieldMode(g_rmode->field_rendering,
	                ((g_rmode->viHeight == 2 * g_rmode->xfbHeight)
	                 ? GX_ENABLE : GX_DISABLE));
	GX_SetPixelFmt(g_rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24,
	               GX_ZC_LINEAR);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	g_sx = (float)g_rmode->fbWidth / CF_GFX_W;
	g_sy = (float)g_rmode->efbHeight / CF_GFX_H;

	/* Ein Vertexformat fuer alles: Position, Farbe, Texturkoordinate. */
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetNumTevStages(1);

	/* Alpha-Mischung fuer Schrift und halbdurchsichtige Flaechen. Tiefe
	 * braucht es in zwei Dimensionen nicht. */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
	                GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_FALSE);
	GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
	GX_SetColorUpdate(GX_TRUE);

	/* Orthographisch, Ursprung oben links, eine Einheit ein Bildpunkt. */
	guOrtho(proj, 0, CF_GFX_H, 0, CF_GFX_W, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	guMtxIdentity(mv);
	GX_LoadPosMtxImm(mv, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);

	GX_CopyDisp(g_xfb[0], GX_TRUE);
	GX_Flush();

	if (FT_Init_FreeType(&g_ft) != 0)
		return 0;

	g_fb = 1;
	g_ready = 1;
	return 1;
}

void cf_gfx_begin(uint32_t bg)
{
	GXColor c;

	color_split(bg, &c);
	c.a = 255;
	GX_SetCopyClear(c, GX_MAX_Z24);
	GX_InvVtxCache();
	GX_InvalidateTexAll();
	cf_gfx_noclip();
}

void cf_gfx_end(void)
{
	GX_DrawDone();
	GX_SetColorUpdate(GX_TRUE);
	GX_CopyDisp(g_xfb[g_fb], GX_TRUE);
	GX_Flush();

	VIDEO_SetNextFramebuffer(g_xfb[g_fb]);
	VIDEO_Flush();
	VIDEO_WaitVSync();

	g_fb ^= 1;
}

/* ------------------------------------------------------------------ */
/* Flaechen                                                           */
/* ------------------------------------------------------------------ */

static void tev_color(void)
{
	GX_SetNumTexGens(0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL,
	               GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}

static void tev_texture(const GXTexObj *obj)
{
	GX_LoadTexObj((GXTexObj *)obj, GX_TEXMAP0);
	GX_SetNumTexGens(1);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
}

static inline void vertex(float x, float y, uint32_t rgba, float s, float t)
{
	GX_Position3f32(x, y, 0.0f);
	GX_Color1u32(rgba);
	GX_TexCoord2f32(s, t);
}

void cf_gfx_rect(int x, int y, int w, int h, uint32_t rgba)
{
	if (w <= 0 || h <= 0 || !g_ready)
		return;

	tev_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vertex((float)x,       (float)y,       rgba, 0, 0);
	vertex((float)(x + w), (float)y,       rgba, 0, 0);
	vertex((float)(x + w), (float)(y + h), rgba, 0, 0);
	vertex((float)x,       (float)(y + h), rgba, 0, 0);
	GX_End();
}

void cf_gfx_frame(int x, int y, int w, int h, int thick, uint32_t rgba)
{
	cf_gfx_rect(x, y, w, thick, rgba);
	cf_gfx_rect(x, y + h - thick, w, thick, rgba);
	cf_gfx_rect(x, y, thick, h, rgba);
	cf_gfx_rect(x + w - thick, y, thick, h, rgba);
}

/* Der aktuelle Ausschnitt in logischen Bildpunkten. Gemerkt, damit ein
 * innerer Ausschnitt (Lauftext in einer Listenzeile) sich mit dem
 * aeusseren (der Liste) schneiden und ihn danach wiederherstellen kann. */
static int g_clip[4] = { 0, 0, CF_GFX_W, CF_GFX_H };
static int g_clip_stack[4][4];
static int g_clip_depth;

static void scissor_apply(void)
{
	GX_SetScissor((u32)(g_clip[0] * g_sx), (u32)(g_clip[1] * g_sy),
	              (u32)(g_clip[2] * g_sx), (u32)(g_clip[3] * g_sy));
}

void cf_gfx_clip(int x, int y, int w, int h)
{
	if (!g_ready)
		return;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (w < 0) w = 0;
	if (h < 0) h = 0;
	g_clip[0] = x; g_clip[1] = y; g_clip[2] = w; g_clip[3] = h;
	g_clip_depth = 0;
	scissor_apply();
}

void cf_gfx_noclip(void)
{
	if (!g_ready)
		return;
	g_clip[0] = 0; g_clip[1] = 0;
	g_clip[2] = CF_GFX_W; g_clip[3] = CF_GFX_H;
	g_clip_depth = 0;
	GX_SetScissor(0, 0, g_rmode->fbWidth, g_rmode->efbHeight);
}

void cf_gfx_clip_push(int x, int y, int w, int h)
{
	int x1 = x + w, y1 = y + h;
	int cx1 = g_clip[0] + g_clip[2], cy1 = g_clip[1] + g_clip[3];

	if (!g_ready)
		return;
	if (g_clip_depth < 4)
		memcpy(g_clip_stack[g_clip_depth], g_clip, sizeof(g_clip));
	g_clip_depth++;

	/* Mit dem bisherigen Ausschnitt schneiden. */
	if (x < g_clip[0]) x = g_clip[0];
	if (y < g_clip[1]) y = g_clip[1];
	if (x1 > cx1) x1 = cx1;
	if (y1 > cy1) y1 = cy1;
	g_clip[0] = x; g_clip[1] = y;
	g_clip[2] = x1 > x ? x1 - x : 0;
	g_clip[3] = y1 > y ? y1 - y : 0;
	scissor_apply();
}

void cf_gfx_clip_pop(void)
{
	if (!g_ready || g_clip_depth == 0)
		return;
	g_clip_depth--;
	if (g_clip_depth < 4)
		memcpy(g_clip, g_clip_stack[g_clip_depth], sizeof(g_clip));
	scissor_apply();
}

/* ------------------------------------------------------------------ */
/* Texturen                                                           */
/*                                                                    */
/* Der Grafikchip erwartet Texturen in Kacheln von 4 x 4 Bildpunkten.  */
/* RGBA8 legt je Kachel erst 16 Paare (A,R) ab, dann 16 Paare (G,B);   */
/* IA8 je Kachel 16 Paare (A,I). Breite und Hoehe werden auf Vielfache */
/* von 4 aufgefuellt.                                                 */
/* ------------------------------------------------------------------ */

static int tex_pad(int v)
{
	return (v + 3) & ~3;
}

int cf_gfx_tex_rgba8(cf_tex *t, int w, int h, const uint8_t *rgba)
{
	int pw = tex_pad(w), ph = tex_pad(h);
	size_t bytes = (size_t)pw * ph * 4;
	uint8_t *dst;
	int ty, tx, y, x;

	memset(t, 0, sizeof(*t));
	if (w <= 0 || h <= 0 || pw > 1024 || ph > 1024)
		return 0;

	dst = memalign(32, bytes);
	if (!dst)
		return 0;

	for (ty = 0; ty < ph; ty += 4) {
		for (tx = 0; tx < pw; tx += 4) {
			uint8_t *tile = dst + ((size_t)(ty / 4) * (pw / 4)
			                       + (tx / 4)) * 64;
			for (y = 0; y < 4; y++) {
				for (x = 0; x < 4; x++) {
					int sx = tx + x, sy = ty + y;
					uint8_t r = 0, g = 0, b = 0, a = 0;
					int i = (y * 4 + x) * 2;

					if (sx < w && sy < h) {
						const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
						r = p[0]; g = p[1]; b = p[2]; a = p[3];
					}
					tile[i]          = a;
					tile[i + 1]      = r;
					tile[32 + i]     = g;
					tile[32 + i + 1] = b;
				}
			}
		}
	}

	DCFlushRange(dst, bytes);
	GX_InitTexObj(&t->obj, dst, (u16)pw, (u16)ph, GX_TF_RGBA8,
	              GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&t->obj, GX_LINEAR, GX_LINEAR, 0, 0, 0,
	                 GX_FALSE, GX_FALSE, GX_ANISO_1);

	t->data = dst;
	t->w = w;
	t->h = h;
	return 1;
}

void cf_gfx_tex_free(cf_tex *t)
{
	free(t->data);
	memset(t, 0, sizeof(*t));
}

void cf_gfx_draw_tex(const cf_tex *t, int x, int y, int w, int h,
                     uint32_t tint)
{
	float s1, t1;

	if (!t || !t->data || !g_ready)
		return;

	/* Nur den belegten Teil der aufgefuellten Textur zeigen. */
	s1 = (float)t->w / tex_pad(t->w);
	t1 = (float)t->h / tex_pad(t->h);

	tev_texture(&t->obj);
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vertex((float)x,       (float)y,       tint, 0,  0);
	vertex((float)(x + w), (float)y,       tint, s1, 0);
	vertex((float)(x + w), (float)(y + h), tint, s1, t1);
	vertex((float)x,       (float)(y + h), tint, 0,  t1);
	GX_End();
}

/* ------------------------------------------------------------------ */
/* Schrift                                                            */
/* ------------------------------------------------------------------ */

/* Welche Zeichen in den Atlas kommen: Latin-1, die typographischen
 * Zeichen aus Spieletiteln, Kyrillisch und dazu jedes Zeichen der
 * aktiven Sprachdatei (fuer Japanisch). Alles andere wird als '?'
 * gezeichnet. Der Satz ist sortiert, gesucht wird binaer. */
static const uint32_t extra_cps[] = {
	0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C, 0x201D, 0x201E,
	0x2022, 0x2026, 0x20AC, 0x2122, 0x2190, 0x2191, 0x2192, 0x2193,
	0x25B6, 0x25C0, 0x2605, 0x2606, 0x2713, 0x2717, 0x0152, 0x0153,
	0x0160, 0x0161, 0x017D, 0x017E, 0x0178, 0x00B7, 0x3000, 0x30FB
};
#define N_EXTRA (sizeof(extra_cps) / sizeof(extra_cps[0]))
#define CS_MAX  4096

static uint32_t g_cs[CS_MAX];
static int      g_ncs;

/* Zweitschrift fuer Zeichen, die der Hauptschrift fehlen (Japanisch). */
static const void *g_fb_ttf;
static size_t      g_fb_len;

typedef struct {
	uint16_t x, y, w, h;   /* im Atlas */
	int16_t  left, top;    /* Versatz zur Grundlinie */
	int16_t  advance;
} glyph;

struct cf_font {
	cf_tex    atlas;
	int       aw, ah;       /* Atlasgroesse */
	int       height;       /* Zeilenabstand */
	int       ascent;
	int       n;
	uint32_t *cp;           /* sortiert, n Eintraege */
	glyph    *g;
	int       qmark;        /* Index von '?' */
};

static uint32_t utf8_next(const char **p);

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y;
}

static void cs_add(uint32_t cp)
{
	if (g_ncs < CS_MAX)
		g_cs[g_ncs++] = cp;
}

void cf_gfx_charset(const char *text)
{
	uint32_t c;
	size_t i;
	int r, w;

	g_ncs = 0;
	for (c = 0x20; c < 0x7F; c++)
		cs_add(c);
	for (c = 0xA0; c < 0x100; c++)
		cs_add(c);
	for (i = 0; i < N_EXTRA; i++)
		cs_add(extra_cps[i]);
	for (c = 0x400; c < 0x460; c++)
		cs_add(c);

	if (text) {
		const char *p = text;
		while (*p) {
			uint32_t cp = utf8_next(&p);
			if (cp >= 0x100)
				cs_add(cp);
		}
	}

	qsort(g_cs, (size_t)g_ncs, sizeof(g_cs[0]), cmp_u32);
	for (r = 0, w = 0; r < g_ncs; r++)
		if (w == 0 || g_cs[r] != g_cs[w - 1])
			g_cs[w++] = g_cs[r];
	g_ncs = w;
}

void cf_gfx_set_fallback(const void *ttf, size_t len)
{
	g_fb_ttf = ttf;
	g_fb_len = len;
}

static int glyph_index(const cf_font *f, uint32_t cp)
{
	int lo = 0, hi = f->n - 1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (f->cp[mid] == cp)
			return mid;
		if (f->cp[mid] < cp)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return f->qmark;
}

/* Ein UTF-8-Zeichen lesen; bei kaputten Folgen ein Byte weiter. */
static uint32_t utf8_next(const char **p)
{
	const unsigned char *s = (const unsigned char *)*p;
	uint32_t cp;
	int n;

	if (s[0] < 0x80) { cp = s[0]; n = 1; }
	else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
		cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); n = 2;
	} else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 &&
	           (s[2] & 0xC0) == 0x80) {
		cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
		n = 3;
	} else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 &&
	           (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
		cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
		     ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
		n = 4;
	} else {
		cp = '?'; n = 1;
	}

	*p += n;
	return cp;
}

/* IA8-Atlas: ein Bildpunkt ist (Alpha, Intensitaet). Intensitaet steht
 * auf 255, die Deckung aus FreeType geht in Alpha; die Farbe kommt beim
 * Zeichnen aus der Vertexfarbe. */
static void atlas_put(uint8_t *atlas, int aw, int x, int y, uint8_t cov)
{
	int tx = x / 4, ty = y / 4;
	int ix = x & 3, iy = y & 3;
	uint8_t *tile = atlas + ((size_t)ty * (aw / 4) + tx) * 32;
	int i = (iy * 4 + ix) * 2;

	tile[i]     = cov;
	tile[i + 1] = 0xFF;
}

static void font_free_parts(cf_font *f)
{
	if (!f)
		return;
	free(f->cp);
	free(f->g);
	free(f);
}

static cf_font *font_build(FT_Face face, FT_Face fb, int aw, int ah)
{
	cf_font *f;
	uint8_t *atlas;
	size_t bytes = (size_t)aw * ah * 2;
	int pen_x = 1, pen_y = 1, row_h = 0;
	int i;

	if (!g_ncs)
		cf_gfx_charset(NULL);

	f = calloc(1, sizeof(*f));
	atlas = memalign(32, bytes);
	if (f) {
		f->n = g_ncs;
		f->cp = malloc(sizeof(uint32_t) * (size_t)g_ncs);
		f->g = calloc((size_t)g_ncs, sizeof(glyph));
	}
	if (!f || !atlas || !f->cp || !f->g) {
		font_free_parts(f);
		free(atlas);
		return NULL;
	}
	memcpy(f->cp, g_cs, sizeof(uint32_t) * (size_t)g_ncs);
	memset(atlas, 0, bytes);

	f->aw = aw;
	f->ah = ah;
	f->height = (int)(face->size->metrics.height >> 6);
	f->ascent = (int)(face->size->metrics.ascender >> 6);
	f->qmark = 0;

	for (i = 0; i < f->n; i++) {
		uint32_t cp = f->cp[i];
		FT_Face use = face;
		FT_Bitmap *bm;
		glyph *g = &f->g[i];
		int y, x;

		if (cp == '?')
			f->qmark = i;

		if (!FT_Get_Char_Index(face, cp)) {
			if (fb && FT_Get_Char_Index(fb, cp))
				use = fb;
			else
				continue;
		}
		if (FT_Load_Char(use, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
			continue;

		bm = &use->glyph->bitmap;
		g->advance = (int16_t)(use->glyph->advance.x >> 6);
		g->left = (int16_t)use->glyph->bitmap_left;
		g->top = (int16_t)use->glyph->bitmap_top;
		g->w = (uint16_t)bm->width;
		g->h = (uint16_t)bm->rows;

		if (!g->w || !g->h)
			continue;

		/* Regalpacker: Zeile fuer Zeile, mit einem Bildpunkt Rand. */
		if (pen_x + g->w + 1 > aw) {
			pen_x = 1;
			pen_y += row_h + 1;
			row_h = 0;
		}
		if (pen_y + g->h + 1 > ah) {
			/* Passt nicht - der Aufrufer versucht es groesser. */
			free(atlas);
			font_free_parts(f);
			return NULL;
		}

		g->x = (uint16_t)pen_x;
		g->y = (uint16_t)pen_y;

		for (y = 0; y < (int)bm->rows; y++)
			for (x = 0; x < (int)bm->width; x++)
				atlas_put(atlas, aw, pen_x + x, pen_y + y,
				          bm->buffer[y * bm->pitch + x]);

		pen_x += g->w + 1;
		if (g->h > row_h)
			row_h = g->h;
	}

	/* Fehlende Zeichen zeigen das Fragezeichen. */
	for (i = 0; i < f->n; i++)
		if (!f->g[i].advance && f->cp[i] != ' ')
			f->g[i] = f->g[f->qmark];

	DCFlushRange(atlas, bytes);
	GX_InitTexObj(&f->atlas.obj, atlas, (u16)aw, (u16)ah, GX_TF_IA8,
	              GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(&f->atlas.obj, GX_NEAR, GX_NEAR, 0, 0, 0,
	                 GX_FALSE, GX_FALSE, GX_ANISO_1);
	f->atlas.data = atlas;
	f->atlas.w = aw;
	f->atlas.h = ah;
	return f;
}

cf_font *cf_font_load(const void *ttf, size_t len, int px)
{
	FT_Face face, fb = NULL;
	cf_font *f = NULL;
	static const int sizes[][2] = {
		{ 256, 256 }, { 512, 256 }, { 512, 512 }, { 1024, 512 },
		{ 1024, 1024 }
	};
	size_t i;

	if (!g_ready)
		return NULL;

	if (FT_New_Memory_Face(g_ft, ttf, (FT_Long)len, 0, &face) != 0)
		return NULL;
	if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)px) != 0) {
		FT_Done_Face(face);
		return NULL;
	}
	if (g_fb_ttf &&
	    FT_New_Memory_Face(g_ft, g_fb_ttf, (FT_Long)g_fb_len, 0, &fb) == 0 &&
	    FT_Set_Pixel_Sizes(fb, 0, (FT_UInt)px) != 0) {
		FT_Done_Face(fb);
		fb = NULL;
	}

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]) && !f; i++)
		f = font_build(face, fb, sizes[i][0], sizes[i][1]);

	if (fb)
		FT_Done_Face(fb);
	FT_Done_Face(face);
	return f;
}

void cf_font_free(cf_font *f)
{
	if (!f)
		return;
	free(f->atlas.data);
	font_free_parts(f);
}

int cf_font_height(const cf_font *f) { return f ? f->height : 16; }
int cf_font_ascent(const cf_font *f) { return f ? f->ascent : 12; }

int cf_gfx_text_width(const cf_font *f, const char *utf8)
{
	const char *p = utf8;
	int w = 0;

	if (!f || !utf8)
		return 0;

	while (*p)
		w += f->g[glyph_index(f, utf8_next(&p))].advance;
	return w;
}

/* y ist die Oberkante der Zeile; die Grundlinie liegt bei y + ascent. */
int cf_gfx_text(const cf_font *f, int x, int y, uint32_t rgba,
                const char *utf8)
{
	const char *p = utf8;
	int pen = x;
	int base = y + f->ascent;
	float inv_w, inv_h;
	int count = 0;

	if (!f || !utf8 || !*utf8 || !g_ready)
		return 0;

	/* Zeichen zaehlen, damit GX_Begin die Zahl der Ecken kennt. */
	while (*p) {
		if (f->g[glyph_index(f, utf8_next(&p))].w)
			count++;
	}
	if (!count)
		return cf_gfx_text_width(f, utf8);

	inv_w = 1.0f / f->aw;
	inv_h = 1.0f / f->ah;

	tev_texture(&f->atlas.obj);
	GX_Begin(GX_QUADS, GX_VTXFMT0, (u16)(count * 4));

	p = utf8;
	while (*p) {
		const glyph *g = &f->g[glyph_index(f, utf8_next(&p))];

		if (g->w) {
			float x0 = (float)(pen + g->left);
			float y0 = (float)(base - g->top);
			float x1 = x0 + g->w;
			float y1 = y0 + g->h;
			float s0 = g->x * inv_w, t0 = g->y * inv_h;
			float s1 = (g->x + g->w) * inv_w, t1 = (g->y + g->h) * inv_h;

			vertex(x0, y0, rgba, s0, t0);
			vertex(x1, y0, rgba, s1, t0);
			vertex(x1, y1, rgba, s1, t1);
			vertex(x0, y1, rgba, s0, t1);
		}
		pen += g->advance;
	}

	GX_End();
	return pen - x;
}

int cf_gfx_text_fit(const cf_font *f, int x, int y, int maxw,
                    uint32_t rgba, const char *utf8)
{
	char buf[512];
	const char *p = utf8;
	int w, ell;
	size_t n = 0;

	if (!f || !utf8)
		return 0;

	w = cf_gfx_text_width(f, utf8);
	if (w <= maxw)
		return cf_gfx_text(f, x, y, rgba, utf8);

	/* Zeichenweise kuerzen, bis es mit den Punkten passt. */
	ell = cf_gfx_text_width(f, "\xE2\x80\xA6");   /* … */
	w = 0;
	while (*p && n < sizeof(buf) - 4) {
		const char *q = p;
		uint32_t cp = utf8_next(&q);
		int adv = f->g[glyph_index(f, cp)].advance;

		if (w + adv + ell > maxw)
			break;
		memcpy(buf + n, p, (size_t)(q - p));
		n += (size_t)(q - p);
		w += adv;
		p = q;
	}
	memcpy(buf + n, "\xE2\x80\xA6", 3);
	buf[n + 3] = '\0';

	return cf_gfx_text(f, x, y, rgba, buf);
}
