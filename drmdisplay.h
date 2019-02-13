/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

#ifndef DRMDISPLAY_H
#define DRMDISPLAY_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <xf86drmMode.h>

extern pthread_t thread;
extern pthread_cond_t cv;
extern pthread_mutex_t lock;

struct drmdisplay {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	uint32_t fb;
	size_t fb_size;
	drmModeModeInfo *modes;
	int count_modes;
	uint32_t old_fb;
	drmModeModeInfo old_mode;
	unsigned int fb_id[2], current_fb_id;
	uint32_t pitch;
};

/* Check DRM, scan available modes and get current mode.
 * Return 0 on success or -1 on error.
 */
int drmdisplay_init(struct drmdisplay *data, int connector_id, bool verbose);

/* Fill width and height variables.
 * If all variables are 0 then will used current display mode.
 * If some variables are non-zero then will fill another variables by correct values.
 * Return 0 on success or -1 if no suitable modes found.
 */
int drmdisplay_fill_mode(struct drmdisplay *data, int *width, int *height);

/* Create new framebuffer with specified sizes and change resolution to
 * use this framebuffer.
 * Return 0 on success or -1 on error.
 */
void drmdisplay_set_mode(struct drmdisplay *data, int width, int height, int bpp, int fd);

/* Restore old mode and free all resources.
 * Return 0 on success or -1 on error.
 */
int drmdisplay_restore_mode(struct drmdisplay *data);

/* Start flipflop */
void drmdisplay_start_flipflop(struct drmdisplay *data, int width, int height, int bpp, int fd);

#endif
