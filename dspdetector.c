/*
 * Copyright 2019 RnD Center "ELVEES", JSC
 */
#include <error.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dspdetector.h"

const int sdma_burst_size = 8;

#define MAKE_STR_(s) #s
#define MAKE_STR(s) MAKE_STR_(s)

static unsigned int tiles_get_number (const struct frame_args frame_data)
{
	return DIV_ROUND_UP(frame_data.frame_width, frame_data.tile_width) * DIV_ROUND_UP(frame_data.frame_height, frame_data.tile_height);
}

static struct tilesbuffer *tile_generator(const struct frame_args frame_data)
{
	int ntiles = tiles_get_number(frame_data);
	struct tilesbuffer *tb = malloc(sizeof(struct tileinfo) * ntiles + sizeof(struct tilesbuffer));
	int i = 0;

	if (!tb)
		return NULL;

	tb->ntiles = ntiles;
	for (uint32_t y = 0; y < frame_data.frame_height; y+=frame_data.tile_height)
		for (uint32_t x = 0; x < frame_data.frame_width; x+=frame_data.tile_width)
			tb->info[i++] = (struct tileinfo){
				.x = x,
				.y = y,
				.width = min(frame_data.tile_width, frame_data.frame_width - x),
				.height = min(frame_data.tile_height, frame_data.frame_height - y),
				.stride = { frame_data.pixel_format, frame_data.pixel_format * frame_data.frame_width }
			};

	return tb;
}

static struct sdma_descriptor tile2descriptor(const struct frame_args frame_data,
					      struct tileinfo tile)
{
	struct sdma_descriptor const desc = {
			.a0e = tile.x * tile.stride[0] + tile.y * tile.stride[1],
			.astride = frame_data.frame_width * tile.stride[0],
			.bcnt = tile.height,
			.asize = tile.width * tile.stride[0],
			.ccr = BURST_SIZE_8BYTE << SCR_BURST_SIZE_BIT |
			       BURST_SIZE_8BYTE << DST_BURST_SIZE_BIT |
			       AUTO_INCREMENT << SRC_AUTO_INCREMENT_BIT |
			       AUTO_INCREMENT << DST_AUTO_INCREMENT_BIT
	};

	return desc;
}

static struct delcore30m_buffer *buf_alloc(int fd, enum delcore30m_memory_type type,
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

void *getbytes(const char *filename, uint32_t *size)
{
	void *data;

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

static void load_firmware(struct dsp_struct *data)
{
	uint32_t size;
	void *fw_data = getbytes(MAKE_STR(FIRMWARE_PATH) "detector.fw.bin", &size);
	void *buf_cores = mmap(NULL, size, PROT_WRITE, MAP_SHARED, data->core.fd,
			       sysconf(_SC_PAGESIZE) * (__builtin_ffs(data->core.mask) - 1));
	if (buf_cores == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to load firmware");
	memcpy(buf_cores, fw_data, size);
	munmap(buf_cores, size);
}

static void check_frame_args(const struct frame_args data)
{
	if (data.frame_width <= 0 || data.frame_height <= 0 ||
	    data.tile_width <= 0 || data.tile_height <= 0 ||
	    data.tile_width >= data.frame_width ||
	    data.tile_height >= data.frame_height)
		error(EXIT_FAILURE, 0, "Incorrect frame/tile sizes");
	if ((data.tile_width * data.pixel_format) % sdma_burst_size)
		error(EXIT_FAILURE, 0, "Tile width in bytes must be multiple of 8");
}

static void allocate_buffers(struct dsp_struct *data, const struct frame_args frame_data)
{
	uint8_t core_id = __builtin_ffsl(data->core.mask) - 1;
	size_t img_size = frame_data.frame_height * frame_data.frame_width * frame_data.pixel_format;
	size_t tile_size = frame_data.tile_height * frame_data.tile_width * frame_data.pixel_format;

	struct tilesbuffer *tb = tile_generator(frame_data);
	tb->tilesize = tile_size;

	struct sdma_descriptor descs[tb->ntiles];

	for (uint32_t i = 0; i < tb->ntiles; ++i) {
		descs[i] = tile2descriptor(frame_data, tb->info[i]);
		descs[i].a_init = (i + 1) * sizeof(struct sdma_descriptor);
	}
	descs[tb->ntiles - 1].a_init = 0;

	data->background = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM, 0,
				     img_size, NULL);

	uint8_t *byte_array = (uint8_t *)mmap(NULL, data->background->size,
					      PROT_READ | PROT_WRITE,
					      MAP_SHARED,
					      data->background->fd, 0);
	memset(byte_array, 255, img_size);

	for (int i = 0; i < 2; ++i) {
		data->tile_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_XYRAM,
						  0, tile_size, NULL);
		data->background_tile_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_XYRAM,
							     1, tile_size, NULL);
		data->background_chain_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM,
							      1,
							      sizeof(struct sdma_descriptor) * tb->ntiles,
							      descs);
		data->background_code_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM,
							     1, 60 * tb->ntiles, NULL);
		data->chain_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM,
						   core_id,
						   sizeof(struct sdma_descriptor) * tb->ntiles,
						   descs);
		data->code_buffers[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM,
						  core_id, 60 * tb->ntiles, NULL);
		data->result_frame[i] = buf_alloc(data->fd, DELCORE30M_MEMORY_SYSTEM,
						  core_id, img_size, NULL);
		data->result_frame_data[i] = mmap(NULL, data->result_frame[i]->size,
						  PROT_READ | PROT_WRITE, MAP_SHARED,
						  data->result_frame[i]->fd, 0);
	}
	data->tileinfo_buffer = buf_alloc(data->fd,
					  DELCORE30M_MEMORY_XYRAM,
					  core_id,
					  sizeof(struct tilesbuffer) + sizeof(struct tileinfo) * tb->ntiles,
					  tb);
	data->dsp_global_data_buffer = buf_alloc(data->fd,
						 DELCORE30M_MEMORY_XYRAM,
						 core_id, 2 * sizeof(uint32_t),
						 NULL);
	data->dsp_global_data = (struct dsp_struct_data *) mmap(NULL,
								data->dsp_global_data_buffer->size,
								PROT_READ | PROT_WRITE,
								MAP_SHARED,
								data->dsp_global_data_buffer->fd,
								0);

	data->dsp_global_data->avering_counter = 0;
	data->dsp_global_data->flag_avered = 0;
	for (uint8_t i = 0; i < 8; ++i)
		data->dsp_global_data->channels[i] = data->sdma_channels[i];
}

