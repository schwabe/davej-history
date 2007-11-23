/*

 *      ippfvsadm - Port Fowarding & Virtual Server ADMinistration program v1.0
 *
 *      Copyright (c) 1998 Wensong Zhang
 *      All rights reserved.
 *
 *      Author: Wensong Zhang <wensong@iinchina.net>
 *
 *      This program is derived from Steven Clarke's ipportfw program.
 *
 *      portfw - Port Forwarding Table Editing v1.1
 *
 *      Copyright (c) 1997 Steven Clarke
 *      All rights reserved.
 *
 *      Author: Steven Clarke <steven@monmouth.demon.co.uk>
 *
 *              Keble College
 *              Oxford
 *              OX1 3PG
 *
 *              WWW:    http://www.monmouth.demon.co.uk/
 *
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 */

#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/if.h>
#include <linux/timer.h>
#include <linux/ip_fw.h>
#include <sys/param.h>

#define IP_PORTFW_NONE	0
#define IP_PORTFW_LIST	10000
#define DEFAULT_WEIGHT      1
#define IPPROTO_NONE	65535

long string_to_number (char *str, int min, int max);
int parse_addressport (char *name, __u32 * raddr, __u16 * rport);
int do_setsockopt (int cmd, struct ip_portfw_edits *data, int length);
void exit_error (int status, char *msg);
void exit_display_help (void);
void list_forwarding (void);

char *program;

int 
main (int argc, char *argv[])
{
    int c;
    int command = IP_PORTFW_NONE;
    struct ip_portfw_edits pfw;

    pfw.protocol = IPPROTO_NONE;
    pfw.raddr = 0;
    pfw.rport = 0;
    pfw.laddr = 0;
    pfw.lport = 0;
    pfw.weight = 0;

    program = argv[0];

    while ((c = getopt (argc, argv, "ADCLt:u:R:w:h")) != -1)
	switch (c)
	  {
	  case 'A':
	      if (command != IP_PORTFW_NONE)
		  exit_error (2, "multiple commands specified");
	      command = IP_PORTFW_ADD;
	      break;
	  case 'D':
	      if (command != IP_PORTFW_NONE)
		  exit_error (2, "multiple commands specified");
	      command = IP_PORTFW_DEL;
	      break;
	  case 'C':
	      if (command != IP_PORTFW_NONE)
		  exit_error (2, "multiple commands specified");
	      command = IP_PORTFW_FLUSH;
	      break;
	  case 'L':
	      if (command != IP_PORTFW_NONE)
		  exit_error (2, "multiple commands specified");
	      command = IP_PORTFW_LIST;
	      break;

	  case 't':
	  case 'u':
	      if (pfw.protocol != IPPROTO_NONE)
		  exit_error (2, "multiple protocols specified");
	      pfw.protocol = (c == 't' ? IPPROTO_TCP : IPPROTO_UDP);
	      if (parse_addressport (optarg, &pfw.laddr, &pfw.lport) == -1)
		  exit_error (2, "illegal virtual server address:port specified");
	      break;
	  case 'R':
	      if (pfw.raddr != 0 || pfw.rport != 0)
		  exit_error (2, "multiple destinations specified");
	      if (parse_addressport (optarg, &pfw.raddr, &pfw.rport) == -1)
		  exit_error (2, "illegal destination specified");
	      break;
	  case 'w':
	      if (pfw.weight != 0)
		  exit_error (2, "multiple server weights specified");
	      pfw.weight = string_to_number (optarg, SERVER_WEIGHT_MIN, SERVER_WEIGHT_MAX);
	      if (pfw.weight == -1)
		  exit_error (2, "illegal weight specified");
	      break;
	  case 'h':
	  case '?':
	  default:
	      exit_display_help ();
	  }

    if (pfw.weight == 0)
	pfw.weight = DEFAULT_WEIGHT;

    if (optind < argc)
	exit_error (2, "unknown arguments found on commandline");

    if (command == IP_PORTFW_NONE)
	exit_display_help ();

    else if (command == IP_PORTFW_ADD &&
	     (pfw.protocol == IPPROTO_NONE || pfw.lport == 0 ||
	      pfw.rport == 0 || pfw.raddr == 0))
	exit_error (2, "insufficient options specified");

    else if (command == IP_PORTFW_DEL &&
	     (pfw.protocol == IPPROTO_NONE || pfw.lport == 0))
	exit_error (2, "insufficient options specified");

    else if ((command == IP_PORTFW_FLUSH || command == IP_PORTFW_LIST) &&
	     (pfw.protocol != IPPROTO_NONE || pfw.lport != 0 ||
	      pfw.rport != 0 || pfw.raddr != 0))
	exit_error (2, "incompatible options specified");

    if (command == IP_PORTFW_LIST)
	list_forwarding ();
    else
	exit (do_setsockopt (command, &pfw, sizeof (pfw)));
}


