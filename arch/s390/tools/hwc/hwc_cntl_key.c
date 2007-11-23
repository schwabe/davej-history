/*
 * small application to set string that will be used as CNTL-C 
 * employing a HWC terminal ioctl command
 *
 * returns:	number of written or read characters
 *
 * Copyright (C) 2000 IBM Corporation
 * Author(s): Martin Peschke <peschke@fh-brandenburg.de>
 */

#include <string.h>
#include <stdio.h>

/* everything about the HWC terminal driver ioctl-commands */
#include "../../../../drivers/s390/char/hwc_tty.h"

/* standard input, should be our HWC tty */
#define DESCRIPTOR 0

int main(int argc, char *argv[], char *env[])
{
	unsigned char buf[HWC_TTY_MAX_CNTL_SIZE];

	if (argc >= 2) {
		if (strcmp(argv[1], "c") == 0 ||
		    strcmp(argv[1], "C") == 0 ||
		    strcmp(argv[1], "INTR_CHAR") == 0) {
			if (argc == 2) {
				ioctl(DESCRIPTOR, TIOCHWCTTYGINTRC, buf);
				printf("%s\n", buf);
				return strlen(buf);
			} else return ioctl(DESCRIPTOR, TIOCHWCTTYSINTRC, argv[2]);
// currently not yet implemented in HWC terminal driver
#if 0
		} else if (strcmp(argv[1], "d") == 0 ||
                	   strcmp(argv[1], "D") == 0 ||
                	   strcmp(argv[1], "EOF_CHAR") == 0) {
	                if (argc == 2) {
        	                ioctl(DESCRIPTOR, TIOCHWCTTYGEOFC, buf);
                	        printf("%s\n", buf);
                        	return strlen(buf);
			} else return ioctl(DESCRIPTOR, TIOCHWCTTYSEOFC, argv[2]);
	        } else if (strcmp(argv[1], "z") == 0 ||
                	   strcmp(argv[1], "Z") == 0 ||
                	   strcmp(argv[1], "SUSP_CHAR") == 0) {
                	if (argc == 2) {
                        	ioctl(DESCRIPTOR, TIOCHWCTTYGSUSPC, buf);
                        	printf("%s\n", buf);
                        	return strlen(buf);
                	} else return ioctl(DESCRIPTOR, TIOCHWCTTYSSUSPC, argv[2]);
        	} else if (strcmp(argv[1], "n") == 0 ||
                   	   strcmp(argv[1], "N") == 0 ||
                   	   strcmp(argv[1], "NEW_LINE") == 0) {
                	if (argc == 2) {
                        	ioctl(DESCRIPTOR, TIOCHWCTTYGNL, buf);
                        	printf("%s\n", buf);
                        	return strlen(buf);
                	} else return ioctl(DESCRIPTOR, TIOCHWCTTYSNL, argv[2]);
#endif
		}
	}

	printf("usage: hwc_cntl_key <control-key> [<new string>]\n");
	printf("  <control-key> ::= \"c\" | \"C\" | \"INTR_CHAR\" |\n");
	printf("                    \"d\" | \"D\" | \"EOF_CHAR\" |\n");
	printf("                    \"z\" | \"Z\" | \"SUSP_CHAR\" |\n");
	printf("                    \"n\" | \"N\" | \"NEW_LINE\"\n");
	return -1;
}