static void job_create(int fd, struct delcore30m_job *job,
		       int *in_buffers, int in_size,
		       int *out_buffers, int out_size,
		       int cores_fd, int sdmas_fd)
{
	job->inum = in_size;
	job->onum = out_size;
	job->cores_fd = cores_fd;
	job->sdmas_fd = sdmas_fd;
	job->flags = 0;

	for (int i = 0; i < in_size; ++i)
		job->input[i] = in_buffers[i];
	for (int i = 0; i < out_size; ++i)
		job->output[i] = out_buffers[i];

	if (ioctl(fd, ELCIOC_JOB_CREATE, job))
		error(EXIT_FAILURE, 0, "Failed to create job");
}

static void dsp_deinit(const struct dsp_struct *data)
{
	close(data->job.fd);
	close(data->dsp_global_data_buffer->fd);
	close(data->tileinfo_buffer->fd);
	for (int i = 0; i < 2; i++) {
		close(data->chain_buffers[i]->fd);
		close(data->code_buffers[i]->fd);
		close(data->tile_buffers[i]->fd);
		close(data->result_frame[i]->fd);
	}
	close(data->sdma.fd);
	close(data->core.fd);
	close(data->fd);
}

void dsp_init(struct dsp_struct *data, const struct frame_args frame_data)
{
	check_frame_args(frame_data);

	data->fd = open("/dev/elcore0", O_RDWR);
	if (data->fd < 0)
		error(EXIT_FAILURE, errno, "Failed to open device file");

	data->core.type = DELCORE30M_CORE;
	data->core.num = 1;

	if (ioctl(data->fd, ELCIOC_RESOURCE_REQUEST, &data->core))
		error(EXIT_FAILURE, errno, "Failed to request DELCORE30M_CORE");

	load_firmware(data);

	data->sdma.type = DELCORE30M_SDMA;
	data->sdma.num = 4;

	if (ioctl(data->fd, ELCIOC_RESOURCE_REQUEST, &data->sdma))
		error(EXIT_FAILURE, errno, "Failed to request DELCORE30M_SDMA");

	uint8_t sdma_msk = data->sdma.mask;
	for (int i = 0; i < data->sdma.num; ++i) {
		data->sdma_channels[i] = __builtin_ffs(sdma_msk) - 1;
		sdma_msk &= ~(1 << data->sdma_channels[i]);
	}

	allocate_buffers(data, frame_data);
	printf("DELcore-30M initialize OK\n");
}

void dsp_job_create(struct dsp_struct *data, const int bufs_fd[], const int count)
{
	int input_temp[] = {
			data->result_frame[1]->fd, data->tile_buffers[0]->fd, data->dsp_global_data_buffer->fd, data->tile_buffers[1]->fd,
			data->chain_buffers[0]->fd, data->chain_buffers[1]->fd, data->tileinfo_buffer->fd};
	int input_temp_size = 7;

	int *input = (int *) malloc(sizeof(int) * (input_temp_size + count));

	for (int i = 0; i < count; ++i)
		input[i] = bufs_fd[i];
	for (int i = count; i < input_temp_size + count; ++i)
		input[i] = input_temp[i - count];

	int output[] = {data->background_tile_buffers[0]->fd, data->background_tile_buffers[1]->fd,
			data->background_code_buffers[0]->fd, data->background_code_buffers[1]->fd,
			data->background->fd,
			data->background_chain_buffers[0]->fd, data->background_chain_buffers[1]->fd,
			data->result_frame[0]->fd, data->code_buffers[0]->fd, data->code_buffers[1]->fd};

	job_create(data->fd, &data->job, input, input_temp_size + count, output, 10,
		   data->core.fd, data->sdma.fd);

	free(input);
}

