/*
 * \file
 * \brief delcore30m-inversiontest - Inversion image on DSP
 *
 * \copyright
 * Copyright 2018-2019 RnD Center "ELVEES", JSC
 *
 */
#ifndef _DELCORE30M_INVERSIONTEST_H_
#define _DELCORE30M_INVERSIONTEST_H_

/// Integer division with rounding up
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/// Get a minimum of two arguments
#define min(x, y) ({ \
		typeof(x) _x = (x); \
		typeof(y) _y = (y); \
		_x < _y ? _x : _y; })

 /// Get a maximum of two arguments
#define max(x, y) ({ \
		typeof(x) _x = (x); \
		typeof(y) _y = (y); \
		_x > _y ? _x : _y; })

#define SCR_BURST_SIZE_BIT 1
#define DST_BURST_SIZE_BIT 15

#define BURST_SIZE_1BYTE 0
#define BURST_SIZE_2BYTE 1
#define BURST_SIZE_4BYTE 2
#define BURST_SIZE_8BYTE 3
#define BURST_SIZE_16BYTE 4

#define SRC_AUTO_INCREMENT_BIT 0
#define DST_AUTO_INCREMENT_BIT 14

#define AUTO_INCREMENT 1


struct tileinfo {
	uint32_t x, y;
	uint32_t width, height;
	uint32_t stride[2];
};

struct tilesbuffer {
	uint32_t ntiles;
	uint32_t tilesize;
	uint32_t pixel_size;
	struct tileinfo info[];
};

#endif
