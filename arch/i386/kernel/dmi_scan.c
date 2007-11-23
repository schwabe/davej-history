#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/apm_bios.h>
#include <asm/io.h>

struct dmi_header
{
	u8	type;
	u8	length;
	u16	handle;
};

static char * __init dmi_string(struct dmi_header *dm, u8 s)
{
	u8 *bp=(u8 *)dm;
	if(!s)
		return "";
	bp+=dm->length;
	s--;
	while(s>0 && *bp)
	{
		bp+=strlen(bp);
		bp++;
		s--;
	}
	return bp;
}

static int __init dmi_table(u32 base, int len, int num, void (*decode)(struct dmi_header *))
{
	u8 *buf;
	struct dmi_header *dm;
	u8 *data;
	int i=0;
		
	buf = ioremap(base, len);
	if(buf==NULL)
		return -1;

	data = buf;
	while(i<num && data-buf+sizeof(struct dmi_header)<=len)
	{
		dm=(struct dmi_header *)data;
		/*
		 *  We want to know the total length (formated area and strings)
		 *  before decoding to make sure we won't run off the table in
		 *  dmi_decode or dmi_string
		 */
		data+=dm->length;
		while(data-buf<len-1 && (data[0] || data[1]))
			data++;
		if(data-buf<len-1)
			decode(dm);
		data+=2;
		i++;
	}
	iounmap(buf);
	return 0;
}


inline int __init dmi_checksum(u8 *buf)
{
	u8 sum=0;
	int a;
	
	for(a=0; a<15; a++)
		sum+=buf[a];
	return (sum==0);
}

int __init dmi_iterate(void (*decode)(struct dmi_header *))
{
	u8 buf[15];
	u32 fp=0xF0000;
	
	while( fp < 0xFFFFF)
	{
		memcpy_fromio(buf, fp, 15);
		if(memcmp(buf, "_DMI_", 5)==0 && dmi_checksum(buf))
		{
			u16 num=buf[13]<<8|buf[12];
			u16 len=buf[7]<<8|buf[6];
			u32 base=buf[11]<<24|buf[10]<<16|buf[9]<<8|buf[8];
#ifdef DUMP_DMI
			/*
			 * DMI version 0.0 means that the real version is taken from
			 * the SMBIOS version, which we don't know at this point.
			 */
			if(buf[14]!=0)
				printk(KERN_INFO "DMI %u.%u present.\n",
					buf[14]>>4, buf[14]&0x0F);
			else
				printk(KERN_INFO "DMI present.\n");
			printk(KERN_INFO "%d structures occupying %d bytes.\n",
				num, len);
			printk(KERN_INFO "DMI table at 0x%08X.\n",
				base);
#endif				
			if(dmi_table(base,len, num, decode)==0)
				return 0;
		}
		fp+=16;
	}
	return -1;
}


/*
 *	Process a DMI table entry. Right now all we care about are the BIOS
 *	and machine entries. For 2.4 we should pull the smbus controller info
 *	out of here.
 */

static void __init dmi_decode(struct dmi_header *dm)
{
	u8 *data = (u8 *)dm;
	
	switch(dm->type)
	{
		case  0:		
#ifdef DUMP_DMI
			printk("BIOS Vendor: %s\n",
				dmi_string(dm, data[4]));
			printk("BIOS Version: %s\n", 
				dmi_string(dm, data[5]));
			printk("BIOS Release: %s\n",
				dmi_string(dm, data[8]));
#endif				
			/*
			 *  Check for clue free BIOS implementations who use
			 *  the following QA technique
			 *
			 *      [ Write BIOS Code ]<------
			 *               |                ^
			 *      < Does it Compile >----N--
			 *               |Y               ^
			 *	< Does it Boot Win98 >-N--
			 *               |Y
			 *           [Ship It]
			 *
			 *	Phoenix A04  08/24/2000 is known bad (Dell Inspiron 5000e)
			 *	Phoenix A07  09/29/2000 is known good (Dell Inspiron 5000)
			 */
			 
			if(strcmp(dmi_string(dm, data[4]), "Phoenix Technologies LTD")==0)
			{
				if(strcmp(dmi_string(dm, data[5]), "A04")==0 
					&& strcmp(dmi_string(dm, data[8]), "08/24/2000")==0)
				{
				   	apm_info.get_power_status_broken = 1;
					printk(KERN_WARNING "BIOS strings suggest APM bugs, disabling power status reporting.\n");
				}
			}
			break;
#ifdef DUMP_DMI
		case 1:
			printk("System Vendor: %s\n",
				dmi_string(dm, data[4]));
			printk("Product Name: %s\n",
				dmi_string(dm, data[5]));
			printk("Version: %s\n",
				dmi_string(dm, data[6]));
			printk("Serial Number: %s\n",
				dmi_string(dm, data[7]));
			break;
		case 2:
			printk("Board Vendor: %s\n",
				dmi_string(dm, data[4]));
			printk("Board Name: %s\n",
				dmi_string(dm, data[5]));
			printk("Board Version: %s\n",
				dmi_string(dm, data[6]));
			break;
#endif			
	}
}

int __init dmi_scan_machine(void)
{
	return dmi_iterate(dmi_decode);
}

__initcall(dmi_scan_machine);
