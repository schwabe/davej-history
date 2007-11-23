/* Aladdin 5229 timings display header file for triton.c.
   Copyright (c) 1998-99 Michel Aubry
   Copyright (c) 1999 Andre Hedrick
*/


static int ali_get_info(char *, char **, off_t, int, int);

static struct proc_dir_entry ali_proc_entry = {
  0, 3, "ali", S_IFREG | S_IRUGO, 1, 0, 0, 0, 0, ali_get_info
};

/* we save bus, function of chipset here for further debug use */
static byte bmide_bus, bmide_fn;

static char *fifo[4] = {
	"FIFO Off",
	"FIFO On ",
	"DMA mode",
	"PIO mode"
};

static char *udmaT[8] = {
	"1.5T",
	"  2T",
	"2.5T",
	"  3T",
	"3.5T",
	"  4T",
	"  6T",
	"  8T"
};

char *channel_status[8] = {
	"OK            ",
	"busy          ",
	"DRQ           ",
	"DRQ busy      ",
	"error         ",
	"error busy    ",
	"error DRQ     ",
	"error DRQ busy"
};

static int ali_get_info(char *buffer, char **addr, off_t offset, int count, int dummy)
{
	byte reg53h, reg5xh, reg5yh, reg5xh1, reg5yh1;
	unsigned int bibma;
	byte c0, c1;
	byte rev, tmp;
	char *p = buffer;
	char *q;

	/* fetch rev. */
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x08, &rev);
	if (rev >= 0xc1)	/* M1543C or newer */
		udmaT[7] = " ???";
	else
		fifo[3]  = "   ???  ";

	/* first fetch bibma: */
	pcibios_read_config_dword(bmide_bus, bmide_fn, 0x20, &bibma);
	bibma = (bibma & 0xfff0) ;
	/*
	 * at that point bibma+0x2 et bibma+0xa are byte
	 * registers to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);

	p += sprintf(p,
		"\n                                Ali M15x3 Chipset.\n");
	p += sprintf(p,
		"                                ------------------\n");
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x78, &reg53h);
	p += sprintf(p, "PCI Clock: %d.\n", reg53h);

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x53, &reg53h);
	p += sprintf(p,
		"CD_ROM FIFO:%s, CD_ROM DMA:%s\n",
		(reg53h & 0x02) ? "Yes" : "No ",
		(reg53h & 0x01) ? "Yes" : "No " );
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x74, &reg53h);
	p += sprintf(p,
		"FIFO Status: contains %d Words, runs%s%s\n\n",
		(reg53h & 0x3f),
		(reg53h & 0x40) ? " OVERWR" : "",
		(reg53h & 0x80) ? " OVERRD." : "." );

	p += sprintf(p,
		"-------------------primary channel-------------------secondary channel---------\n\n");

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x09, &reg53h);
	p += sprintf(p,
		"channel status:       %s                               %s\n",
		(reg53h & 0x20) ? "On " : "Off",
		(reg53h & 0x10) ? "On " : "Off" );

	p += sprintf(p,
		"both channels togth:  %s                               %s\n",
		(c0&0x80) ? "No " : "Yes",
		(c1&0x80) ? "No " : "Yes" );

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x76, &reg53h);
	p += sprintf(p,
		"Channel state:        %s                    %s\n",
		channel_status[reg53h & 0x07],
		channel_status[(reg53h & 0x70) >> 4] );

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x58, &reg5xh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5c, &reg5yh);
	p += sprintf(p,
		"Add. Setup Timing:    %dT                                %dT\n",
		(reg5xh & 0x07) ? (reg5xh & 0x07) : 8,
		(reg5yh & 0x07) ? (reg5yh & 0x07) : 8 );

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x59, &reg5xh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5d, &reg5yh);
	p += sprintf(p,
		"Command Act. Count:   %dT                                %dT\n"
		"Command Rec. Count:   %dT                               %dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8, 
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16 );

	p += sprintf(p,
		"----------------drive0-----------drive1------------drive0-----------drive1------\n\n");
	p += sprintf(p,
		"DMA enabled:      %s              %s               %s              %s\n",
		(c0&0x20) ? "Yes" : "No ",
		(c0&0x40) ? "Yes" : "No ",
		(c1&0x20) ? "Yes" : "No ",
		(c1&0x40) ? "Yes" : "No " );

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x54, &reg5xh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x55, &reg5yh);
	q = "FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n";
	if (rev < 0xc1) {
		if ((rev == 0x20) && (pcibios_read_config_byte(bmide_bus, bmide_fn, 0x4f, &tmp), (tmp &= 0x20))) {
			p += sprintf(p, q, 8, 8, 8, 8);
		} else {
			p += sprintf(p, q,
				(reg5xh & 0x03) + 12,
				((reg5xh & 0x30)>>4) + 12,
				(reg5yh & 0x03) + 12,
				((reg5yh & 0x30)>>4) + 12 );
		}
	} else {
		p += sprintf(p, q,
			(tmp = (reg5xh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5xh & 0x30)>>4)) ? (tmp << 3) : 4,
			(tmp = (reg5yh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5yh & 0x30)>>4)) ? (tmp << 3) : 4 );
	}

#if 0
	p += sprintf(p, 
		"FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n",
		(reg5xh & 0x03) + 12,
		((reg5xh & 0x30)>>4) + 12,
		(reg5yh & 0x03) + 12,
		((reg5yh & 0x30)>>4) + 12 );
#endif

	p += sprintf(p,
		"FIFO mode:        %s         %s          %s         %s\n",
		fifo[((reg5xh & 0x0c) >> 2)],
		fifo[((reg5xh & 0xc0) >> 6)],
		fifo[((reg5yh & 0x0c) >> 2)],
		fifo[((reg5yh & 0xc0) >> 6)] );

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5a, &reg5xh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5b, &reg5xh1);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5e, &reg5yh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x5f, &reg5yh1);

	p += sprintf(p,/*
		"------------------drive0-----------drive1------------drive0-----------drive1------\n")*/
		"Dt RW act. Cnt    %dT               %dT                %dT               %dT\n"
		"Dt RW rec. Cnt    %2dT              %2dT               %2dT              %2dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5xh1 & 0x70) ? ((reg5xh1 & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8,
		(reg5yh1 & 0x70) ? ((reg5yh1 & 0x70) >> 4) : 8,
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5xh1 & 0x0f) ? (reg5xh1 & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16,
		(reg5yh1 & 0x0f) ? (reg5yh1 & 0x0f) : 16 );

	p += sprintf(p,
		"-----------------------------------UDMA Timings--------------------------------\n\n");

	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x56, &reg5xh);
	pcibios_read_config_byte(bmide_bus, bmide_fn, 0x57, &reg5yh);
	p += sprintf(p,
		"UDMA:             %s               %s                %s               %s\n"
		"UDMA timings:     %s             %s              %s             %s\n\n",
		(reg5xh & 0x08) ? "OK" : "No",
		(reg5xh & 0x80) ? "OK" : "No",
		(reg5yh & 0x08) ? "OK" : "No",
		(reg5yh & 0x80) ? "OK" : "No",
		udmaT[(reg5xh & 0x07)],
		udmaT[(reg5xh & 0x70) >> 4],
		udmaT[reg5yh & 0x07],
		udmaT[(reg5yh & 0x70) >> 4] );

	return p-buffer; /* => must be less than 4k! */
}

