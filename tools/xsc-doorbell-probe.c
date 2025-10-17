// SPDX-License-Identifier: GPL-2.0
/*
 * XSC Doorbell Probe Tool
 *
 * Usage: xsc-doorbell-probe [--device <name>] [--cpu <N>] [--bursts <N>]
 *                            [--interval-us <N>] [--p99 <N>us] [--max <N>us]
 *
 * Exit codes:
 *   0 - Doorbell validated and enabled
 *   1 - Validation failed (fell back to polling)
 *   2 - Invalid arguments
 *   3 - Permission denied (requires root/CAP_SYS_ADMIN)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <getopt.h>

#define XSC_DEV_PATH		"/dev/xsc"
#define XSC_SYSFS_DOORBELL	"/sys/kernel/xsc/doorbell"

/* IOCTL commands (must match kernel) */
#define XSC_IOC_MAGIC		'x'
#define XSC_IOC_TEST_DOORBELL	_IOW(XSC_IOC_MAGIC, 10, struct xsc_doorbell_test_params)
#define XSC_IOC_GET_DB_STATS	_IOR(XSC_IOC_MAGIC, 11, struct xsc_doorbell_stats)

struct xsc_doorbell_test_params {
	uint32_t cpu;
	uint32_t bursts;
	uint32_t interval_us;
	uint64_t p99_threshold_ns;
	uint64_t max_threshold_ns;
};

struct xsc_doorbell_stats {
	uint64_t total_irqs;
	uint64_t useful_irqs;
	uint64_t spurious_irqs;
	uint64_t wrong_cpu_irqs;
	uint64_t min_latency_ns;
	uint64_t max_latency_ns;
	uint64_t avg_latency_ns;
};

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [OPTIONS]\n"
		"\n"
		"Options:\n"
		"  --device <name>       Device name (default: auto-detect)\n"
		"  --cpu <N>             Target CPU for IRQ affinity (default: 0)\n"
		"  --bursts <N>          Number of test doorbells (default: 100000)\n"
		"  --interval-us <N>     Interval between pokes in µs (default: 20)\n"
		"  --p99 <N>us           P99 latency threshold (default: 150)\n"
		"  --max <N>us           Maximum latency threshold (default: 500)\n"
		"  -v, --verbose         Verbose output\n"
		"  -h, --help            This help\n"
		"\n"
		"Exit codes:\n"
		"  0 = Doorbell validated and enabled\n"
		"  1 = Validation failed\n"
		"  2 = Invalid arguments\n"
		"  3 = Permission denied\n",
		prog);
}

static uint64_t get_timestamp_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int read_sysfs_u64(const char *path, uint64_t *value)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -errno;

	int ret = fscanf(f, "%lu", value);
	fclose(f);

	return (ret == 1) ? 0 : -EINVAL;
}

static int check_doorbell_status(int verbose)
{
	char path[256];
	char status[32];
	uint64_t val;
	FILE *f;

	/* Check if doorbell is enabled */
	snprintf(path, sizeof(path), "%s/mode", XSC_SYSFS_DOORBELL);
	f = fopen(path, "r");
	if (!f) {
		if (verbose)
			fprintf(stderr, "No doorbell found in sysfs\n");
		return -ENODEV;
	}

	if (fscanf(f, "%31s", status) != 1) {
		fclose(f);
		return -EINVAL;
	}
	fclose(f);

	printf("mode=%s", status);

	/* Read stats if available */
	if (strcmp(status, "ENABLED") == 0 || strcmp(status, "COALESCED") == 0) {
		snprintf(path, sizeof(path), "%s/irq", XSC_SYSFS_DOORBELL);
		if (read_sysfs_u64(path, &val) == 0)
			printf(" irq=%lu", val);

		snprintf(path, sizeof(path), "%s/cpu", XSC_SYSFS_DOORBELL);
		if (read_sysfs_u64(path, &val) == 0)
			printf(" cpu=%lu", val);

		snprintf(path, sizeof(path), "%s/p99_ns", XSC_SYSFS_DOORBELL);
		if (read_sysfs_u64(path, &val) == 0)
			printf(" p99=%luus", val / 1000);

		snprintf(path, sizeof(path), "%s/max_ns", XSC_SYSFS_DOORBELL);
		if (read_sysfs_u64(path, &val) == 0)
			printf(" max=%luus", val / 1000);

		snprintf(path, sizeof(path), "%s/spurious", XSC_SYSFS_DOORBELL);
		if (read_sysfs_u64(path, &val) == 0)
			printf(" spurious=%lu", val);
	}

	snprintf(path, sizeof(path), "%s/status", XSC_SYSFS_DOORBELL);
	f = fopen(path, "r");
	if (f) {
		char stat[32];
		if (fscanf(f, "%31s", stat) == 1)
			printf(" status=%s", stat);
		fclose(f);
	}

	printf("\n");

	return (strcmp(status, "ENABLED") == 0) ? 0 : 1;
}

