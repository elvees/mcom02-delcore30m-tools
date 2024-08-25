/*
 * \file
 * \brief delcore30m-inversiontest - Inversion image on DSP
 *
 * \copyright
 * Copyright 2018-2019 RnD Center "ELVEES", JSC
 *
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <error.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <poll.h>
#include <sys/time.h>
#include <linux/types.h>
#include <asm/types.h>

#include <linux/delcore30m.h>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

#include "delcore30m-inversiontest.h"

#define MAKE_STR_(s) #s
#define MAKE_STR(s) MAKE_STR_(s)

/* Some evident defines */
#define MSEC_IN_SEC 1000
#define USEC_IN_SEC 1000000
#define NSEC_IN_SEC 1000000000

const int tile_width = 288;
const int tile_height = 64;
const int SDMA_CHANNELS_COUNT = 2;
int img_width, img_height, img_channels;

struct timespec job_begin, job_end, test_begin, test_end;
double elapsedTime_job, elapsedTime_test;

uint8_t profile = 0;

char *fw_path = MAKE_STR(FIRMWARE_PATH) "inversiontest.fw.bin";

bool passed = false;

void printresult(void)
{
	puts(passed ? "TEST PASSED" : "TEST FAILED");
}

void help(const char *pname)
{
	printf("Usage: %s -i image_file [options]\n\n", pname);
	puts("Options:");
	puts("    -o arg\tSet saving result in file");
	puts("    -p arg\tSet profiling mode for DSP");
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

static inline float timespec2msec(struct timespec const t)
{
	return (float)t.tv_sec * MSEC_IN_SEC + (float)t.tv_nsec / (NSEC_IN_SEC / MSEC_IN_SEC);
}

void printtimings(int core)
{
	struct timespec elapsed;

	clock_gettime(CLOCK_MONOTONIC, &test_end);
	elapsed = timespec_subtract(job_begin, job_end);
	fprintf(stdout, "JOB<CORE %d> runtime = %f ms\n", core, timespec2msec(elapsed));

	elapsed = timespec_subtract(test_begin, test_end);
	fprintf(stdout, "TEST runtime = %f ms\n", timespec2msec(elapsed));
}

unsigned int tiles_get_number (int img_width, int img_height, int tile_width, int tile_height)
{
	return DIV_ROUND_UP(img_width, tile_width) * DIV_ROUND_UP(img_height, tile_height);
}

struct tilesbuffer *tile_generator(int width, int height, int channels)
{
	int ntiles = tiles_get_number(width, height, tile_width, tile_height);
	struct tilesbuffer *tb = malloc(sizeof(struct tileinfo) * ntiles + sizeof(struct tilesbuffer));
	int i = 0;

	if (!tb)
		return NULL;

	tb->ntiles = ntiles;
	for (uint32_t y = 0; y < height; y+=tile_height)
		for (uint32_t x = 0; x < width; x+=tile_width)
			tb->info[i++] = (struct tileinfo){
				.x = x,
				.y = y,
				.width = min(tile_width, width - x),
				.height = min(tile_height, height - y),
				.stride = { channels, channels * width }
			};

	return tb;
}

struct sdma_descriptor tile2descriptor(struct tileinfo tile)
{
	struct sdma_descriptor const desc = {
		.a0e = tile.x * tile.stride[0] + tile.y * tile.stride[1],
		.astride = img_width * tile.stride[0],
		.bcnt = tile.height,
		.asize = tile.width * tile.stride[0],
		.ccr = BURST_SIZE_8BYTE << SCR_BURST_SIZE_BIT |
		       BURST_SIZE_8BYTE << DST_BURST_SIZE_BIT |
		       AUTO_INCREMENT << SRC_AUTO_INCREMENT_BIT |
		       AUTO_INCREMENT << DST_AUTO_INCREMENT_BIT
	};
	return desc;
}

uint8_t *getbytes(const char *filename, uint32_t *size)
{
	uint8_t *data;

	FILE *f = fopen(filename, "r");
	if (f == NULL)
		error(EXIT_FAILURE, errno, "Failed to open firmware");
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);

	data = malloc(*size);
	fread(data, 1, *size, f);

	fclose(f);

	return data;
}

