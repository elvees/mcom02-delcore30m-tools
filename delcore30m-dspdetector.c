/*
 * \file
 * \brief dspdetector - motion detection on DSP from
 * video from V4L2 devices and copy result frames to framebuffer
 *
 * \copyright
 * Copyright 2019 RnD Center "ELVEES", JSC
 */

/* Need for sigaction() */
#define _XOPEN_SOURCE 600

#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "drmdisplay.h"
#include "dspdetector.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stbfont.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

#define MAX_IFACE 4
#define MAX_LEN 64
#define DEFAULT_OUTFILE "/dev/fb0"

#define MAX_BUFFERS_COUNT 2

#define NSEC_IN_SEC 1000000000

pthread_t thread;
pthread_cond_t cv;
pthread_mutex_t lock;

uint32_t frames = 0;

float fps, cpu_usage;

struct termios stored_settings;

struct arguments {
	uint8_t iface;
	char *outfile;
	int width;
	int height;
	int connector_id;
	bool verbose;
};

bool stop;

void set_format(int fd, uint32_t pixelformat, uint32_t width, uint32_t height)
{
	struct v4l2_format format = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix = {
			.width = width,
			.height = height,
			.pixelformat = pixelformat
		}
	};
	if (ioctl(fd, VIDIOC_S_FMT, &format) == -1)
		error(EXIT_FAILURE, errno, "ioctl error VIDIOC_S_FMT");
	if (format.fmt.pix.width != width || format.fmt.pix.height != height)
		printf("Requested frame size %ux%u, but VINC will use %ux%u\n",
		   width, height, format.fmt.pix.width, format.fmt.pix.height);
}

void request_buffers(int fd, uint32_t *count)
{
	struct v4l2_requestbuffers req = {
		.count = *count,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP
	};
	if (ioctl(fd, VIDIOC_REQBUFS, &req) == -1)
		error(EXIT_FAILURE, errno, "ioctl error VIDIOC_REQBUFS");
	*count = req.count;
}

void export_buffers(int const fd, uint32_t const num, int bufs[])
{
	for (int i = 0; i < num; ++i) {
		struct v4l2_exportbuffer ebuf = {
			.index = i,
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE
		};

		if (ioctl(fd, VIDIOC_EXPBUF, &ebuf) == -1)
			error(EXIT_FAILURE, errno, "ioctl error VIDIOC_EXPBUF");

		bufs[i] = ebuf.fd;
	}
}

void stream_on(int fd)
{
	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	if (ioctl(fd, VIDIOC_STREAMON, &type) == -1)
		error(EXIT_FAILURE, errno, "ioctl error VIDIOC_STREAMON");
}

void qbuf(int fd, uint32_t index, struct v4l2_buffer *buf)
{
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = index;
	if (ioctl(fd, VIDIOC_QBUF, buf) == -1)
		error(EXIT_FAILURE, errno, "ioctl error VIDIOC_QBUF");
}

void dqbuf(int fd, uint32_t index, struct v4l2_buffer *buf)
{
	buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf->memory = V4L2_MEMORY_MMAP;
	buf->index = index;
	if (ioctl(fd, VIDIOC_DQBUF, buf) == -1)
		error(EXIT_FAILURE, errno, "ioctl error VIDIOC_DQBUF");
}

static void print_usage(void)
{
	puts("Capture video from V4L2 device, detect moving objects on DSP and write to the framebuffer.\n");
	puts("Usage: delcore30m-dspdetector [options]");
	puts("   -i <iface>\tsensor interface number (0,1 - parallel; 2,3 - serial)");
	puts("   -o <file>\tframebuffer device (default: " DEFAULT_OUTFILE ")");
	puts("   -w <width>\twidth of frame (default: autodetect from framebuffer)");
	puts("   -h <height>\theight of frame (default: autodetect from framebuffer)");
	puts("   -c <id>\tconnector ID (for DRM mode only) (default: first available connector)");
	puts("   -v\t\tprint additional information");

        printf("\nBy default, performance metrics are rendered on the frame with %s.\n",
               DEFAULT_FONT_PATH);
        puts("You can redefine font by DELCORE30M_FONT_PATH environment variable.");
}

