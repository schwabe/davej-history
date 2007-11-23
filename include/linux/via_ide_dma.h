/* Via Apollo timings display header file for triton.c.
   Copyright (c) 1998 Michel Aubry 
*/

typedef char *PCHAR;

static int via_get_info(char *, char **, off_t, int, int);
static PCHAR print_apollo_drive_config(char *buf,byte bus, byte fn);
static PCHAR print_apollo_ide_config(char *buf, byte bus, byte fn);
static PCHAR print_apollo_chipset_control1(char *buf,byte bus, byte fn);
static PCHAR print_apollo_chipset_control2(char *p,byte bus, byte fn);
static PCHAR print_apollo_chipset_control3(char *p, byte bus, byte fn, unsigned short n);

static struct proc_dir_entry via_proc_entry = {
  0, 3, "via", S_IFREG | S_IRUGO, 1, 0, 0, 0, 0, via_get_info
};

/* we save bus, function of chipset here for further debug use */
static byte bmide_bus, bmide_fn;

static char *FIFO_str[] = {" 1 ", "3/4", "1/2", "1/4"};
static char *control3_str[] = {"No limitation", "64","128","192"};
 
static int via_get_info(char *buffer, char **addr, off_t offset, int count, int dummy)
{
   /* print what /proc/via displays, if required from DISPLAY_APOLLO_TIMINGS */
     char *p = buffer;
     /* Parameter of chipset : */
     /* Miscellaneous control 1 */
     p = print_apollo_chipset_control1(buffer,bmide_bus, bmide_fn);
     /* Miscellaneous control 2 */
     p = print_apollo_chipset_control2(p,bmide_bus, bmide_fn);
     /* Parameters of drives: */
     /* Header */
     p += sprintf(p,"------------------Primary IDE------------Secondary IDE-----\n");
     p = print_apollo_chipset_control3(p, bmide_bus, bmide_fn, 0);
     p = print_apollo_ide_config(p,bmide_bus, bmide_fn);
     p += sprintf(p,"--------------drive0------drive1-------drive0------drive1----\n");
     p = print_apollo_chipset_control3(p, bmide_bus, bmide_fn, 1);
     p = print_apollo_drive_config(p,bmide_bus, bmide_fn);
 
     return p-buffer; /* hoping it is less than 4K... */
}
 
static PCHAR print_apollo_drive_config(char *buf,byte bus, byte fn)
{
     int rc;
     unsigned int time;
     byte tm;
     char *p = buf;  
 
     /*  printk("--------------drive0------drive1-------drive0------drive1----");*/
 
     /* Drive Timing Control */
 
     rc = pcibios_read_config_dword(bus, fn, 0x48, &time);
     p += sprintf(p,"Act Pls Width:  %02d          %02d           %02d          %02d\n",((time & 0xf0000000)>>28)+1,((time & 0xf00000)>>20)+1,((time & 0xf000)>>12)+1, ((time & 0xf0)>>4)+1);
     p += sprintf(p,"Recovery Time:  %02d          %02d           %02d          %02d\n",((time & 0x0f000000)>>24)+1, ((time & 0x0f0000)>>16)+1, ((time & 0x0f00)>>8)+1, (time & 0x0f)+1);
 
     /* Address Setup Time */
 
     rc = pcibios_read_config_byte(bus, fn, 0x4C, &tm);
     p += sprintf(p, "Add. Setup T.:  %01dT          %01dT           %01dT          %01dT\n",((tm & 0xc0)>>6) + 1,((tm & 0x30)>>4) + 1,((tm & 0x0c)>>2) + 1,(tm & 0x03) + 1);
 
     /* UltraDMA33 Extended Timing Control */
 
     rc = pcibios_read_config_dword(bus, fn, 0x50, &time);
     p += sprintf(p, "------------------UDMA-Timing-Control------------------------\n");
     p += sprintf(p, "Enable Meth.:    %01d           %01d            %01d           %01d\n",(time & 0x80000000)?1:0,(time & 0x800000)?1:0, (time & 0x8000)?1:0, (time & 0x80)?1:0);
     p += sprintf(p, "Enable:         %s         %s          %s         %s\n",(time & 0x40000000)?"yes":"no ", (time & 0x400000)?"yes":"no ",(time & 0x4000)?"yes":"no ",(time & 0x40)?"yes":"no ");
     p += sprintf(p, "Transfer Mode: %s         %s          %s         %s\n",(time & 0x20000000)?"PIO":"DMA",(time & 0x200000)?"PIO":"DMA",(time & 0x2000)?"PIO":"DMA",(time & 0x20)?"PIO":"DMA");
     p += sprintf(p, "Cycle Time:     %01dT          %01dT           %01dT          %01dT\n",((time & 0x03000000)>>24)+2,((time & 0x030000)>>16)+2,((time & 0x0300)>>8)+2,(time & 0x03)+2);
 
     return (PCHAR)p;
}
 
