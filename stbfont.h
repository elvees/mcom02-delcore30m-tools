/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */
#ifndef _STBFONT_H
#define _STBFONT_H

#include <stdint.h>

#include "stb/stb_truetype.h"

#define DEFAULT_FONT_PATH "/usr/share/fonts/ubuntu/Ubuntu-Regular.ttf"

struct fontData {
	stbtt_fontinfo font;
	uint8_t *fontbuf;
	int ascent;
	int descent;
	int lineGap;
	float scale;
};

void init_font(struct fontData *data, int line_height);

void draw_string(struct fontData *font, uint8_t *dest, uint32_t width, char *text, int line);

#endif