struct delcore30m_buffer *buf_alloc(int fd, enum delcore30m_memory_type type,
		int core_num, int size, void *ptr)
{
	struct delcore30m_buffer *buffer = (struct delcore30m_buffer *) malloc(sizeof(struct delcore30m_buffer));
	buffer->size = size;
	buffer->type = type;
	buffer->core_num = core_num;

	if (ioctl(fd, ELCIOC_BUF_ALLOC, buffer))
		error(EXIT_FAILURE, errno, "Failed to allocate buffer");

	if (ptr != NULL) {
		void *mmap_buf = mmap(NULL, buffer->size,
				      PROT_READ | PROT_WRITE, MAP_SHARED,
				      buffer->fd, 0);
		if (mmap_buf == MAP_FAILED)
			error(EXIT_FAILURE, errno, "Failed to mmap buffer");
		memcpy(mmap_buf, ptr, size);
		munmap(mmap_buf, buffer->size);
	}
	return buffer;
}

void results_check(uint64_t *img1, uint64_t *img2, size_t size)
{
	for (int i = 0; i < size / 8; ++i) {
		img1[i] ^= UINT64_MAX;
		if (img1[i] != img2[i])
			error(EXIT_FAILURE, 0, "Result pixels incorrect");
	}
}

void job_start(int fd, struct delcore30m_job *job)
{
	clock_gettime(CLOCK_MONOTONIC, &job_begin);
	if (ioctl(fd, ELCIOC_JOB_ENQUEUE, job))
		error(EXIT_FAILURE, errno, "Failed to enqueue job");

	struct pollfd fds = {
		.fd = job->fd,
		.events = POLLIN | POLLPRI | POLLOUT | POLLHUP
	};
	poll(&fds, 1, 2000);
	clock_gettime(CLOCK_MONOTONIC, &job_end);

	if (ioctl(fd, ELCIOC_JOB_STATUS, job))
		error(EXIT_FAILURE, errno, "Failed to get job status");

	if (job->status != DELCORE30M_JOB_IDLE) {
		if (ioctl(fd, ELCIOC_JOB_CANCEL, job))
			error(EXIT_FAILURE, errno, "Failed to cancel job");
		fprintf(stderr, "Job timed out\n");
	}

	if (job->rc != DELCORE30M_JOB_SUCCESS)
		error(EXIT_FAILURE, 0, "Job failed");
}

void job_create(int fd, struct delcore30m_job *job,
		int *in_buffers, int in_size,
		int *out_buffers, int out_size,
		int cores_fd, int sdmas_fd)
{
	job->inum = in_size;
	job->onum = out_size;
	job->cores_fd = cores_fd;
	job->sdmas_fd = sdmas_fd;
	job->flags = profile ? DELCORE30M_PROFILE : 0;

	for (int i = 0; i < in_size; ++i)
		job->input[i] = in_buffers[i];
	for (int i = 0; i < out_size; ++i)
		job->output[i] = out_buffers[i];

	if (ioctl(fd, ELCIOC_JOB_CREATE, job))
		error(EXIT_FAILURE, errno, "Failed to create job");
}

