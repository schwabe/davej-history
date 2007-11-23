/*
 *  arch/s390/boot/silo.c
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Holger Smolinski <linux390@de.ibm.com>
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <linux/fs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <asm/ioctl.h>

/* from dasd.h */
#define DASD_PARTN_BITS 2
#define BIODASDRWTB _IOWR('D',5,int)
/* end */

#define PRINT_LEVEL(x,y...) if ( silo_options.verbosity >= x ) printf(y)
#define ERROR_LEVEL(x,y...) if ( silo_options.verbosity >= x ) fprintf(stderr,y)
#define TOGGLE(x) ((x)=((x)?(0):(1)))
#define GETARG(x) {int len=strlen(optarg);x=malloc(len);strncpy(x,optarg,len);PRINT_LEVEL(1,"%s set to %s\n",#x,optarg);}

#define ITRY(x) if ( (x) == -1 ) { ERROR_LEVEL(0,"%s (line:%d) '%s' returned %d='%s'\n", __FILE__,__LINE__,#x,errno,strerror(errno)); usage(); exit(1); }
#define NTRY(x) if ( (x) == 0 ) { ERROR_LEVEL(0,"%s (line:%d) '%s' returned %d='%s'\n", __FILE__,__LINE__,#x,errno,strerror(errno)); usage(); exit(1); }

#define MAX_CLUSTERS 256
#define PARTN_MASK ((1 << DASD_PARTN_BITS) - 1)

struct silo_options
  {
    short int verbosity;
    struct
      {
	unsigned char testonly;
      }
    flags;
    char *image;
    char *ipldevice;
    char *parmfile;
    char *ramdisk;
    char *bootsect;
  }
silo_options =
{
  1,				/* verbosity */
  {
    0,				/* testonly */
  }
  ,
    "./image",			/* image */
    NULL,			/* ipldevice */
    NULL,			/* parmfile */
    NULL,			/* initrd */
    "ipleckd.boot",		/* bootsector */
};

struct blockdesc
  {
    unsigned long off;
    unsigned short ct;
    unsigned long addr;
  };

struct blocklist
  {
    struct blockdesc blk[MAX_CLUSTERS];
    unsigned short ix;
  };

void
usage (void)
{
  printf ("Usage:\n");
  printf ("silo -d ipldevice [additional options]\n");
  printf ("-d /dev/node : set ipldevice to /dev/node\n");
  printf ("-f image : set image to image\n");
  printf ("-p parmfile : set parameter file to parmfile\n");
  printf ("-b bootsect : set bootsector to bootsect\n");
  printf ("Additional options\n");
  printf ("-v: increase verbosity level\n");
  printf ("-v#: set verbosity level to #\n");
  printf ("-t: toggle testonly flag\n");
  printf ("-h: print this message\n");
  printf ("-?: print this message\n");
}

int
parse_options (struct silo_options *o, int argc, char *argv[])
{
  int rc = 0;
  int oc;
  if (argc == 1)
    {
      usage ();
      exit (0);
    }
  while ((oc = getopt (argc, argv, "f:d:p:r:b:B:h?v::t")) != -1)
    {
      switch (oc)
	{
	case 't':
	  TOGGLE (o->flags.testonly);
	  PRINT_LEVEL (1, "Testonly flag is now %sactive\n", o->flags.testonly ? "" : "in");
	  break;
	case 'v':
	  {
	    unsigned short v;
	    if (optarg && sscanf (optarg, "%hu", &v))
	      o->verbosity = v;
	    else
	      o->verbosity++;
	    PRINT_LEVEL (1, "Verbosity value is now %hu\n", o->verbosity);
	    break;
	  }
	case 'h':
	case '?':
	  usage ();
	  break;
	case 'd':
	  GETARG (o->ipldevice);
	  break;
	case 'f':
	  GETARG (o->image);
	  break;
	case 'p':
	  GETARG (o->parmfile);
	  break;
	case 'r':
	  GETARG (o->ramdisk);
	  break;
	case 'b':
	  GETARG (o->bootsect);
	  break;
	default:
	  rc = EINVAL;
	  break;
	}
    }
  return rc;
}

