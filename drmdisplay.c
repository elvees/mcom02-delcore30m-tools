/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "drmdisplay.h"

static void page_flip_handler(int fd, unsigned int frame, unsigned int sec,
			      unsigned int usec, void *data)
{
	struct drmdisplay *pipe = data;

	/* wait buffer ready event */
	pthread_mutex_lock(&lock);
	pthread_cond_wait(&cv, &lock);
	pthread_mutex_unlock(&lock);

	if (pipe->current_fb_id == pipe->fb_id[0])
		pipe->current_fb_id = pipe->fb_id[1];
	else
		pipe->current_fb_id = pipe->fb_id[0];

	drmModePageFlip(fd, pipe->crtc_id, pipe->current_fb_id,
			DRM_MODE_PAGE_FLIP_EVENT, pipe);
}

static void *pthread_worker(void *private_data)
{
	struct drmdisplay *data = private_data;
	int ret;

	drmEventContext evctx;

	memset(&evctx, 0, sizeof evctx);
	evctx.version = DRM_EVENT_CONTEXT_VERSION;
	evctx.vblank_handler = NULL;
	evctx.page_flip_handler = page_flip_handler;

	while (1) {
		struct timeval timeout = { .tv_sec = 3, .tv_usec = 0 };
		fd_set fds;

		FD_ZERO(&fds);
		FD_SET(0, &fds);
		FD_SET(data->fd, &fds);
		ret = select(data->fd + 1, &fds, NULL, NULL, &timeout);

		if (ret <= 0) {
			fprintf(stderr, "select timed out or error (ret %d)\n",
				ret);
			continue;
		}

		drmHandleEvent(data->fd, &evctx);
	}
	pthread_exit(NULL);
}

void drmdisplay_start_flipflop(struct drmdisplay *data, int width, int height, int bpp, int fd)
{
	uint32_t handle;
	uint32_t another_fb;

	int ret = drmPrimeFDToHandle(data->fd, fd, &handle);
	if (ret < 0)
		error(EXIT_FAILURE, errno, "Can not import dmabuf");

	data->fb_size = height * width * bpp;
	uint32_t pitch = width * bpp;
	ret = drmModeAddFB(data->fd, width, height, 24, 32, pitch, handle, &another_fb);
	if (ret)
		error(EXIT_FAILURE, errno, "Can not create framebuffer via drmModeAddFB()");

	ret = drmModePageFlip(data->fd, data->crtc_id, another_fb, DRM_MODE_PAGE_FLIP_EVENT, data);
	if (ret)
		error(EXIT_FAILURE, errno, "Can not page flip = %d", errno);

	data->fb_id[0] = data->fb;
	data->fb_id[1] = another_fb;
	data->current_fb_id = another_fb;

	if (pthread_create(&thread, NULL, pthread_worker, data))
		error(EXIT_FAILURE, errno, "PThread creation failed");
}

static void create_fb(struct drmdisplay *data, int width, int height, int bpp, int fd)
{
	int ret;
	uint32_t handle;

	ret = drmPrimeFDToHandle(data->fd, fd, &handle);
	if (ret < 0)
		error(EXIT_FAILURE, errno, "Can not import dmabuf");

	data->fb_size = height * width * bpp;
	uint32_t pitch = width * bpp;
	ret = drmModeAddFB(data->fd, width, height, 24, 32, pitch, handle, &data->fb);
	if (ret)
		error(EXIT_FAILURE, errno, "Can not create framebuffer via drmModeAddFB()");
}

static void print_resources(drmModeResPtr res)
{
	printf("\nResources:\n");
	printf("min_width:        %d\n", res->min_width);
	printf("max_width:        %d\n", res->max_width);
	printf("min_height:       %d\n", res->min_height);
	printf("max_height:       %d\n", res->max_height);
	printf("count_connectors: %d\n", res->count_connectors);
	for (int i = 0; i < res->count_connectors; i++)
		printf("  [%d]: %d\n", i, res->connectors[i]);

	printf("count_encoders:   %d\n", res->count_encoders);
	for (int i = 0; i < res->count_encoders; i++)
		printf("  [%d]: %d\n", i, res->encoders[i]);

	printf("count_crtcs:      %d\n", res->count_crtcs);
	for (int i = 0; i < res->count_crtcs; i++)
		printf("  [%d]: %d\n", i, res->crtcs[i]);

	printf("count_fbs:        %d\n", res->count_fbs);
	for (int i = 0; i < res->count_fbs; i++)
		printf("  [%d]: %d\n", i, res->fbs[i]);
}