static int dma_init(struct dsp_struct *data, int fd_src, int fd_dst) {
	struct delcore30m_dmachain dmachain_input = {
		.job = data->job.fd,
		.core = __builtin_ffs(data->core.mask) - 1,
		.external = fd_src,
		.internal = { data->tile_buffers[0]->fd, data->tile_buffers[1]->fd },
		.chain = data->chain_buffers[0]->fd,
		.codebuf = data->code_buffers[0]->fd,
		.channel = {SDMA_CHANNEL_INPUT,  data->sdma_channels[0]}
	};
	if (ioctl(data->fd, ELCIOC_DMACHAIN_SETUP, &dmachain_input)) {
		printf("Failed to setup input[0] dmachain: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	struct delcore30m_dmachain dmachain_output = {
		.job = data->job.fd,
		.core = __builtin_ffs(data->core.mask) - 1,
		.external = fd_dst,
		.internal = { data->tile_buffers[0]->fd, data->tile_buffers[1]->fd },
		.chain = data->chain_buffers[1]->fd,
		.codebuf = data->code_buffers[1]->fd,
		.channel = {SDMA_CHANNEL_OUTPUT,  data->sdma_channels[1]}
	};
	if (ioctl(data->fd, ELCIOC_DMACHAIN_SETUP, &dmachain_output)) {
		printf("Failed to setup output[0] dmachain: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	struct delcore30m_dmachain dmachain_background_input = {
		.job = data->job.fd,
		.core = __builtin_ffs(data->core.mask) - 1,
		.external = data->background->fd,
		.internal = { data->background_tile_buffers[0]->fd, data->background_tile_buffers[1]->fd },
		.chain = data->background_chain_buffers[0]->fd,
		.codebuf = data->background_code_buffers[0]->fd,
		.channel = {SDMA_CHANNEL_INPUT,  data->sdma_channels[2]}
	};
	if (ioctl(data->fd, ELCIOC_DMACHAIN_SETUP, &dmachain_background_input)) {
		printf("Failed to setup input[1] dmachain: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	struct delcore30m_dmachain dmachain_background_output = {
		.job = data->job.fd,
		.core = __builtin_ffs(data->core.mask) - 1,
		.external = data->background->fd,
		.internal = { data->background_tile_buffers[0]->fd, data->background_tile_buffers[1]->fd },
		.chain = data->background_chain_buffers[1]->fd,
		.codebuf = data->background_code_buffers[1]->fd,
		.channel = {SDMA_CHANNEL_OUTPUT,  data->sdma_channels[3]}
	};
	if (ioctl(data->fd, ELCIOC_DMACHAIN_SETUP, &dmachain_background_output)) {
		printf("Failed to setup output[1] dmachain: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int job_start(struct dsp_struct *data)
{
	int ret;
	sigset_t mask;

	if (ioctl(data->fd, ELCIOC_JOB_ENQUEUE, &data->job) < 0) {
		puts("Failed to enqueue job");
		return EXIT_FAILURE;
	}

	struct pollfd fds = {
		.fd = data->job.fd,
		.events = POLLIN | POLLPRI | POLLOUT | POLLHUP
	};

	sigfillset(&mask);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	ret = poll(&fds, 1, 2000);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);

	if (ret == -1) {
		puts("Failed to poll()");
		return EXIT_FAILURE;
	}
	if (ret == 0) {
		if (ioctl(data->fd, ELCIOC_JOB_CANCEL, &data->job)) {
			puts("Failed to cancel job");
			return EXIT_FAILURE;
		}
		puts("Job timed out");
		return EXIT_FAILURE;
	}
	if (ioctl(data->fd, ELCIOC_JOB_STATUS, &data->job)) {
		puts("Failed to get job status");
		return EXIT_FAILURE;
	}
	if (data->job.rc != DELCORE30M_JOB_SUCCESS) {
		puts("Job failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

void dsp_free(struct dsp_struct *data)
{
	munmap(data->result_frame_data[0], data->result_frame[0]->size);
	munmap(data->result_frame_data[1], data->result_frame[1]->size);
	dsp_deinit(data);
}

int frame_detector(struct dsp_struct *data, int buf_fd, int dma_buf_ind)
{
	if (dma_init(data, buf_fd, data->result_frame[dma_buf_ind]->fd))
		return EXIT_FAILURE;

	if (job_start(data))
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}
