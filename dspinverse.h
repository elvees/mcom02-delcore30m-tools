/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */
#ifndef _DSPINVERSE_H_
#define _DSPINVERSE_H_

#include <stdbool.h>
#include <stdint.h>
#include <asm/types.h>

#include <linux/delcore30m.h>

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

enum pixel_format {
	PIXEL_FORMAT_RGB = 3,
	PIXEL_FORMAT_RGBA = 4
};

struct frame_args {
	uint16_t frame_width;
	uint16_t frame_height;
	uint16_t tile_width;
	uint16_t tile_height;
	enum pixel_format pixel_format;
};

struct dsp_struct {
	int fd;
	struct delcore30m_resource core;
	struct delcore30m_resource sdma;
	struct delcore30m_job job;

	//source and result frames
	struct delcore30m_buffer *result_frame[2];
	struct delcore30m_buffer *tile_buffers[2];
	struct delcore30m_buffer *code_buffers[2];
	struct delcore30m_buffer *chain_buffers[2];
	struct delcore30m_buffer *tileinfo_buffer;
	struct delcore30m_buffer *channel_buffer;

	uint8_t *result_frame_data[2];

	uint32_t sdma_channels[2];
};

void dsp_init(struct dsp_struct *data, const struct frame_args frame_data);
void dsp_free(struct dsp_struct *data);
void dsp_job_create(struct dsp_struct *data, const int bufs_fd[], const int count);
int frame_inverse(struct dsp_struct *data, int source_fd, int dest_buf);

#endif