static void print_connector(drmModeConnectorPtr conn)
{
	const char *mode_connectors[] = {
		"Unknown",
		"VGA",
		"DVII",
		"DVID",
		"DVIA",
		"Composite",
		"SVIDEO",
		"LVDS",
		"Component",
		"9PinDIN",
		"DisplayPort",
		"HDMIA",
		"HDMIB",
		"TV",
		"eDP",
		"VIRTUAL",
		"DSI",
		"DPI",
	};
	const char *mode_connections[] = {
		"DRM_MODE_CONNECTED",
		"DRM_MODE_DISCONNECTED",
		"DRM_MODE_UNKNOWNCONNECTION",
	};
	const char *mode_subpixels[] = {
		"DRM_MODE_SUBPIXEL_UNKNOWN",
		"DRM_MODE_SUBPIXEL_HORIZONTAL_RGB",
		"DRM_MODE_SUBPIXEL_HORIZONTAL_BGR",
		"DRM_MODE_SUBPIXEL_VERTICAL_RGB",
		"DRM_MODE_SUBPIXEL_VERTICAL_BGR",
		"DRM_MODE_SUBPIXEL_NONE",
	};
	const char *mode_connector = (conn->connector_type <= 17) ?
			mode_connectors[conn->connector_type] : "?";
	const char *mode_connection = (conn->connection <= 3) ?
			mode_connections[conn->connection - 1] : "?";
	const char *mode_subpixel = (conn->subpixel <= 6) ?
			mode_subpixels[conn->subpixel - 1] : "?";

	printf("\nConnector:\n");
	printf("connector_id:      %d\n", conn->connector_id);
	printf("encoder_id:        %d\n", conn->encoder_id);
	printf("connector_type:    %d (%s)\n", conn->connector_type, mode_connector);
	printf("connector_type_id: %d\n", conn->connector_type_id);
	printf("connection:        %d (%s)\n", conn->connection, mode_connection);
	printf("mmWidth:           %d\n", conn->mmWidth);
	printf("mmHeight:          %d\n", conn->mmHeight);
	printf("subpixel:          %d (%s)\n", conn->subpixel, mode_subpixel);
	printf("count_encoders:    %d\n", conn->count_encoders);
	for (int i = 0; i < conn->count_encoders; i++)
		printf("  [%d]: %d\n", i, conn->encoders[i]);

	printf("count_modes:       %d\n", conn->count_modes);
	for (int i = 0; i < conn->count_modes; i++)
		printf("  [%d]: %s (%dx%d-%d)\n", i, conn->modes[i].name, conn->modes[i].hdisplay,
		       conn->modes[i].vdisplay, conn->modes[i].vrefresh);

	printf("count_props:       %d\n", conn->count_props);
	for (int i = 0; i < conn->count_props; i++)
		printf("  [%d]: prop: %#x, value: %#" PRIx64 "\n", i, conn->props[i],
		       conn->prop_values[i]);
}

static void print_encoder(drmModeEncoderPtr enc)
{
	printf("\nEncoder:\n");
	printf("encoder_id:      %d\n", enc->encoder_id);
	printf("encoder_type:    %d\n", enc->encoder_type);
	printf("crtc_id:         %d\n", enc->crtc_id);
	printf("possible_crtcs:  %d\n", enc->possible_crtcs);
	printf("possible_clones: %d\n", enc->possible_clones);
}

