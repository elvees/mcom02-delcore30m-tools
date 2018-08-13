/*
 * \file
 * \brief paralleltest - Addition of two integers
 * on Elcore-30M
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
#include <string.h>
#include <linux/types.h>
#include <asm/types.h>

#include <linux/delcore30m.h>

#define KiB 1024

#define MAKE_STR_(s) #s
#define MAKE_STR(s) MAKE_STR_(s)

unsigned long max_cores_mask = 0;
int max_cores = 0;

static bool passed = false;

static void printresult(void) {
	puts(passed ? "TEST PASSED" : "TEST FAILED");
}

void add_job(int fd, struct delcore30m_job *job, uint32_t *args, uint8_t size, unsigned long cores, int cores_fd)
{
	long sz = sysconf(_SC_PAGESIZE);

	struct delcore30m_buffer bufin = {
		.type = DELCORE30M_MEMORY_XYRAM,
		.size = sz,
		.core_num = (cores == ELCORE30M_CORE_1)
	};

	struct delcore30m_buffer bufout = {
		.type = DELCORE30M_MEMORY_XYRAM,
		.size = sz,
		.core_num = (cores == ELCORE30M_CORE_1)
	};

	if (ioctl(fd, ELCIOC_BUF_ALLOC, &bufin))
		error(EXIT_FAILURE, errno, "Failed to allocate input buffer");

	uint32_t *input_arg = (uint32_t *)mmap(NULL, sz,
					       PROT_READ | PROT_WRITE,
					       MAP_SHARED, bufin.fd, 0);
	if (input_arg == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to mmap input buffer");

	for (int i = 0; i < size * __builtin_popcount(cores); ++i)
		input_arg[i] = args[i];

	munmap((void*)input_arg, sz);

	if (ioctl(fd, ELCIOC_BUF_ALLOC, &bufout))
		error(EXIT_FAILURE, errno, "Failed to allocate output buffer");

	job->input[0] = bufin.fd;
	job->output[0] = bufout.fd;
	job->inum = 1;
	job->onum = 1;
	job->cores_fd = cores_fd;
	job->flags = 0;

	if (ioctl(fd, ELCIOC_JOB_CREATE, job))
		error(EXIT_FAILURE, errno, "Failed to create job");

	if (ioctl(fd, ELCIOC_JOB_ENQUEUE, job))
		error(EXIT_FAILURE, errno, "Failed to enqueue job");
}

int wait_job(int fd, struct delcore30m_job *job)
{
	struct pollfd fds = {
		.fd = job->fd,
		.events = POLLIN | POLLPRI | POLLOUT | POLLHUP
	};
	poll(&fds, 1, 2000);

	if (ioctl(fd, ELCIOC_JOB_STATUS, job) < 0)
		error(EXIT_FAILURE, errno, "Failed to get job status");

	if (job->status != DELCORE30M_JOB_IDLE) {
		ioctl(fd, ELCIOC_JOB_CANCEL, job);
		fprintf(stderr, "Job timed out\n");
		return DELCORE30M_JOB_CANCELLED;
	}

	return job->rc;
}

int check_result(struct delcore30m_job *job, uint32_t mask, uint32_t *args, uint8_t job_index)
{
	int rc = 0;

	uint32_t *output_arg = (uint32_t *)mmap(NULL, sysconf(_SC_PAGESIZE),
						PROT_READ | PROT_WRITE,
						MAP_SHARED, job->output[0], 0);

	if (output_arg == MAP_FAILED)
		error(EXIT_FAILURE, errno, "Failed to mmap output buffer");

	for (int core = 0; mask; ++core, mask >>= 1) {
		if (!(mask & 1))
			continue;
		rc += (output_arg[core] != (args[core * 2] + args[1 + core * 2]));

		fprintf(stdout, "<Core %d> complete task [%d] %s: 0x%08X + 0x%08X = 0x%08X\n",
				core,
				job_index,
				rc ? "Error" : "Success",
				args[core * 2],
				args[core * 2 + 1],
				output_arg[core]);
	}

	fprintf(stdout, "\n");
	return rc;
}

struct thread_private_data {
	int id; //thread id
	int fd; //device fd
	uint8_t errors;
	uint32_t cores;
	int cores_fd;
};

void *worker(void *private_data)
{
	struct thread_private_data *data = private_data;
	struct delcore30m_job job;
	uint32_t args[] = {
		0x12345678 << data->id,
		0x87654321 >> data->id,
		0x11111111 * (data->id % 10),
		0x12345678 + data->id
	};

	data->errors = 0;

	add_job(data->fd, &job, args, 2, data->cores, data->cores_fd);

	if (wait_job(data->fd, &job)) {
		data->errors += 1;
		fprintf(stderr, "[%d] Failed to wait for the job\n", data->id);
		return NULL;
	}

	data->errors += check_result(&job, data->cores, args, data->id);
	return NULL;
}

uint8_t *getbytes(const char *filename, uint32_t *size)
{
	uint8_t *data;

	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		*size = 0;
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	*size = ftell(f);
	fseek(f, 0, SEEK_SET);

	data = malloc(*size);
	fread(data, 1, *size, f);

	fclose(f);

	return data;
}

int main (int argc, char **argv)
{
	int jobs, cores, i, ret;
	pthread_t *threads;
	struct thread_private_data *data;

	atexit(printresult);
	if (argc < 3)
		error(EXIT_FAILURE, 0, "Arguments error occured\n"
				       "First argument - job count\n"
				       "Second argument - core count\n");

	jobs = strtol(argv[1], NULL, 10);
	cores = strtol(argv[2], NULL, 10);

	int fd = open("/dev/elcore0", O_RDWR);
	if (fd < 0)
		error(EXIT_FAILURE, errno, "Failed to open device file");

	struct delcore30m_hardware hw;
	if (ioctl(fd, ELCIOC_SYS_INFO, &hw) < 0)
		error(EXIT_FAILURE, errno, "Failed to get hardware information");

	max_cores = hw.ncores;
	max_cores_mask = (1 << hw.ncores) - 1;

	struct delcore30m_resource cores_res = {
		.type = DELCORE30M_CORE,
		.num = cores
	};

	while (ioctl(fd, ELCIOC_RESOURCE_REQUEST, &cores_res)) {
		if (errno == EBUSY)
			continue;
		error(EXIT_FAILURE, errno, "Failed to request DELCORE30M_CORE");
	}

	uint32_t fwsize;
	uint8_t *fwdata = getbytes(MAKE_STR(FIRMWARE_PATH) "sum.fw.bin", &fwsize);
	uint8_t mask = cores_res.mask;
	for (int core = 0; mask; core++, mask >>= 1) {
		if (!(mask & 1))
			continue;
		uint32_t *buf_cores = (uint32_t *)mmap(NULL, fwsize,
						       PROT_WRITE,
						       MAP_SHARED,
						       cores_res.fd,
						       4096 * core);
		if (buf_cores == MAP_FAILED)
			error(EXIT_FAILURE, errno, "Failed to load firmware");
		memcpy((void *)buf_cores, (void *)fwdata, fwsize);
		munmap((void *)buf_cores, fwsize);
	}

	threads = malloc(sizeof(pthread_t) * jobs);
	data = (struct thread_private_data *)malloc(sizeof(struct thread_private_data) * jobs);
	if (!threads || !data)
		error(EXIT_FAILURE, 0, "Failed to allocate memory for threads");

	for (i = 0; i < jobs; ++i) {
		data[i].fd = fd;
		data[i].id = i;
		data[i].cores = cores_res.mask;
		data[i].cores_fd = cores_res.fd;
		ret = pthread_create(&threads[i], NULL, worker, &data[i]);
		if (ret)
			error(EXIT_FAILURE, ret, "Failed to create pthread");
	}

	int errors = 0;
	for (i = 0; i < jobs; ++i) {
		pthread_join(threads[i], NULL);
		errors += data[i].errors;
	}

	free(threads);
	free(data);

	if (errors)
		return EXIT_FAILURE;

	passed = true;
	return EXIT_SUCCESS;
}
