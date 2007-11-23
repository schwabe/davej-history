/* 
 * File...........: linux/drivers/s390/block/dasd_setup.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *                : Utz Bacher <utz.bacher@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 */

#include <linux/ctype.h>
#include <linux/malloc.h>

#include <linux/dasd.h>
#include "dasd_types.h"

#define PRINTK_HEADER "dasd(setup):"

#define MIN(a,b) (((a)<(b))?(a):(b))

static int dasd_autodetect = 0;
int dasd_probeonly = 1;
static int dasd_count = 0;
static int dasd_devno[DASD_MAX_DEVICES] =
{0,};
int dasd_force_mdsk_flag[DASD_MAX_DEVICES] =
{0,};

extern char *dasd[DASD_MAX_DEVICES];
#ifdef CONFIG_DASD_MDSK
extern char *dasd_force_mdsk[DASD_MAX_DEVICES];
#endif

typedef struct dasd_range {
	int from, to;
	struct dasd_range *next;
} dasd_range;

static dasd_range *first_range = NULL;

void 
dasd_add_devno_to_ranges (int devno)
{
	dasd_range *p, *prev;
	for (p = first_range; p; prev = p, p = prev->next) {
		if (devno >= p->from && devno <= p->to) {
			PRINT_WARN ("devno %04X already in range %04X-%04X\n",
				    devno, p->from, p->to);
			return;
		}
		if (devno == (p->from - 1)) {
			p->from--;
			return;
		}
		if (devno == (p->to + 1)) {
			p->to++;
			return;
		}
	}
	prev = kmalloc (sizeof (dasd_range), GFP_ATOMIC);
	prev->from = prev->to = devno;
	if (!first_range) {
		first_range = prev;
	}
	return;
}

int
dasd_proc_print_probed_ranges (char *buf, char **start, off_t off, int len, int d)
{
	dasd_range *p;
	len = sprintf (buf, "Probed ranges of the DASD driver\n");
	for (p = first_range; p; p = p->next) {
		if (len >= PAGE_SIZE - 80)
			len += sprintf (buf + len, "terminated...\n");
		if (p != first_range) {
			len += sprintf (buf + len, ",");
		}
		if (p->from == p->to) {
			len += sprintf (buf + len, "%04x", p->from);
		} else {
			len += sprintf (buf + len, "%04x-%04x",
					p->from, p->to);
		}
	}
	len += sprintf (buf + len, "\n");
	return len;
}

static int
dasd_get_hexdigit (char c)
{
	if ((c >= '0') && (c <= '9'))
		return c - '0';
	if ((c >= 'a') && (c <= 'f'))
		return c + 10 - 'a';
	if ((c >= 'A') && (c <= 'F'))
		return c + 10 - 'A';
	return -1;
}

/* sets the string pointer after the next comma */
static void
dasd_scan_for_next_comma (char **strptr)
{
	while (((**strptr) != ',') && (**strptr))
		(*strptr)++;

	/* set the position AFTER the comma */
	if (**strptr == ',')
		(*strptr)++;
}

/*sets the string pointer after the next comma, if a parse error occured */
static int
dasd_get_next_int (char **strptr)
{
	int j, i = -1;		/* for cosmetic reasons first -1, then 0 */
	if (isxdigit (**strptr)) {
		for (i = 0; isxdigit (**strptr);) {
			i <<= 4;
			j = dasd_get_hexdigit (**strptr);
			if (j == -1) {
				PRINT_ERR ("no integer: skipping range.\n");
				dasd_scan_for_next_comma (strptr);
				i = -1;
				break;
			}
			i += j;
			(*strptr)++;
			if (i > 0xffff) {
				PRINT_ERR (" value too big, skipping range.\n");
				dasd_scan_for_next_comma (strptr);
				i = -1;
				break;
			}
		}
	}
	return i;
}

int
devindex_from_devno (int devno)
{
	int i;
	if (dasd_probeonly) {
		return 0;
	}
	for (i = 0; i < dasd_count; i++) {
		if (dasd_devno[i] == devno)
			return i;
	}
	if (dasd_autodetect) {
		if (dasd_count < DASD_MAX_DEVICES) {
			dasd_devno[dasd_count] = devno;
			return dasd_count++;
		}
		return -EOVERFLOW;
	}
	return -ENODEV;
}

/* returns 1, if dasd_no is in the specified ranges, otherwise 0 */
int
dasd_is_accessible (int devno)
{
	return (devindex_from_devno (devno) >= 0);
}