long 
string_to_number (char *str, int min, int max)
{
    char *end;
    long number;

    number = strtol (str, &end, 10);
    if (*end == '\0' && end != str)
      {
	  if (min <= number && number <= max)
	      return number;
	  else
	      return -1;
      }
    else
	return -1;
}


int 
parse_addressport (char *name, __u32 * raddr, __u16 * rport)
{
    char buf[23];		/* xxx.xxx.xxx.xxx:ppppp\0 */
    char *p, *q;
    int onebyte, i;
    long l;

    strncpy (buf, name, sizeof (buf) - 1);
    if ((p = strchr (buf, ':')) == NULL)
	return -1;

    *p = '\0';
    if ((l = string_to_number (p + 1, IP_PORTFW_PORT_MIN, IP_PORTFW_PORT_MAX)) == -1)
	return -1;
    else
	*rport = l;

    p = buf;
    *raddr = 0;
    for (i = 0; i < 3; i++)
      {
	  if ((q = strchr (p, '.')) == NULL)
	      return -1;
	  else
	    {
		*q = '\0';
		if ((onebyte = string_to_number (p, 0, 255)) == -1)
		    return -1;
		else
		    *raddr = (*raddr << 8) + onebyte;
	    }
	  p = q + 1;
      }

    /* we've checked 3 bytes, now we check the last one */
    if ((onebyte = string_to_number (p, 0, 255)) == -1)
	return -1;
    else
	*raddr = (*raddr << 8) + onebyte;

    return 0;
}


int 
do_setsockopt (int cmd, struct ip_portfw_edits *data, int length)
{
    static int sockfd = -1;
    int ret;

    if (sockfd == -1)
      {
	  if ((sockfd = socket (AF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
	    {
		perror ("ippfvsadm: socket creation failed");
		exit (1);
	    }
      }

    ret = setsockopt (sockfd, IPPROTO_IP, cmd, (char *) data, length);
    if (ret)
	perror ("ippfvsadm: setsockopt failed");

    return ret;
}


void 
exit_error (int status, char *msg)
{
    fprintf (stderr, "%s: %s\n", program, msg);
    exit (status);
}

void 
list_forwarding (void)
{
    char buffer[256];

    FILE *handle;
    handle = fopen ("/proc/net/ip_portfw", "r");
    if (!handle)
      {
	  printf ("Could not open /proc/net/ip_portfw\n");
	  printf ("Are you sure you have Port Forwarding & Virtual Server support installed?\n");
	  exit (1);
      }

    while (!feof (handle))
	if (fgets (buffer, 256, handle))
	    printf ("%s", buffer);
    fclose (handle);

}

void 
exit_display_help (void)
{
    printf ("%s v1.0 1998/5/26\n\n"
	  "Usage: %s -A -[t|u] l.l.l.l:lport -R a.a.a.a:rport [-w weight]\n"
	    "       %s -D -[t|u] l.l.l.l:lport -R a.a.a.a:rport\n"
	    "       %s -C\n"
	    "       %s -L\n\n"
	    "Commands:\n"
	    "       -A  To add a real server\n"
	    "       -D  To delete a real server\n"
	    "       -C  To clear the table\n"
	    "       -L  To list the table\n\n"
	    "Options:\n"
	    "  t means TCP protocol, and u indicates UDP protocol.\n"
            "  l.l.l.l:lport are the IP address and port of the virtual server.\n"
	    "  a.a.a.a:rport are the IP address and port of the real server.\n"
	    "  weight is a value to indicate the processing capacity of a real server.\n",
	    program, program, program, program, program);

    exit (0);
}