static PCHAR print_apollo_ide_config(char *buf, byte bus, byte fn)
{
     byte time, tmp; 
     unsigned short size0,size1;
     int rc;
     char *p = buf;  
 
     rc = pcibios_read_config_byte(bus, fn, 0x41, &time);
     p += sprintf(p,"Prefetch Buffer :      %s                     %s\n", (time & 128)?"on ":"off", (time & 32)?"on ":"off");
     p += sprintf(p,"Post Write Buffer:     %s                     %s\n", (time & 64)? "on ": "off",(time & 16)?"on ":"off");
 
     /* FIFO configuration */
     rc = pcibios_read_config_byte(bus, fn, 0x43, &time);
     tmp = ((time & 0x20)>>2) + ((time & 0x40)>>3);
     p += sprintf(p,"FIFO Conf/Chan. :      %02d                      %02d\n", 16 - tmp, tmp);
     tmp = (time & 0x0F)>>2;
     p += sprintf(p,"Threshold Prim. :      %s                     %s\n", FIFO_str[tmp],FIFO_str[time & 0x03]);
 
     /* chipset Control3 */
     rc = pcibios_read_config_byte(bus, fn, 0x46, &time);
     p += sprintf(p,"Read DMA FIFO flush:   %s                     %s\n",(time & 0x80)?"on ":"off", (time & 0x40)?"on ":"off");
     p += sprintf(p,"End Sect. FIFO flush:  %s                     %s\n",(time & 0x20)?"on ":"off", (time & 0x10)?"on ":"off");
     p += sprintf(p,"Max DRDY Pulse Width:  %s %s\n", control3_str[(time & 0x03)], (time & 0x03)? "PCI clocks":"");
 
     /* Primary and Secondary sector sizes */
     rc = pcibios_read_config_word(bus, fn, 0x60, &size0);
     rc = pcibios_read_config_word(bus, fn, 0x68, &size1);
     p += sprintf(p,"Bytes Per Sector:      %03d                     %03d\n",size0 & 0xfff,size1 & 0xfff);
     return (PCHAR)p;
}

static PCHAR print_apollo_chipset_control1(char *buf,byte bus, byte fn)
{
     byte t;
     int rc;
     char *p = buf;  
     unsigned short c;
     byte l,l_max;   
 
     rc = pcibios_read_config_word(bus, fn, 0x04, &c);
     rc = pcibios_read_config_byte(bus, fn, 0x44, &t);
     rc = pcibios_read_config_byte(bus, fn, 0x0d, &l);
     rc = pcibios_read_config_byte(bus, fn, 0x3f, &l_max);
     p += sprintf(p,"Command register = 0x%x\n",c);
     p += sprintf(p,"Master Read  Cycle IRDY %d Wait State\n", (t & 64)>>6);
     p += sprintf(p,"Master Write Cycle IRDY %d Wait State\n", (t & 32)>>5 );
     p += sprintf(p,"FIFO Output Data 1/2 Clock Advance: %s\n", (t & 16)? "on ":"off");
     p += sprintf(p,"Bus Master IDE Status Register Read Retry: %s\n", (t & 8)? "on " : "off");
     p += sprintf(p,"Latency timer = %d (max. = %d)\n",l,l_max);
     return (PCHAR)p;
}
 
static PCHAR print_apollo_chipset_control2(char *buf,byte bus, byte fn)
{
     byte t;
     int rc;
     char *p = buf;  
     rc = pcibios_read_config_byte(bus, fn, 0x45, &t);
     p += sprintf(p,"Interrupt Steering Swap: %s\n", (t & 64)? "on ":"off");
     return (PCHAR)p;
}
 
static PCHAR print_apollo_chipset_control3(char *buf, byte bus, byte fn, unsigned short n)
{
     /* at that point we can be sure that register 0x20 of the chipset contains the right address... */
     unsigned int bibma;
     int rc;
     byte c0,c1;    
     char *p = buf; 
 
     rc = pcibios_read_config_dword(bus, fn, 0x20, &bibma);
     bibma = (bibma & 0xfff0) ;
 
     /* at that point bibma+0x2 et bibma+0xa are byte registers to investigate:*/
     c0 = inb((unsigned short)bibma + 0x02);
     c1 = inb((unsigned short)bibma + 0x0a);
 
     if(n==0)
        /*p = sprintf(p,"--------------------Primary IDE------------Secondary IDE-----");*/
        p += sprintf(p,"both channels togth:   %s                     %s\n",(c0&0x80)?"no":"yes",(c1&0x80)?"no":"yes");
     else
        /*p = sprintf(p,"--------------drive0------drive1-------drive0------drive1----");*/
        p += sprintf(p,"DMA enabled:    %s         %s          %s         %s\n",(c0&0x20)?"yes":"no ",(c0&0x40)?"yes":"no ",(c1&0x20)?"yes":"no ",(c1&0x40)?"yes":"no ");
 
     return (PCHAR)p;
}