void signal_handler(int sig)
{
	stop = true;
}

static int find_device_on_interface_number(uint8_t iface)
{
	int fd;

	struct v4l2_streamparm sparm = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
	};
	char infile[MAX_LEN];

	for (uint8_t i = 0; i < MAX_IFACE; i++) {
		snprintf(infile, MAX_LEN, "/dev/v4l/by-path/platform-37200000.vinc-video-index%d",
			 i);
		fd = open(infile, O_RDWR);
		if (fd < 0)
			continue;
		if (ioctl(fd, VIDIOC_G_PARM, &sparm) == -1) {
			close(fd);
			continue;
		}
		if (sparm.parm.capture.extendedmode == iface)
			break;
		else
			close(fd);
	}

	if (sparm.parm.capture.extendedmode != iface)
		return -1;

	return fd;
}

static inline struct timespec timespec_subtract(struct timespec const start,
					 struct timespec const stop)
{
	struct timespec res = {
		.tv_sec = stop.tv_sec - start.tv_sec,
		.tv_nsec = stop.tv_nsec - start.tv_nsec
	};

	if (res.tv_nsec < 0) {
		res.tv_sec -= 1;
		res.tv_nsec += NSEC_IN_SEC;
	}

	return res;
}

static inline float timespec2float(struct timespec const t)
{
	return t.tv_sec + (float)t.tv_nsec / 1e9;
}

static void get_cpu_usage()
{
	static uint32_t total, total_prev;
	static uint32_t idle, idle_prev;
	FILE *f;
	uint32_t data[7];
	char tmp[255];

	total_prev = total;
	idle_prev = idle;
	f = fopen("/proc/stat", "rb");
	fscanf(f, "%3s %u %u %u %u %u %u %u", tmp, &data[0], &data[1], &data[2], &data[3],
	       &data[4], &data[5], &data[6]);
	fclose(f);
	total = data[0] + data[1] + data[2] + data[3] + data[4] + data[5] + data[6];
	idle = data[3];

	cpu_usage = 100.0 * (1 - ((idle - idle_prev) * 1.0) / (total - total_prev));
}

static void get_fps()
{
	static struct timespec start, end;
	start = end;
	clock_gettime(CLOCK_MONOTONIC, &end);

	struct timespec elapsed = timespec_subtract(start, end);
	fps = frames / timespec2float(elapsed);

	frames = 0;
}

void reset_keypress(void)
{
	tcsetattr(0, TCSANOW, &stored_settings);
	return;
}

void set_keypress(void)
{
	struct termios new_settings;

	tcgetattr(0,&stored_settings);

	new_settings = stored_settings;
	new_settings.c_lflag &= (~ICANON);
	new_settings.c_lflag &= (~ECHO);
	new_settings.c_cc[VTIME] = 0;
	new_settings.c_cc[VMIN] = 1;

	tcsetattr(0,TCSANOW,&new_settings);
	return;
}