int
verify_device (char *name)
{
  int rc = 0;
  struct stat dst;
  struct stat st;
  ITRY (stat (name, &dst));
  if (S_ISBLK (dst.st_mode))
    {
      if (!(MINOR (dst.st_rdev) & PARTN_MASK))
	{
	  rc = dst.st_rdev;
	}
      else
	/* invalid MINOR & PARTN_MASK */
	{
	  ERROR_LEVEL (1, "Cannot boot from partition %d %d %d",
		       (int) PARTN_MASK, (int) MINOR (dst.st_rdev), (int) (PARTN_MASK & MINOR (dst.st_rdev)));
	  rc = -1;
	  errno = EINVAL;
	}
    }
  else
    /* error S_ISBLK */
    {
      ERROR_LEVEL (1, "%s is no block device\n", name);
      rc = -1;
      errno = EINVAL;
    }
  return rc;
}

int
verify_file (char *name, int dev)
{
  int rc = 0;
  struct stat dst;
  struct stat st;
  int bs = 1024;
  int l;
  ITRY (stat (name, &dst));
  if (S_ISREG (dst.st_mode))
    {
      if ((unsigned) MAJOR (dev) == (unsigned) MAJOR (dst.st_dev) && (unsigned) MINOR (dev) == (unsigned) (MINOR (dst.st_dev) & ~PARTN_MASK))
	{
	  /* whatever to do if all is ok... */
	}
      else
	/* devicenumber doesn't match */
	{
	  ERROR_LEVEL (1, "%s is not on device (%d/%d) but on (%d/%d)\n", name, (unsigned) MAJOR (dev), (unsigned) MINOR (dev), (unsigned) MAJOR (dst.st_dev), (unsigned) (MINOR (dst.st_dev) & ~PARTN_MASK));
	  rc = -1;
	  errno = EINVAL;
	}
    }
  else
    /* error S_ISREG */
    {
      ERROR_LEVEL (1, "%s is neither regular file nor linkto one\n", name);
      rc = -1;
      errno = EINVAL;
    }
  return rc;
}