/* dasd_insert_range skips ranges, if the start or the end is -1 */
void
dasd_insert_range (int start, int end)
{
	int curr;
	if (dasd_count >= DASD_MAX_DEVICES) {
		PRINT_ERR (" too many devices specified, ignoring some.\n");
		return;
	}
	if ((start == -1) || (end == -1)) {
		PRINT_ERR
		    ("invalid format of parameter, skipping range\n");
		return;
	}
	if (end < start) {
		PRINT_ERR (" ignoring range from %x to %x - start value " \
			   "must be less than end value.\n", start, end);
		return;
	}
/* concurrent execution would be critical, but will not occur here */
	for (curr = start; curr <= end; curr++) {
		if (dasd_is_accessible (curr)) {
			PRINT_WARN (" %x is already in list as device %d\n",
				    curr, devindex_from_devno (curr));
		}
		dasd_devno[dasd_count] = curr;
		dasd_count++;
		if (dasd_count >= DASD_MAX_DEVICES) {
			PRINT_ERR (" too many devices specified, ignoring some.\n");
			break;
		}
	}
	PRINT_INFO (" added dasd range from %x to %x.\n",
		    start, dasd_devno[dasd_count - 1]);

}

void
dasd_setup (char *str, int *ints)
{
	int devno, devno2;
	static const char *adstring = "autodetect";
	static const char *prstring = "probeonly";
	if (!strncmp (str, prstring,
		      MIN (strlen (str), strlen (prstring)))) {
		dasd_autodetect = 1;
		return;
	}
	if (!strncmp (str, adstring,
		      MIN (strlen (str), strlen (adstring)))) {
		dasd_autodetect = 1;
		dasd_probeonly = 0;
		return;
	}
	dasd_probeonly = 0;
	while (*str && *str != 1) {
		if (!isxdigit (*str)) {
			str++;	/* to avoid looping on two commas */
			PRINT_ERR (" kernel parameter in invalid format.\n");
			continue;
		}
		devno = dasd_get_next_int (&str);

		/* range was skipped? -> scan for comma has been done */
		if (devno == -1)
			continue;

		if (*str == ',') {
			str++;
			dasd_insert_range (devno, devno);
			continue;
		}
		if (*str == '-') {
			str++;
			devno2 = dasd_get_next_int (&str);
			if (devno2 == -1) {
				PRINT_ERR (" invalid character in " \
					   "kernel parameters.");
			} else {
				dasd_insert_range (devno, devno2);
			}
			dasd_scan_for_next_comma (&str);
			continue;
		}
		if (*str == 0) {
			dasd_insert_range (devno, devno);
			break;
		}
		PRINT_ERR (" unexpected character in kernel parameter, " \
			   "skipping range.\n");
	}
}
#ifdef CONFIG_DASD_MDSK
int dasd_force_mdsk_flag[DASD_MAX_DEVICES];

/*
 * Parameter parsing function, called from init/main.c
 * size    : size in kbyte
 * offset  : offset after which minidisk is available
 * blksize : blocksize minidisk is formated
 * Format is: mdisk=<vdev>,<vdev>,...
 */
void
dasd_mdsk_setup (char *str, int *ints)
{
	int devno, devno2;
	int di, i;

	while (*str && *str != 1) {
		if (!isxdigit (*str)) {
			str++;	/* to avoid looping on two commas */
			PRINT_ERR (" kernel parameter in invalid format.\n");
			continue;
		}
		devno = dasd_get_next_int (&str);

		/* range was skipped? -> scan for comma has been done */
		if (devno == -1)
			continue;

		if (*str == ',') {
			str++;
			di = devindex_from_devno (devno);
			if (di >= DASD_MAX_DEVICES) {
				return;
			} else if (di < 0)
				dasd_insert_range (devno, devno);
			dasd_force_mdsk_flag[di] = 1;
			continue;
		}
		if (*str == '-') {
			str++;
			devno2 = dasd_get_next_int (&str);
			if (devno2 == -1) {
				PRINT_ERR (" invalid character in " \
					   "kernel parameters.");
			} else {
				for (i = devno; i <= devno2; i++) {
					di = devindex_from_devno (i);
					if (di >= DASD_MAX_DEVICES) {
						return;
					} else if (di < 0)
						dasd_insert_range (i, i);
					dasd_force_mdsk_flag[di] = 1;
				}
			}
			dasd_scan_for_next_comma (&str);
			continue;
		}
		if (*str == 0) {
			di = devindex_from_devno (devno);
			if (di >= DASD_MAX_DEVICES) {
				return;
			} else if (di < 0)
				dasd_insert_range (devno, devno);
			dasd_force_mdsk_flag[di] = 1;
			break;
		}
		PRINT_ERR (" unexpected character in kernel parameter, " \
			   "skipping range.\n");
	}
}
#endif

#ifdef MODULE
int
dasd_parse_module_params (void)
{
	while (dasd)
		dasd_setup (dasd, NULL);
#ifdef CONFIG_DASD_MDSK
	while (dasd_force_mdsk)
		dasd_mdsk_setup (dasd_force_mdsk, NULL);
#endif
}
#endif
