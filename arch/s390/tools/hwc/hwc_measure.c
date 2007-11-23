/*
 * small application for HWC measurement 
 *
 * Copyright (C) 2000 IBM Corporation
 * Author(s): Martin Peschke <peschke@fh-brandenburg.de>
 */

#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <errno.h>

/* everything about the HWC low level driver ioctl-commands */
#include "../../../../drivers/s390/char/hwc_rw.h"

/* standard input, should be our HWC tty */
#define DESCRIPTOR 0

int main(int argc, char *argv[], char *env[])
{
	ioctl_meas_t	measured = 0;
	signed int	retval = 0;

	if (argc = 2) {

		if (strcmp(argv[1], "lines") == 0) {
			retval = ioctl(DESCRIPTOR, TIOCHWCGMEASL,
					&measured);
			if (retval < 0)
				return errno;
			else {
				printf("%ld\n", measured);
				return retval;
			}
		}

		if (strcmp(argv[1], "chars") == 0) {
			retval = ioctl(DESCRIPTOR, TIOCHWCGMEASC,
					&measured);
			if (retval < 0)
				return errno;
			else {
				printf("%ld\n", measured);
				return retval;
			}
		}

		if (strcmp(argv[1], "wcalls") == 0) {
			retval = ioctl(DESCRIPTOR, TIOCHWCGMEASS,
					&measured);
			if (retval < 0)
				return errno;
			else {
				printf("%ld\n", measured);
				return retval;
			}
		}

		if (strcmp(argv[1], "reset") == 0) {
			retval = ioctl(DESCRIPTOR, TIOCHWCSMEAS);
			if (retval < 0)
				return errno;
			else	return retval;
		}
	}

	printf("usage:\n");
	printf("   hwc_measure lines          "
		"(prints # of measured lines)        or\n");
	printf("   hwc_measure_chars          "
		"(prints # of measured characters)   or\n");
	printf("   hwc_measure wcalls         "
		"(prints # of measured write calls to HWC interface)   or\n");
	printf("   hwc_measure reset          "
		"(resets measurement counters)\n");

	return -1;
}