static void use_drm_connector(struct drmdisplay *data, drmModeResPtr resources, uint32_t connector_id,
			      bool verbose)
{
	uint32_t encoder_id;
	drmModeEncoderPtr enc;
	drmModeCrtcPtr crtc;
	drmModeConnectorPtr conn;

	if (verbose)
		print_resources(resources);
	if (!resources->count_connectors)
		error(EXIT_FAILURE, 0, "Can not find proper DRM connector");

	if (connector_id == (uint32_t)-1)
		connector_id = resources->connectors[0];

	conn = drmModeGetConnector(data->fd, connector_id);
	if (!conn)
		error(EXIT_FAILURE, errno, "Can not get DRM connector %d", connector_id);

	if (verbose)
		print_connector(conn);

	data->count_modes = conn->count_modes;
	data->modes = (drmModeModeInfo *)malloc(sizeof(drmModeModeInfo) * conn->count_modes);
	for (int i = 0; i < conn->count_modes; i++)
		data->modes[i] = conn->modes[i];

	encoder_id = conn->encoder_id;

	// If current encoder is not set then select first available encoder for this connector
	if (!encoder_id && conn->count_encoders)
		encoder_id = conn->encoders[0];

	if (verbose)
		printf("\nUsing encoder_id: %u\n", encoder_id);

	drmModeFreeConnector(conn);

	enc = drmModeGetEncoder(data->fd, encoder_id);
	if (!enc)
		error(EXIT_FAILURE, errno, "Can not get DRM encoder for connector %d", connector_id);

	if (verbose)
		print_encoder(enc);

	data->crtc_id = enc->crtc_id;

	// If current CRTC is not set then select first available CRTC
	if (!data->crtc_id && resources->count_crtcs)
		data->crtc_id = resources->crtcs[0];

	if (verbose)
		printf("\nUsing crtc_id: %u\n", data->crtc_id);

	drmModeFreeEncoder(enc);
	crtc = drmModeGetCrtc(data->fd, data->crtc_id);
	if (!crtc)
		error(EXIT_FAILURE, errno, "Can not get DRM crtc for encoder %d", encoder_id);

	data->conn_id = connector_id;
	data->old_fb = crtc->buffer_id;
	data->old_mode = crtc->mode;
	drmModeFreeCrtc(crtc);
}

int drmdisplay_init(struct drmdisplay *data, int connector_id, bool verbose)
{
	drmModeResPtr resources;

	memset(data, 0, sizeof(*data));
	data->fd = open("/dev/dri/card0", O_RDWR);
	if (data->fd == -1) {
		error(0, errno, "Can not open video driver");
		return -1;
	}

	resources = drmModeGetResources(data->fd);
	if (!resources)
		error(EXIT_FAILURE, errno, "Can not get DRM resources");

	use_drm_connector(data, resources, (uint32_t)connector_id, verbose);
	drmModeFreeResources(resources);

	return 0;
}

int drmdisplay_fill_mode(struct drmdisplay *data, int *width, int *height)
{
	if (*width && *height)
		return 0;  // Nothing to fill

	// if no one field is filled
	if (!*width && !*height) {
		*width = data->old_mode.hdisplay;
		*height = data->old_mode.vdisplay;
		return 0;
	}

	// if some fields are filled then filling another fields
	for (int i = 0; i < data->count_modes; i++) {
		if ((!*width || (data->modes[i].hdisplay == *width)) &&
		    (!*height || (data->modes[i].vdisplay == *height))) {
			*width = data->modes[i].hdisplay;
			*height = data->modes[i].vdisplay;
			return 0;
		}
	}

	// no suitable modes found
	return -1;
}

void drmdisplay_set_mode(struct drmdisplay *data, int width, int height, int bpp, int fd)
{
	drmModeModeInfo mode = {0};

	for (int i = 0; i < data->count_modes; i++) {
		if ((data->modes[i].hdisplay == width) && (data->modes[i].vdisplay == height)) {
			mode = data->modes[i];
			break;
		}
	}
	if (mode.hdisplay == 0)
		error(EXIT_FAILURE, 0, "Resolution %dx%d is not supported by display", width, height);

	create_fb(data, width, height, bpp, fd);

	printf("Setting resolution %s %d Hz\n", mode.name, mode.vrefresh);
	if (drmModeSetCrtc(data->fd, data->crtc_id, data->fb, 0, 0, &data->conn_id, 1, &mode))
		error(EXIT_FAILURE, errno, "Can not change mode for DRM crtc %d", data->crtc_id);
}

int drmdisplay_restore_mode(struct drmdisplay *data)
{
	int err = 0;

	if (data->fb) {
		printf("Restore resolution %s %d Hz\n",
		       data->old_mode.name, data->old_mode.vrefresh);
		if (drmModeSetCrtc(data->fd, data->crtc_id, data->old_fb, 0, 0, &data->conn_id, 1,
				   &data->old_mode)) {
			error(0, errno, "Can not restore mode for DRM");
			err = -1;
		}

		if (drmModeRmFB(data->fd, data->fb)) {
			error(0, errno, "Can not remove framebuffer1");
			err = -1;
		}
	}

	if (data->fb_id[1]) {
		if (drmModeRmFB(data->fd, data->fb_id[1])) {
			error(0, errno, "Can not remove framebuffer2");
			err = -1;
		}
	}

	if (close(data->fd)) {
		error(0, errno, "Error at close DRM driver");
		err = -1;
	}

	if (data->count_modes) {
		data->count_modes = 0;
		free(data->modes);
	}

	return err;
}