int
verify_options (struct silo_options *o)
{
  int rc = 0;
  int dev = 0;
  int crc = 0;
  if (!o->ipldevice || !o->image || !o->bootsect)
    {
      usage ();
      exit (1);
    }
  PRINT_LEVEL (1, "IPL device is: '%s'", o->ipldevice);

  ITRY (dev = verify_device (o->ipldevice));
  PRINT_LEVEL (2, "...ok...(%d/%d)", (unsigned short) MAJOR (dev), (unsigned short) MINOR (dev));
  PRINT_LEVEL (1, "\n");

  PRINT_LEVEL (0, "bootsector is: '%s'", o->bootsect);
  ITRY (verify_file (o->bootsect, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  PRINT_LEVEL (0, "Kernel image is: '%s'", o->image);
  ITRY (verify_file (o->image, dev));
  PRINT_LEVEL (1, "...ok...");
  PRINT_LEVEL (0, "\n");

  if (o->parmfile)
    {
      PRINT_LEVEL (0, "parameterfile is: '%s'", o->parmfile);
      ITRY (verify_file (o->parmfile, dev));
      PRINT_LEVEL (1, "...ok...");
      PRINT_LEVEL (0, "\n");
    }

  if (o->ramdisk)
    {
      PRINT_LEVEL (0, "initialramdisk is: '%s'", o->ramdisk);
      ITRY (verify_file (o->ramdisk, dev));
      PRINT_LEVEL (1, "...ok...");
      PRINT_LEVEL (0, "\n");
    }

  return crc;
}


int
add_file_to_blocklist (char *name, struct blocklist *lst, long addr)
{
  int fd;
  int devfd;
  struct stat fst;
  int i;
  int blk;
  int bs;
  int blocks;

  int rc = 0;

  ITRY (fd = open (name, O_RDONLY));
  ITRY (fstat (fd, &fst));
  ITRY (mknod ("/tmp/silodev", S_IFBLK | S_IRUSR | S_IWUSR, fst.st_dev));
  ITRY (devfd = open ("/tmp/silodev", O_RDONLY));
  ITRY (ioctl (fd, FIGETBSZ, &bs));
  blocks = (fst.st_size + bs - 1) / bs;
  for (i = 0; i < blocks; i++)
    {
      blk = i;
      ITRY (ioctl (fd, FIBMAP, &blk));
      if (blk)
	{
	  int oldblk = blk;
	  ITRY (ioctl (devfd, BIODASDRWTB, &blk));
	  if (blk <= 0)
	    {
	      ERROR_LEVEL (0, "BIODASDRWTB on blk %d returned %d\n", oldblk, blk);
	      break;
	    }
	}
      else
	{
	  PRINT_LEVEL (1, "Filled hole on blk %d\n", i);
	}
      if (lst->ix == 0 || i == 0  || 
	  lst->blk[lst->ix - 1].ct >= 128 ||
	  (lst->blk[lst->ix - 1].off + lst->blk[lst->ix - 1].ct != blk &&
	   !(lst->blk[lst->ix - 1].off == 0 && blk == 0)))
	{
	  if (lst->ix >= MAX_CLUSTERS)
	    {
	      rc = 1;
	      errno = ENOMEM;
	      break;
	    }
	  lst->blk[lst->ix].off = blk;
	  lst->blk[lst->ix].ct = 1;
	  lst->blk[lst->ix].addr = addr + i * bs;
	  lst->ix++;
	}
      else
	{
	  lst->blk[lst->ix - 1].ct++;
	}
    }
  ITRY(unlink("/tmp/silodev"));
  return rc;
}

int
write_bootsect (char *ipldevice, char *bootsect, struct blocklist *blklst)
{
  int i;
  int s_fd, d_fd, b_fd, bd_fd;
  struct stat s_st, d_st, b_st;
  int rc=0;
  int bs, boots;
  char buffer[4096];
  ITRY (d_fd = open (ipldevice, O_RDWR | O_SYNC));
  ITRY (fstat (d_fd, &d_st));
  ITRY (s_fd = open ("boot.map", O_RDWR | O_TRUNC | O_CREAT | O_SYNC));
  ITRY (verify_file (bootsect, d_st.st_rdev));
  for (i = 0; i < blklst->ix; i++)
    {
      int offset = blklst->blk[i].off;
      int addrct = blklst->blk[i].addr | (blklst->blk[i].ct & 0xff);
      PRINT_LEVEL (1, "ix %i: offset: %06x count: %02x address: 0x%08x\n", i, offset, blklst->blk[i].ct & 0xff, blklst->blk[i].addr);
      NTRY (write (s_fd, &offset, sizeof (int)));
      NTRY (write (s_fd, &addrct, sizeof (int)));
    }
  ITRY (ioctl (s_fd,FIGETBSZ, &bs));
  ITRY (stat ("boot.map", &s_st));
  if (s_st.st_size > bs )
    {
      ERROR_LEVEL (0,"boot.map is larger than one block\n");
      rc = -1;
      errno = EINVAL;
    }
  boots=0;
  ITRY (mknod ("/tmp/silodev", S_IFBLK | S_IRUSR | S_IWUSR, s_st.st_dev));
  ITRY (bd_fd = open ("/tmp/silodev", O_RDONLY));
  ITRY ( ioctl(s_fd,FIBMAP,&boots));
  ITRY (ioctl (bd_fd, BIODASDRWTB, &boots));
  PRINT_LEVEL (1, "Bootmap is in block no: 0x%08x\n", boots);
  close (bd_fd);
  close(s_fd);
  ITRY (unlink("/tmp/silodev"));
  /* Now patch the bootsector */
  ITRY (b_fd = open (bootsect, O_RDONLY));
  NTRY (read (b_fd, buffer, 4096));
  memset (buffer + 0xe0, 0, 8);
  *(int *) (buffer + 0xe0) = boots;
  NTRY (write (d_fd, buffer, 4096));
  NTRY (write (d_fd, buffer, 4096));
  close (b_fd);
  close (d_fd);
  return rc;
}

int
do_silo (struct silo_options *o)
{
  int rc = 0;

  int device_fd;
  int image_fd;
  struct blocklist blklist;
  memset (&blklist, 0, sizeof (struct blocklist));
  ITRY (add_file_to_blocklist (o->image, &blklist, 0x00000000));
  if (o->parmfile)
    {
      ITRY (add_file_to_blocklist (o->parmfile, &blklist, 0x00008000));
    }
  if (o->ramdisk)
    {
      ITRY (add_file_to_blocklist (o->ramdisk, &blklist, 0x00800000));
    }
  ITRY (write_bootsect (o->ipldevice, o->bootsect, &blklist));

  return rc;
}

int
main (int argct, char *argv[])
{
  int rc = 0;
  ITRY (parse_options (&silo_options, argct, argv));
  ITRY (verify_options (&silo_options));
  ITRY (do_silo (&silo_options));
  return rc;
}
