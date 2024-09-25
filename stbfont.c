/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stbfont.h"

void init_font(struct fontData *data, int line_height)
{
	size_t size;
	char *font_path_env = getenv("DELCORE30M_FONT_PATH");

	FILE* font_file = fopen(font_path_env ? font_path_env : DEFAULT_FONT_PATH, "rb");
	if (font_file == NULL)
		error(EXIT_FAILURE, errno, "Failed to open font %s",
		      font_path_env ? font_path_env : DEFAULT_FONT_PATH);

	fseek(font_file, 0, SEEK_END);
	size = ftell(font_file);
	fseek(font_file, 0, SEEK_SET);

	data->fontbuf = malloc(size);

	fread(data->fontbuf, size, 1, font_file);
	fclose(font_file);

	if (!stbtt_InitFont(&data->font, data->fontbuf, 0))
		error(EXIT_FAILURE, 0, "Failed to init font");

	stbtt_GetFontVMetrics(&data->font, &data->ascent, &data->descent, &data->lineGap);
	data->scale = stbtt_ScaleForPixelHeight(&data->font, line_height);

	data->ascent *= data->scale;
	data->descent *= data->scale;
	data->lineGap *= data->scale;
}

void draw_string(struct fontData *font, uint8_t *dest, uint32_t width, char *text,
		 int line)
{
	uint32_t height = font->ascent - font->descent + font->lineGap;
	uint8_t *bitmap = calloc(height * width, sizeof(uint8_t));

	int x = 0;
	for (int i = 0; i < strlen(text); ++i) {

		int ax, kern, lsb;
		kern = stbtt_GetCodepointKernAdvance(&font->font, text[i], text[i + 1]);
		stbtt_GetCodepointHMetrics(&font->font, text[i], &ax, &lsb);

		int x1, y1, x2, y2;
		stbtt_GetCodepointBitmapBox(&font->font, text[i], font->scale, font->scale, &x1,
					    &y1, &x2, &y2);

		int byteOffset = x + (lsb * font->scale) + (font->ascent + y1) * width;
		stbtt_MakeCodepointBitmap(&font->font, bitmap + byteOffset, x2 - x1, y2 - y1,
					  width, font->scale, font->scale, text[i]);
		x += (ax * 1.2 + kern) * font->scale;
	}

	for (int i = 0; i < height; ++i)
		memcpy(dest + (i + line * height) * width, bitmap + i * width, x);

	free(bitmap);
}