int main(int argc, char *argv[])
{
	struct v4l2_buffer buf = { .type = V4L2_BUF_TYPE_VIDEO_CAPTURE };
	int fd, opt;
	uint32_t buffer_count = MAX_BUFFERS_COUNT;
	uint32_t buffer_size;
	struct drmdisplay data_drm;
	struct dsp_struct dsp_data;
	struct frame_args frame_data;
	struct fontData font_data = {0};
	//!< Exported file descriptors of input buffers
	int inbufs[MAX_BUFFERS_COUNT];

	struct arguments arguments = {
		.iface = MAX_IFACE,
		.outfile = DEFAULT_OUTFILE,
		.width = 0,
		.height = 0,
		.connector_id = -1,
		.verbose = false,
	};
	struct sigaction new_sigaction = {
		.sa_handler = signal_handler,
		.sa_flags = SA_RESTART,
	};

	while ((opt = getopt(argc, argv, "i:o:w:h:c:v")) != -1) {
		switch (opt) {
		case 'i':
			arguments.iface = atoi(optarg);
			break;
		case 'o':
			arguments.outfile = optarg;
			break;
		case 'w':
			arguments.width = atoi(optarg);
			break;
		case 'h':
			arguments.height = atoi(optarg);
			break;
		case 'c':
			arguments.connector_id = atoi(optarg);
			break;
		case 'v':
			arguments.verbose = true;
			break;
		default:
			print_usage();
			return EXIT_FAILURE;
		}
	}

	if (arguments.iface >= MAX_IFACE) {
		print_usage();
		return EXIT_FAILURE;
	}

	sigaction(SIGINT, &new_sigaction, NULL);
	printf("Opening device with sensor interface %d\n", arguments.iface);

	fd = find_device_on_interface_number(arguments.iface);
	if (fd < 0)
		error(EXIT_FAILURE, errno, "Can not open device with interface %d",
		      arguments.iface);

	if (drmdisplay_init(&data_drm, arguments.connector_id, arguments.verbose))
		error(EXIT_FAILURE, errno, "DRM initialize failed");

	if (drmdisplay_fill_mode(&data_drm, &arguments.width, &arguments.height))
		printf("No suitable display modes found\n");

	frame_data = (struct frame_args) {
		.frame_width = arguments.width,
		.frame_height = arguments.height,
		.tile_width = 288,
		.tile_height = 48,
		.pixel_format = PIXEL_FORMAT_RGBA
	};

	buffer_size = frame_data.frame_width * frame_data.frame_height * frame_data.pixel_format;

	dsp_init(&dsp_data, frame_data);

	set_format(fd, V4L2_PIX_FMT_BGR32, arguments.width, arguments.height);
	request_buffers(fd, &buffer_count);
	export_buffers(fd, buffer_count, &inbufs[0]);

	for (uint32_t i = 0; i < buffer_count; i++) {
		buf.memory = V4L2_MEMORY_MMAP;
		buf.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &buf) == -1)
			error(EXIT_FAILURE, errno, "ioctl VIDIOC_QUERYBUF error");
		if (!buf.length)
			error(EXIT_FAILURE, 0, "Buffer #%d is empty\n", i);
		else if (buf.length > buffer_size)
			error(EXIT_FAILURE, 0, "Buffer #%d is too big\n", i);
	}
	for (uint32_t i = 0; i < buffer_count; i++)
		qbuf(fd, i, &buf);

	dsp_job_create(&dsp_data, inbufs, buffer_count);

	init_font(&font_data, arguments.height / 12);

	drmdisplay_set_mode(&data_drm, arguments.width, arguments.height, frame_data.pixel_format,
			    dsp_data.result_frame[0]->fd);
	drmdisplay_start_flipflop(&data_drm, arguments.width, arguments.height,
				  frame_data.pixel_format, dsp_data.result_frame[1]->fd);
	stream_on(fd);

	set_keypress();
	int pid = fork();
	if (pid == 0) {
		while (1) {
			if (getchar() == 'u') {
					dsp_data.dsp_global_data->avering_counter = 0;
					dsp_data.dsp_global_data->flag_avered = 0;
			}
			usleep(30 * 1000);
		}
	}

	uint32_t buffer_id = 0;
	while (!stop) {
		if (frames % 30 == 0) {
			get_fps();
			get_cpu_usage();
		}
		dqbuf(fd, buffer_id, &buf);

		// TODO: Input and output buffers with different stride are not supported.
		if (frame_detector(&dsp_data, inbufs[buffer_id], buffer_id)) {
			qbuf(fd, buffer_id, &buf);
			break;
		}

		char str[255];
		sprintf(str, "CPU: %.1f%%, %.1f FPS", cpu_usage, fps);
		draw_string(&font_data, dsp_data.result_frame_data[buffer_id],
			    frame_data.frame_width * frame_data.pixel_format,
			    str, 0);
		/* send message to flip page handler */
		pthread_cond_signal(&cv);

		frames++;

		qbuf(fd, buffer_id, &buf);

		buffer_id++;
		if (buffer_id >= buffer_count)
			buffer_id = 0;
	}
	drmdisplay_restore_mode(&data_drm);

	dsp_free(&dsp_data);

	close(fd);

	pthread_cancel(thread);
	kill(pid, SIGUSR1);
	reset_keypress();

	return EXIT_SUCCESS;
}