int main(int argc, char **argv)
{
	int opt, fd;
	uint8_t core_id;
	char *image_input_path = NULL, *image_output_path = NULL;

	clock_gettime(CLOCK_MONOTONIC, &test_begin);
	atexit(printresult);

	while ((opt = getopt(argc, argv, "i:o:ph")) != -1) {
		switch (opt) {
		case 'i':
			image_input_path = optarg;
			break;
		case 'o':
			image_output_path = optarg;
			break;
		case 'p':
			fw_path = MAKE_STR(FIRMWARE_PATH) "inversiontest-profile.fw.bin";
			profile = 1;
			break;
		case 'h':
			help(argv[0]);
			return EXIT_SUCCESS;
		default:
			error(EXIT_FAILURE, 0, "Try %s -h for help.", argv[0]);
		}
	}

	if (image_input_path == NULL)
		error(EXIT_FAILURE, 0, "Not enough arguments");

	fd = open("/dev/elcore0", O_RDWR);
	if (fd < 0)
		error(EXIT_FAILURE, errno, "Failed to open device file");

	struct delcore30m_resource cores_res = {
		.type = DELCORE30M_CORE,
		.num = 1
	};

	if (ioctl(fd, ELCIOC_RESOURCE_REQUEST, &cores_res))
		error(EXIT_FAILURE, errno, "Failed to request DELCORE30M_CORE");

	uint32_t fwsize;
	uint8_t *fwdata = getbytes(fw_path, &fwsize);
	uint8_t mask = cores_res.mask;
	for (core_id = 0; mask; core_id++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		void *buf_cores = mmap(NULL, fwsize, PROT_WRITE,
				       MAP_SHARED, cores_res.fd,
				       sysconf(_SC_PAGESIZE) * core_id);
		if (buf_cores == MAP_FAILED)
			error(EXIT_FAILURE, errno, "Failed to load firmware");
		memcpy(buf_cores, fwdata, fwsize);
		munmap(buf_cores, fwsize);
	}
	core_id--;

	struct delcore30m_resource sdmas_res = {
		.type = DELCORE30M_SDMA,
		.num = SDMA_CHANNELS_COUNT
	};

	if (ioctl(fd, ELCIOC_RESOURCE_REQUEST, &sdmas_res))
		error(EXIT_FAILURE, errno, "Failed to request DELCORE30M_SDMA");

	uint32_t channels[SDMA_CHANNELS_COUNT];
	mask = sdmas_res.mask;
	for (int channel = 0, k = 0; mask; channel++, mask >>= 1) {
		if (mask & 1)
			channels[k++] = channel;
	}

	void *image = stbi_load(image_input_path, &img_width,
				&img_height, &img_channels, 0);
	if (image == NULL)
		error(EXIT_FAILURE, 0, "Failed to open image %s: %s", image_input_path,
		      stbi_failure_reason());

	int img_size = img_width * img_height * img_channels;

	struct delcore30m_buffer *img_buffer = buf_alloc(fd,
			DELCORE30M_MEMORY_SYSTEM, core_id, img_size, image);

	struct tilesbuffer *tb = tile_generator(img_width, img_height, img_channels);
	tb->tilesize = tile_width * tile_height * img_channels;
	struct sdma_descriptor descs[tb->ntiles];

	for (uint32_t i = 0; i < tb->ntiles; ++i) {
		descs[i] = tile2descriptor(tb->info[i]);
		descs[i].a_init = (i + 1) * sizeof(struct sdma_descriptor);
	}
	descs[tb->ntiles - 1].a_init = 0;

	struct tilesbuffer *tb_out = tile_generator(img_width, img_height, img_channels);
	tb_out->tilesize = tb->tilesize;
	struct sdma_descriptor descs_out[tb_out->ntiles];

	for (uint32_t i = 0; i < tb_out->ntiles; ++i) {
		descs_out[i] = tile2descriptor(tb_out->info[i]);
		descs_out[i].a_init = (i + 1) * sizeof(struct sdma_descriptor);
	}
	descs_out[tb->ntiles - 1].a_init = 0;

	struct delcore30m_buffer *chain_buffer1 = buf_alloc(fd,
			DELCORE30M_MEMORY_SYSTEM, core_id,
			sizeof(struct sdma_descriptor) * tb->ntiles, descs_out);

	struct delcore30m_buffer *chain_buffer2 = buf_alloc(fd,
			DELCORE30M_MEMORY_SYSTEM, core_id,
			sizeof(struct sdma_descriptor) * tb->ntiles, descs);

	struct delcore30m_buffer *tb_buffer = buf_alloc(fd,
			DELCORE30M_MEMORY_XYRAM, core_id,
			sizeof(struct tilesbuffer) + sizeof(struct tileinfo) * tb->ntiles,
			tb);

	struct delcore30m_buffer *tile_buffers[2];
	for (int i = 0; i < 2; ++i)
		tile_buffers[i] = buf_alloc(fd, DELCORE30M_MEMORY_XYRAM, core_id,
					    tile_width * tile_height * img_channels,
					    NULL);

	struct delcore30m_buffer *out_img_buffer = buf_alloc(fd,
			DELCORE30M_MEMORY_SYSTEM, core_id, img_size, NULL);

	struct delcore30m_buffer *code_buffer1 = buf_alloc(fd,
			DELCORE30M_MEMORY_SYSTEM, core_id, 60 * tb->ntiles, NULL);

	struct delcore30m_buffer *channel_buffer = buf_alloc(fd,
				DELCORE30M_MEMORY_XYRAM, core_id,
				SDMA_CHANNELS_COUNT * sizeof(uint32_t), channels);

	struct delcore30m_buffer *code_buffer2 = buf_alloc(fd,
				DELCORE30M_MEMORY_SYSTEM, core_id, 60 * tb->ntiles, NULL);

	struct delcore30m_job job;
	int input[] = {img_buffer->fd,
			tile_buffers[0]->fd, channel_buffer->fd, tile_buffers[1]->fd,
			chain_buffer1->fd, chain_buffer2->fd, tb_buffer->fd};
	int output[] = {out_img_buffer->fd, code_buffer1->fd, code_buffer2->fd};
	job_create(fd, &job, input, 7, output, 3, cores_res.fd, sdmas_res.fd);

	struct delcore30m_dmachain dmachain_input = {
		.job = job.fd,
		.core = core_id,
		.external = img_buffer->fd,
		.internal = { tile_buffers[0]->fd, tile_buffers[1]->fd },
		.chain = chain_buffer1->fd,
		.codebuf = code_buffer1->fd,
		.channel = {SDMA_CHANNEL_INPUT,  channels[0]}
	};
	if (ioctl(fd, ELCIOC_DMACHAIN_SETUP, &dmachain_input))
		error(EXIT_FAILURE, errno, "Failed to setup input dmachain");

	struct delcore30m_dmachain dmachain_output = {
		.job = job.fd,
		.core = core_id,
		.external = out_img_buffer->fd,
		.internal = { tile_buffers[0]->fd, tile_buffers[1]->fd},
		.chain = chain_buffer2->fd,
		.codebuf = code_buffer2->fd,
		.channel = {SDMA_CHANNEL_OUTPUT,  channels[1]}
	};
	if (ioctl(fd, ELCIOC_DMACHAIN_SETUP, &dmachain_output))
		error(EXIT_FAILURE, errno, "Failed to setup output dmachain");

	job_start(fd, &job);

	void *img_out = mmap(NULL, out_img_buffer->size,
			     PROT_READ | PROT_WRITE, MAP_SHARED,
			     out_img_buffer->fd, 0);
	if (img_out == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to mmap output buffer");
	void* output_img = malloc(img_size);
	memcpy(output_img, img_out, img_size);
	munmap(img_out, out_img_buffer->size);

	results_check(image, output_img, img_size);

	if (image_output_path) {
		if (!stbi_write_png(image_output_path, img_width, img_height, img_channels,
				   output_img, img_width * img_channels))
			error(EXIT_FAILURE, 0, "Failed to write image");
	}

	printtimings(core_id);
	stbi_image_free(image);
	passed = true;
	return EXIT_SUCCESS;
}
