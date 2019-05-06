/*
 * \file
 * \brief This test runs Fibonacci numbers
 * calculation on two DSP cores in parallel
 *
 * \copyright
 * Copyright 2019 RnD Center "ELVEES", JSC
 *
 */

#include <asm/types.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <linux/delcore30m.h>

#define MAKE_STR_(s) #s
#define MAKE_STR(s) MAKE_STR_(s)

int stop = 0;
int debug_mode = 0;
int iters = 5;
char* fw_path = MAKE_STR(FIRMWARE_PATH) "fibonacci.fw.bin";

struct delcore30m_job jobs[MAX_CORES];
struct delcore30m_buffer *iter_bufs[MAX_CORES];
struct delcore30m_buffer *magic_bufs[MAX_CORES];
struct delcore30m_buffer *result_bufs[MAX_CORES];

void help(const char *pname)
{
    printf("%s [options]\n", pname);
    printf("Options:\n");
    printf("    -v arg\tSet debug mode.\n");
    printf("    -i arg\tSet number of iterations. Default is 5\n");
    printf("    -f arg\tSet firmware path. Default is /usr/share/delcore30m-tests/firmware-delcore30m-fibonacci.bin\n");
}

int read_buffer(int fd)
{
    int *mmap_buf = (int *)mmap(NULL, 4, PROT_READ, MAP_SHARED, fd, 0);
    int ret = mmap_buf[0];

    munmap((void*)mmap_buf, 4);
    return ret;
}

int check()
{
    int it[MAX_CORES];
    int sleep_time = 3;

    /* The final fibonacci number DSP must find */
    int magic = 1788967197;

    for (int core = 0; core < MAX_CORES; ++core)
        it[core] = read_buffer(iter_bufs[core]->fd);

    while (!stop && iters--) {
        sleep(sleep_time);
        if (debug_mode) {
            for (int core = 0; core < MAX_CORES; ++core)
                printf("%d/", (read_buffer(iter_bufs[core]->fd) - it[core]) / sleep_time);
            printf("\n");
        }

        for (int core = 0; core < MAX_CORES; ++core) {
            if (read_buffer(iter_bufs[core]->fd) == it[core]) {
                printf("Iterator is not changed\n");
                return EXIT_FAILURE;
            }
            if (read_buffer(magic_bufs[core]->fd) != magic) {
                printf("Calculated fibonacci number is wrong\n");
                return EXIT_FAILURE;
            }

            it[core] = read_buffer(iter_bufs[core]->fd);
        }
    }

    return stop;
}

struct delcore30m_buffer *buf_alloc(int fd, enum delcore30m_memory_type type,
                                    int core_num, int size, int init_val)
{
    struct delcore30m_buffer *buffer = (struct delcore30m_buffer *)
				       malloc(sizeof(struct delcore30m_buffer));
    buffer->size = size;
    buffer->type = type;
    buffer->core_num = core_num;

    if (ioctl(fd, ELCIOC_BUF_ALLOC, buffer) < 0) {
        error(0, errno, "Failed to allocate buffer");
        goto err;
    }

    void *mmap_buf = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, buffer->fd, 0);
    if (mmap_buf == MAP_FAILED) {
        error(0, errno, "Failed to map buffer");
        goto err;
    }

    memcpy(mmap_buf, (void*)&init_val, sizeof(int));
    munmap(mmap_buf, buffer->size);

    return buffer;

err:
    free(buffer);
    return NULL;
}

int load_firmware(int fd, uint8_t core_id, const char *filename)
{
    int ret = EXIT_FAILURE;

    FILE *f = fopen(filename, "r");
    if (!f)
        error(EXIT_FAILURE, errno, "Failed to open %s file", filename);

    fseek(f, 0, SEEK_END);
    uint32_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *data = malloc(size);
    if (!data) {
        error(0, errno, "Failed to allocate memory");
        goto exit;
    }

    fread(data, 1, size, f);
    if (ferror(f)) {
        error(0, errno, "Failed to read firmware");
        goto free_data;
    }

    void *buf_cores = mmap(NULL, size, PROT_WRITE,
                           MAP_SHARED, fd, 4096 * core_id);
    if (buf_cores == MAP_FAILED)
        goto free_data;

    memcpy(buf_cores, data, size);
    munmap(buf_cores, size);

    ret = EXIT_SUCCESS;

free_data:
    free(data);

exit:
    fclose(f);
    return ret;
}

void signal_handler(int sig)
{
    stop = 1;
}

int main(int argc, char **argv)
{
    int rc = 0;

    signal(SIGINT, signal_handler);

    int fd = open("/dev/elcore0", O_RDWR);
    if (fd < 0)
        error(EXIT_FAILURE, errno, "Failed to open device file");

    int opt;
    while ((opt = getopt(argc, argv, "hvi:f:")) != -1) {
        switch (opt) {
        case 'h':
            help(argv[0]);
            return EXIT_SUCCESS;
        case 'v':
            debug_mode = 1;
            break;
        case 'i':
            iters = atoi(optarg);
            break;
        case 'f':
            fw_path = optarg;
            break;
        default:
            error(EXIT_FAILURE, errno, "Try %s -h for help.\n", argv[0]);
        }
    }

    for (int core = 0; core < MAX_CORES; ++core) {

        struct delcore30m_resource cores_res = {
            .type = DELCORE30M_CORE,
            .num = 1
        };

        while (ioctl(fd, ELCIOC_RESOURCE_REQUEST, &cores_res)) {
            if (errno == EBUSY)
                continue;
            error(EXIT_FAILURE, errno, "Failed to request resource");
        }

        int core_id = ffs(cores_res.mask) - 1;
        if (load_firmware(cores_res.fd, core_id, fw_path))
            return EXIT_FAILURE;

        iter_bufs[core_id] = buf_alloc(fd, DELCORE30M_MEMORY_XYRAM, core_id,
                                       4, 0);
        if (iter_bufs[core_id] == NULL)
            return EXIT_FAILURE;

        magic_bufs[core_id] = buf_alloc(fd, DELCORE30M_MEMORY_XYRAM, core_id,
                                        4, 0);
        if (magic_bufs[core_id] == NULL)
                return EXIT_FAILURE;

        result_bufs[core_id] = buf_alloc(fd, DELCORE30M_MEMORY_XYRAM, core_id,
                                         16384, 0);
        if (result_bufs[core_id] == NULL)
            return EXIT_FAILURE;

        jobs[core_id].inum = 0;
        jobs[core_id].onum = 3;
        jobs[core_id].cores_fd = cores_res.fd;

        jobs[core_id].output[0] = iter_bufs[core_id]->fd;
        jobs[core_id].output[1] = magic_bufs[core_id]->fd;
        jobs[core_id].output[2] = result_bufs[core_id]->fd;

        if (ioctl(fd, ELCIOC_JOB_CREATE, &jobs[core_id]) < 0)
            error(EXIT_FAILURE, errno, "Failed to create job");
    }
    for (int core = 0; core < MAX_CORES; ++core)
        if (ioctl(fd, ELCIOC_JOB_ENQUEUE, &jobs[core]) < 0)
                error(EXIT_FAILURE, errno, "Failed to enqueue job");

    rc = check();

    for (int core = 0; core < MAX_CORES; ++core)
        if (ioctl(fd, ELCIOC_JOB_CANCEL, &jobs[core]) < 0)
                error(EXIT_FAILURE, errno, "Failed to cancel job");

    if (rc)
        error(EXIT_FAILURE, errno, "Finished with error");
    else
        printf("Finished successfully\n");

    return rc;
}
