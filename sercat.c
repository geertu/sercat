/*
 *  Serial Read/Write Test Program
 *
 *  (C) Copyright 2016 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <bsd/stdlib.h>

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <linux/serial.h>

#define BUF_SIZE	1024

static const char *opt_dev;
static uint32_t opt_speed;
static int opt_read;
static int opt_write;
static int opt_hwflow;
static int opt_noflow;
static int opt_verbose;

#define ESC_BLACK	"\e[30m"
#define ESC_RED		"\e[31m"
#define ESC_GREEN	"\e[32m"
#define ESC_YELLOW	"\e[33m"
#define ESC_BLUE	"\e[34m"
#define ESC_PURPLE	"\e[35m"
#define ESC_CYAN	"\e[36m"
#define ESC_WHITE	"\e[37m"
#define ESC_RM		"\e[0m"

static const struct speed {
	speed_t sym;
	unsigned int val;
} speeds[] = {
	{ B0,		0 },
	{ B50,		50 },
	{ B75,		75 },
	{ B110,		110 },
	{ B134,		134 },
	{ B150,		150 },
	{ B200,		200 },
	{ B300,		300 },
	{ B600,		600 },
	{ B1200,	1200 },
	{ B1800,	1800 },
	{ B2400,	2400 },
	{ B4800,	4800 },
	{ B9600,	9600 },
	{ B19200,	19200 },
	{ B38400,	38400 },
#ifdef B57600
	{ B57600,	57600 },
#endif
#ifdef B115200
	{ B115200,	115200 },
#endif
#ifdef B230400
	{ B230400,	230400 },
#endif
#ifdef B460800
	{ B460800,	460800 },
#endif
#ifdef B500000
	{ B500000,	500000 },
#endif
#ifdef B576000
	{ B576000,	576000 },
#endif
#ifdef B921600
	{ B921600,	921600 },
#endif
#ifdef B1000000
	{ B1000000,	1000000 },
#endif
#ifdef B1152000
	{ B1152000,	1152000 },
#endif
#ifdef B1500000
	{ B1500000,	1500000 },
#endif
#ifdef B2000000
	{ B2000000,	2000000 },
#endif
#ifdef B2500000
	{ B2500000,	2500000 },
#endif
#ifdef B3000000
	{ B3000000,	3000000 },
#endif
#ifdef B3500000
	{ B3500000,	3500000 },
#endif
#ifdef B4000000
	{ B4000000,	4000000 },
#endif
};

static int get_speed_val(speed_t speed)
{
	unsigned int i;

	for (i = 0; i < sizeof(speeds)/sizeof(*speeds); i++)
		if (speeds[i].sym == speed)
			return speeds[i].val;
	return -1;
}

static speed_t get_speed_sym(unsigned speed)
{
	unsigned int i;

	for (i = 0; i < sizeof(speeds)/sizeof(*speeds); i++)
		if (speeds[i].val == speed)
			return speeds[i].sym;
	return -1;
}

#define pr_debug(fmt, ...) \
{ \
	if (opt_verbose) \
		printf(fmt, ##__VA_ARGS__); \
}

#define pr_info(fmt, ...) \
	printf(fmt, ##__VA_ARGS__)

#define pr_warn(fmt, ...) \
	printf(ESC_YELLOW fmt ESC_RM, ##__VA_ARGS__)

#define pr_error(fmt, ...) \
	fprintf(stderr, ESC_RED fmt ESC_RM, ##__VA_ARGS__)

static void __attribute__ ((noreturn)) usage(void)
{
	fprintf(stderr,
		"\n"
		"%s: [options] <dev>\n\n"
		"Valid options are:\n"
		"    -h, --help       Display this usage information\n"
		"    -f, --hwflow     Enable hardware flow control (RTS/CTS)\n"
		"    -n, --noflow     Disable hardware flow control\n"
		"    -r, --read       Read mode (default)\n"
		"    -s, --speed      Serial speed\n"
		"    -v, --verbose    Enable verbose mode\n"
		"    -w, --write      Write mode\n"
		"\n",
		getprogname());
	exit(1);
}

static int device_open(const char *pathname, int flags)
{
	struct termios termios;
	int fd;

	pr_debug("Opening %s...\n", pathname);
	fd = open(pathname, flags);
	if (fd < 0) {
		pr_error("Failed to open %s%s: %s\n", pathname,
			 flags == O_WRONLY ? " for writing" :
			 flags == O_RDONLY ? " for reading" : "",
			 strerror(errno));
		exit(-1);
	}

	if (tcgetattr(fd, &termios)) {
		if (errno == ENOTTY) {
			pr_info("%s is not a tty, skipping tty config\n",
				pathname);
			return fd;
		}
		pr_error("Failed to get terminal attributes: %s\n",
			 strerror(errno));
		exit(-1);
	}
	pr_debug("termios.c_iflag = 0%o\n", termios.c_iflag);
	pr_debug("termios.c_oflag = 0%o\n", termios.c_oflag);
	pr_debug("termios.c_cflag = 0%o\n", termios.c_cflag);
	pr_debug("termios.c_lflag = 0%o\n", termios.c_lflag);

	pr_debug("Enable terminal raw mode\n");
	cfmakeraw(&termios);
	if (tcsetattr(fd, TCSANOW, &termios)) {
		pr_error("Failed to enable raw mode: %s\n", strerror(errno));
		exit(-1);
	}

	if (opt_hwflow || opt_noflow) {
		if (opt_hwflow)
			termios.c_cflag |= CRTSCTS;
		else
			termios.c_cflag &= ~CRTSCTS;
		pr_debug("%sabling hardware flow control\n",
			 opt_hwflow ? "En" : "Dis");
		if (tcsetattr(fd, TCSANOW, &termios)) {
			pr_error("Failed to %sable hardware flow control: %s\n",
				 opt_hwflow ? "en" : "dis", strerror(errno));
			exit(-1);
		}
	}

	if (opt_speed) {
		int sym = get_speed_sym(opt_speed);

		if (sym == -1) {
			pr_error("Unknown serial speed %u\n", opt_speed);
			exit(-1);
		}
		pr_debug("Setting serial speed to %u bps\n", opt_speed);
		if (cfsetspeed(&termios, sym)) {
			pr_error("Failed to set serial speed: %s\n",
				 strerror(errno));
			exit(-1);
		}
		if (tcsetattr(fd, TCSANOW, &termios)) {
			pr_error("Failed to set speed attribute: %s\n",
				 strerror(errno));
			exit(-1);
		}
	} else {
		pr_debug("Serial speed is %u/%u\n",
			 get_speed_val(cfgetispeed(&termios)),
			 get_speed_val(cfgetospeed(&termios)));
	}

	pr_debug("Flushing terminal\n");
	if (tcflush(fd, TCIOFLUSH)) {
		pr_error("Failed to flush: %s\n", strerror(errno));
		exit(-1);
	}

	return fd;
}

int main(int argc, char *argv[])
{
	char buf[BUF_SIZE];
	int rx_fd, tx_fd;
	ssize_t in, out;

	while (argc > 1) {
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
			usage();
		} else if (!strcmp(argv[1], "-f") ||
			   !strcmp(argv[1], "--hwflow")) {
			opt_hwflow = 1;
		} else if (!strcmp(argv[1], "-n") ||
			   !strcmp(argv[1], "--noflow")) {
			opt_noflow = 1;
		} else if (!strcmp(argv[1], "-r") ||
			   !strcmp(argv[1], "--read")) {
			opt_read = 1;
		} else if (!strcmp(argv[1], "-w") ||
			   !strcmp(argv[1], "--write")) {
			opt_write = 1;
		} else if (!strcmp(argv[1], "-s") ||
			   !strcmp(argv[1], "--speed")) {
			if (argc <= 2)
				usage();
			opt_speed = strtoul(argv[2], NULL, 0);
			argv++;
			argc--;
		} else if (!strcmp(argv[1], "-v") ||
			   !strcmp(argv[1], "--verbose")) {
			opt_verbose = 1;
		} else if (!opt_dev) {
			opt_dev = argv[1];
		} else {
			usage();
		}
		argv++;
		argc--;
	}

	if (!opt_dev || (opt_hwflow && opt_noflow) || (opt_read && opt_write))
		usage();

	if (opt_write) {
		rx_fd = 0;	/* stdin */
		tx_fd = device_open(opt_dev, O_WRONLY);
	} else {
		rx_fd = device_open(opt_dev, O_RDONLY);
		tx_fd = 1;	/* stdout */
	}

	while (1) {
		in = read(rx_fd, buf, sizeof(buf));
		if (!in)
			break;

		if (in < 0) {
			pr_error("Read error: %s\n", strerror(errno));
			exit(-1);
		}

		out = write(tx_fd, buf, in);
		if (out < 0) {
			pr_error("Write error: %s\n", strerror(errno));
			exit(-1);
		}
		if (out < in) {
			pr_error("Short write %zd < %zu\n", out, in);
			exit(-1);
		}
	}

	close(opt_write ? tx_fd : rx_fd);

	exit(0);
}