static int run_doorbell_test(struct xsc_doorbell_test_params *params, int verbose)
{
	int fd, ret;
	struct xsc_doorbell_stats stats;

	/* Open XSC device */
	fd = open(XSC_DEV_PATH, O_RDWR);
	if (fd < 0) {
		if (errno == EACCES || errno == EPERM) {
			fprintf(stderr, "Permission denied - run as root\n");
			return 3;
		}
		fprintf(stderr, "Failed to open %s: %s\n",
			XSC_DEV_PATH, strerror(errno));
		return 1;
	}

	if (verbose) {
		printf("Running doorbell validation test:\n");
		printf("  CPU: %u\n", params->cpu);
		printf("  Bursts: %u\n", params->bursts);
		printf("  Interval: %u µs\n", params->interval_us);
		printf("  P99 threshold: %lu ns\n", params->p99_threshold_ns);
		printf("  Max threshold: %lu ns\n", params->max_threshold_ns);
	}

	/* Run test via ioctl */
	ret = ioctl(fd, XSC_IOC_TEST_DOORBELL, params);
	if (ret < 0) {
		fprintf(stderr, "Doorbell test failed: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	/* Get final stats */
	ret = ioctl(fd, XSC_IOC_GET_DB_STATS, &stats);
	if (ret < 0) {
		fprintf(stderr, "Failed to get stats: %s\n", strerror(errno));
		close(fd);
		return 1;
	}

	close(fd);

	/* Print results */
	printf("Test completed:\n");
	printf("  Total IRQs: %lu\n", stats.total_irqs);
	printf("  Useful IRQs: %lu (%lu%%)\n",
	       stats.useful_irqs,
	       stats.total_irqs ? (stats.useful_irqs * 100) / stats.total_irqs : 0);
	printf("  Spurious: %lu\n", stats.spurious_irqs);
	printf("  Wrong CPU: %lu\n", stats.wrong_cpu_irqs);
	printf("  Min latency: %lu ns\n", stats.min_latency_ns);
	printf("  Avg latency: %lu ns\n", stats.avg_latency_ns);
	printf("  Max latency: %lu ns\n", stats.max_latency_ns);

	/* Check thresholds */
	if (stats.max_latency_ns > params->max_threshold_ns) {
		fprintf(stderr, "FAIL: Max latency exceeds threshold\n");
		return 1;
	}

	if (stats.avg_latency_ns > params->p99_threshold_ns) {
		fprintf(stderr, "FAIL: Average latency exceeds P99 threshold\n");
		return 1;
	}

	if (stats.spurious_irqs > 0) {
		fprintf(stderr, "FAIL: Spurious IRQs detected\n");
		return 1;
	}

	if (stats.wrong_cpu_irqs > 0) {
		fprintf(stderr, "FAIL: IRQs delivered to wrong CPU\n");
		return 1;
	}

	printf("SUCCESS: Doorbell validated\n");
	return 0;
}

int main(int argc, char **argv)
{
	struct xsc_doorbell_test_params params = {
		.cpu = 0,
		.bursts = 100000,
		.interval_us = 20,
		.p99_threshold_ns = 150000,  /* 150 µs */
		.max_threshold_ns = 500000,  /* 500 µs */
	};

	int verbose = 0;
	int opt, option_index;

	static struct option long_options[] = {
		{"device",	required_argument, 0, 'd'},
		{"cpu",		required_argument, 0, 'c'},
		{"bursts",	required_argument, 0, 'b'},
		{"interval-us",	required_argument, 0, 'i'},
		{"p99",		required_argument, 0, 'p'},
		{"max",		required_argument, 0, 'm'},
		{"verbose",	no_argument,	   0, 'v'},
		{"help",	no_argument,	   0, 'h'},
		{0, 0, 0, 0}
	};

	while ((opt = getopt_long(argc, argv, "d:c:b:i:p:m:vh",
				  long_options, &option_index)) != -1) {
		switch (opt) {
		case 'd':
			/* Device name - for future use */
			break;
		case 'c':
			params.cpu = atoi(optarg);
			break;
		case 'b':
			params.bursts = atoi(optarg);
			break;
		case 'i':
			params.interval_us = atoi(optarg);
			break;
		case 'p':
			params.p99_threshold_ns = atol(optarg) * 1000;
			break;
		case 'm':
			params.max_threshold_ns = atol(optarg) * 1000;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	/* Check current status first */
	if (verbose)
		check_doorbell_status(verbose);

	/* Run validation test */
	return run_doorbell_test(&params, verbose);
}
