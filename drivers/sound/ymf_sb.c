/*
  Legacy audio driver for YMF724, 740, 744, 754 series.
  Copyright 2000 Daisuke Nagano <breeze.nagano@nifty.ne.jp>

  Based on the VIA 82Cxxx driver by Jeff Garzik <jgarzik@pobox.com>

  Distribued under the GNU PUBLIC LICENSE (GPL) Version 2.
  See the "COPYING" file distributed with kernel source tree for more info.

  -------------------------------------------------------------------------

  It only supports SBPro compatible function of YMF7xx series s.t.
    * 22.05kHz, 8-bit and stereo sample
    * OPL3-compatible FM synthesizer
    * MPU-401 compatible "external" MIDI interface

  and AC'97 mixer of these cards.

  -------------------------------------------------------------------------

  Revision history

   Tue May 14 19:00:00 2000   0.0.1
   * initial release

   Tue May 16 19:29:29 2000   0.0.2

   * add a little delays for reset devices.
   * fixed addressing bug.

   Sun May 21 15:14:37 2000   0.0.3

   * Add 'master_vol' module parameter to change 'PCM out Vol' of AC'97.
   * remove native UART401 support. External MIDI port should be supported 
     by sb_midi driver.
   * add support for SPDIF OUT. Module parameter 'spdif_out' is now available.

   Wed May 31 00:13:57 2000   0.0.4

   * remove entries in Hwmcode.h. Now YMF744 / YMF754 sets instructions 
     in 724hwmcode.h.
   * fixed wrong legacy_io setting on YMF744/YMF754 .

   Sat Jun  3 23:44:08 2000   0.0.5

   * correct all return value -1 to appropriate -E* value.
   * add pci_enable_device() for linux-2.3.x.

   Sat Jun  7 00:17:32 2000   0.1.0

   * Remove SBPro mixer support.
   * Added AC'97 mixer support and remove option 'master_volume'.
   * Correct all separated valuables into struct ymf_cards.

   Mon Jun 12 21:36:50 2000   0.1.1

   * Invalid dma setting fixed.
     -- John A. Boyd Jr. <jaboydjr@netwalk.com>

   * ymfsb only compiled as module: fixed
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>

#include "sound_config.h"
#include "soundmodule.h"
#include "sb.h"
#include "724hwmcode.h"

#include "ac97.h"

#undef YMF_DEBUG

/* ---------------------------------------------------------------------- */

#ifndef SOUND_LOCK
# define SOUND_LOCK do {} while (0)
# define SOUND_LOCK_END do {} while (0)
#endif

#ifndef PCI_VENDOR_ID_YAMAHA
# define PCI_VENDOR_ID_YAMAHA  0x1073
#endif
#ifndef PCI_DEVICE_ID_YMF724
# define PCI_DEVICE_ID_YMF724  0x0004
#endif
#ifndef PCI_DEVICE_ID_YMF740
# define PCI_DEVICE_ID_YMF740  0x000A
#endif
#ifndef PCI_DEVICE_ID_YMF740C
# define PCI_DEVICE_ID_YMF740C 0x000C
#endif
#ifndef PCI_DEVICE_ID_YMF724F
# define PCI_DEVICE_ID_YMF724F 0x000D
#endif
#ifndef PCI_DEVICE_ID_YMF744
# define PCI_DEVICE_ID_YMF744  0x0010
#endif
#ifndef PCI_DEVICE_ID_YMF754
# define PCI_DEVICE_ID_YMF754  0x0012
#endif

/* ---------------------------------------------------------------------- */

#define YMFSB_RESET_DELAY               5

#define YMFSB_REGSIZE                   0x8000

#define YMFSB_AC97TIMEOUT               2000

#define	YMFSB_WORKBITTIMEOUT            250000

#define	YMFSB_DSPLENGTH                 0x0080
#define	YMFSB_CTRLLENGTH                0x3000

#define YMFSB_PCIR_VENDORID             0x00
#define YMFSB_PCIR_DEVICEID             0x02
#define YMFSB_PCIR_CMD                  0x04
#define YMFSB_PCIR_REVISIONID           0x08
#define YMFSB_PCIR_BASEADDR             0x10
#define YMFSB_PCIR_IRQ                  0x3c

#define	YMFSB_PCIR_LEGCTRL              0x40
#define	YMFSB_PCIR_ELEGCTRL             0x42
#define	YMFSB_PCIR_DSXGCTRL             0x48
#define YMFSB_PCIR_OPLADR               0x60
#define YMFSB_PCIR_SBADR                0x62
#define YMFSB_PCIR_MPUADR               0x64

#define	YMFSB_INTFLAG                   0x0004
#define	YMFSB_ACTIVITY                  0x0006
#define	YMFSB_GLOBALCTRL                0x0008
#define	YMFSB_ZVCTRL                    0x000A
#define	YMFSB_TIMERCTRL                 0x0010
#define	YMFSB_TIMERCOUNT                0x0012
#define	YMFSB_SPDIFOUTCTRL              0x0018
#define	YMFSB_SPDIFOUTSTATUS            0x001C
#define	YMFSB_EEPROMCTRL                0x0020
#define	YMFSB_SPDIFINCTRL               0x0034
#define	YMFSB_SPDIFINSTATUS             0x0038
#define	YMFSB_DSPPROGRAMDL              0x0048
#define	YMFSB_DLCNTRL                   0x004C
#define	YMFSB_GPIOININTFLAG             0x0050
#define	YMFSB_GPIOININTENABLE           0x0052
#define	YMFSB_GPIOINSTATUS              0x0054
#define	YMFSB_GPIOOUTCTRL               0x0056
#define	YMFSB_GPIOFUNCENABLE            0x0058
#define	YMFSB_GPIOTYPECONFIG            0x005A
#define	YMFSB_AC97CMDDATA               0x0060
#define	YMFSB_AC97CMDADR                0x0062
#define	YMFSB_PRISTATUSDATA             0x0064
#define	YMFSB_PRISTATUSADR              0x0066
#define	YMFSB_SECSTATUSDATA             0x0068
#define	YMFSB_SECSTATUSADR              0x006A
#define	YMFSB_SECCONFIG                 0x0070
#define	YMFSB_LEGACYOUTVOL              0x0080
#define	YMFSB_LEGACYOUTVOLL             0x0080
#define	YMFSB_LEGACYOUTVOLR             0x0082
#define	YMFSB_NATIVEDACOUTVOL           0x0084
#define	YMFSB_NATIVEDACOUTVOLL          0x0084
#define	YMFSB_NATIVEDACOUTVOLR          0x0086
#define	YMFSB_SPDIFOUTVOL               0x0088
#define	YMFSB_SPDIFOUTVOLL              0x0088
#define	YMFSB_SPDIFOUTVOLR              0x008A
#define	YMFSB_AC3OUTVOL                 0x008C
#define	YMFSB_AC3OUTVOLL                0x008C
#define	YMFSB_AC3OUTVOLR                0x008E
#define	YMFSB_PRIADCOUTVOL              0x0090
#define	YMFSB_PRIADCOUTVOLL             0x0090
#define	YMFSB_PRIADCOUTVOLR             0x0092
#define	YMFSB_LEGACYLOOPVOL             0x0094
#define	YMFSB_LEGACYLOOPVOLL            0x0094
#define	YMFSB_LEGACYLOOPVOLR            0x0096
#define	YMFSB_NATIVEDACLOOPVOL          0x0098
#define	YMFSB_NATIVEDACLOOPVOLL         0x0098
#define	YMFSB_NATIVEDACLOOPVOLR         0x009A
#define	YMFSB_SPDIFLOOPVOL              0x009C
#define	YMFSB_SPDIFLOOPVOLL             0x009E
#define	YMFSB_SPDIFLOOPVOLR             0x009E
#define	YMFSB_AC3LOOPVOL                0x00A0
#define	YMFSB_AC3LOOPVOLL               0x00A0
#define	YMFSB_AC3LOOPVOLR               0x00A2
#define	YMFSB_PRIADCLOOPVOL             0x00A4
#define	YMFSB_PRIADCLOOPVOLL            0x00A4
#define	YMFSB_PRIADCLOOPVOLR            0x00A6
#define	YMFSB_NATIVEADCINVOL            0x00A8
#define	YMFSB_NATIVEADCINVOLL           0x00A8
#define	YMFSB_NATIVEADCINVOLR           0x00AA
#define	YMFSB_NATIVEDACINVOL            0x00AC
#define	YMFSB_NATIVEDACINVOLL           0x00AC
#define	YMFSB_NATIVEDACINVOLR           0x00AE
#define	YMFSB_BUF441OUTVOL              0x00B0
#define	YMFSB_BUF441OUTVOLL             0x00B0
#define	YMFSB_BUF441OUTVOLR             0x00B2
#define	YMFSB_BUF441LOOPVOL             0x00B4
#define	YMFSB_BUF441LOOPVOLL            0x00B4
#define	YMFSB_BUF441LOOPVOLR            0x00B6
#define	YMFSB_SPDIFOUTVOL2              0x00B8
#define	YMFSB_SPDIFOUTVOL2L             0x00B8
#define	YMFSB_SPDIFOUTVOL2R             0x00BA
#define	YMFSB_SPDIFLOOPVOL2             0x00BC
#define	YMFSB_SPDIFLOOPVOL2L            0x00BC
#define	YMFSB_SPDIFLOOPVOL2R            0x00BE
#define	YMFSB_ADCSLOTSR                 0x00C0
#define	YMFSB_RECSLOTSR                 0x00C4
#define	YMFSB_ADCFORMAT                 0x00C8
#define	YMFSB_RECFORMAT                 0x00CC
#define	YMFSB_P44SLOTSR                 0x00D0
#define	YMFSB_STATUS                    0x0100
#define	YMFSB_CTRLSELECT                0x0104
#define	YMFSB_MODE                      0x0108
#define	YMFSB_SAMPLECOUNT               0x010C
#define	YMFSB_NUMOFSAMPLES              0x0110
#define	YMFSB_CONFIG                    0x0114
#define	YMFSB_PLAYCTRLSIZE              0x0140
#define	YMFSB_RECCTRLSIZE               0x0144
#define	YMFSB_EFFCTRLSIZE               0x0148
#define	YMFSB_WORKSIZE                  0x014C
#define	YMFSB_MAPOFREC                  0x0150
#define	YMFSB_MAPOFEFFECT               0x0154
#define	YMFSB_PLAYCTRLBASE              0x0158
#define	YMFSB_RECCTRLBASE               0x015C
#define	YMFSB_EFFCTRLBASE               0x0160
#define	YMFSB_WORKBASE                  0x0164
#define	YMFSB_DSPINSTRAM                0x1000
#define	YMFSB_CTRLINSTRAM               0x4000


/* ---------------------------------------------------------------------- */

#define MAX_CARDS	4

#define PFX		"ymf_sb: "

#define YMFSB_VERSION	"0.1.1"
#define YMFSB_CARD_NAME	"YMF7xx Legacy Audio driver " YMFSB_VERSION

#define ymf7xxsb_probe_midi probe_sbmpu
#define ymf7xxsb_attach_midi attach_sbmpu
#define ymf7xxsb_unload_midi unload_sbmpu

/* ---------------------------------------------------------------------- */

static struct ymf_card {
  int                  card;
  unsigned short      *ymfbase;

  struct address_info sb_data, opl3_data, mpu_data;

  struct ac97_hwint   ac97_dev;
  int                 mixer_oss_dev;
} ymf_cards[MAX_CARDS];

static unsigned int cards = 0;

/* ---------------------------------------------------------------------- */

#ifdef MODULE
static int mpu_io     = 0;
static int synth_io   = 0;
static int io         = 0;
static int dma        = 0;
static int spdif_out  = 1;
MODULE_PARM(mpu_io, "i");
MODULE_PARM(synth_io, "i");
MODULE_PARM(io,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(spdif_out,"i");
#else
static int mpu_io     = 0x330;
static int synth_io   = 0x388;
static int io         = 0x220;
static int dma        = 1;
static int spdif_out  = 1;
#endif

/* ---------------------------------------------------------------------- */

static int readRegWord( struct ymf_card *card, int adr ) {

	if (card->ymfbase==NULL) return 0;

	return readw(card->ymfbase+adr/2);
}

static void writeRegWord( struct ymf_card *card, int adr, int val ) {

	if (card->ymfbase==NULL) return;

	writew((unsigned short)(val&0xffff), card->ymfbase + adr/2);

	return;
}

static int readRegDWord( struct ymf_card *card, int adr ) {

	if (card->ymfbase==NULL) return 0;

	return (readl(card->ymfbase+adr/2));
}

static void writeRegDWord( struct ymf_card *card, int adr, int val ) {

	if (card->ymfbase==NULL) return;

	writel((unsigned int)(val&0xffffffff), card->ymfbase+adr/2);

	return;
}

/* ---------------------------------------------------------------------- */

static int checkPrimaryBusy( struct ymf_card *card )
{
	int timeout=0;

	while ( timeout++ < YMFSB_AC97TIMEOUT )
	{
		if ( (readRegWord(card, YMFSB_PRISTATUSADR) & 0x8000) == 0x0000 )
			return 0;
	}
	return -EBUSY;
}

static int checkCodec( struct ymf_card *card, struct pci_dev *pcidev )
{
	u8 tmp8;

	pci_read_config_byte( pcidev, YMFSB_PCIR_DSXGCTRL, &tmp8 );
	if ( tmp8 & 0x03 ) {
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8&0xfc);
		mdelay(YMFSB_RESET_DELAY);
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8|0x03);
		mdelay(YMFSB_RESET_DELAY);
		pci_write_config_byte(pcidev, YMFSB_PCIR_DSXGCTRL, tmp8&0xfc);
		mdelay(YMFSB_RESET_DELAY);
	}

	if ( checkPrimaryBusy( card ) ) return -EBUSY;

	return 0;
}

static int setupLegacyIO( struct ymf_card *card, struct pci_dev *pcidev )
{
	int v;
	int sbio=0, mpuio=0, oplio=0,dma=0;

	switch(card->sb_data.io_base) {
	case 0x220:
		sbio = 0;
		break;
	case 0x240:
		sbio = 1;
		break;
	case 0x260:
		sbio = 2;
		break;
	case 0x280:
		sbio = 3;
		break;
	default:
		return -EINVAL;
		break;
	}
#ifdef YMF_DEBUG
	printk(PFX "set SBPro I/O at 0x%x\n",card->sb_data.io_base);
#endif

	switch(card->mpu_data.io_base) {
	case 0x330:
		mpuio = 0;
		break;
	case 0x300:
		mpuio = 1;
		break;
	case 0x332:
		mpuio = 2;
		break;
	case 0x334:
		mpuio = 3;
		break;
	default:
		mpuio = 0;
		break;
	}
#ifdef YMF_DEBUG
	printk(PFX "set MPU401 I/O at 0x%x\n",card->mpu_data.io_base);
#endif

	switch(card->opl3_data.io_base) {
	case 0x388:
		oplio = 0;
		break;
	case 0x398:
		oplio = 1;
		break;
	case 0x3a0:
		oplio = 2;
		break;
	case 0x3a8:
		oplio = 3;
		break;
	default:
		return -EINVAL;
		break;
	}
#ifdef YMF_DEBUG
	printk(PFX "set OPL3 I/O at 0x%x\n",card->opl3_data.io_base);
#endif

	dma = card->sb_data.dma;
#ifdef YMF_DEBUG
	printk(PFX "set DMA address at 0x%x\n",card->sb_data.dma);
#endif

	v = 0x0000 | ((dma&0x03)<<6) | 0x003f;
	pci_write_config_word(pcidev, YMFSB_PCIR_LEGCTRL, v);
#ifdef YMF_DEBUG
	printk(PFX "LEGCTRL: 0x%x\n",v);
#endif
	switch( pcidev->device ) {
	case PCI_DEVICE_ID_YMF724:
	case PCI_DEVICE_ID_YMF740:
	case PCI_DEVICE_ID_YMF724F:
	case PCI_DEVICE_ID_YMF740C:
		v = 0x8800 | ((mpuio<<4)&0x03) | ((sbio<<2)&0x03) | (oplio&0x03);
		pci_write_config_word(pcidev, YMFSB_PCIR_ELEGCTRL, v);
#ifdef YMF_DEBUG
		printk(PFX "ELEGCTRL: 0x%x\n",v);
#endif
		break;

	case PCI_DEVICE_ID_YMF744:
	case PCI_DEVICE_ID_YMF754:
		v = 0x8800;
		pci_write_config_word(pcidev, YMFSB_PCIR_ELEGCTRL, v);
		pci_write_config_word(pcidev, YMFSB_PCIR_OPLADR, card->opl3_data.io_base);
		pci_write_config_word(pcidev, YMFSB_PCIR_SBADR,  card->sb_data.io_base);
		pci_write_config_word(pcidev, YMFSB_PCIR_MPUADR, card->mpu_data.io_base);
		break;

	default:
		printk(KERN_ERR PFX "Invalid device ID: %d\n",pcidev->device);
		return -EINVAL;
		break;
	}

	return 0;
}

/* ---------------------------------------------------------------------- */
/* AC'97 stuff */
static int ymfsb_readAC97Reg( struct ac97_hwint *dev, u8 reg )
{
	struct ymf_card *card = (struct ymf_card *)dev->driver_private;
	unsigned long flags;
	int ret;

	if ( reg > 0x7f ) return -EINVAL;

	save_flags(flags);
	cli();
	writeRegWord( card, YMFSB_AC97CMDADR, 0x8000 | reg );
	if ( checkPrimaryBusy( card ) ) {
		restore_flags(flags);
		return -EBUSY;
	}
	ret = readRegWord( card, YMFSB_AC97CMDDATA );
	restore_flags(flags);

	return ret;
}

static int ymfsb_writeAC97Reg( struct ac97_hwint *dev, u8 reg, u16 value )
{
	struct ymf_card *card = (struct ymf_card *)dev->driver_private;
	unsigned long flags;

	if ( reg > 0x7f ) return -EINVAL;

	save_flags(flags);
	cli();
	if ( checkPrimaryBusy( card ) ) {
	  restore_flags(flags);
	  return -EBUSY;
	}

	writeRegWord( card, YMFSB_AC97CMDADR, 0x0000 | reg );
	writeRegWord( card, YMFSB_AC97CMDDATA, value );

	restore_flags(flags);
	return 0;
}

struct initialValues
{
    unsigned short port;
    unsigned short value;
};

static struct initialValues ymfsb_ac97_initial_values[] =
{
    { AC97_RESET,             0x0000 },
    { AC97_MASTER_VOL_STEREO, 0x0000 },
    { AC97_HEADPHONE_VOL,     0x8000 },
    { AC97_PCMOUT_VOL,        0x0606 },
    { AC97_MASTER_VOL_MONO,   0x0000 },
    { AC97_PCBEEP_VOL,        0x0000 },
    { AC97_PHONE_VOL,         0x0008 },
    { AC97_MIC_VOL,           0x8000 },
    { AC97_LINEIN_VOL,        0x8808 },
    { AC97_CD_VOL,            0x8808 },
    { AC97_VIDEO_VOL,         0x8808 },
    { AC97_AUX_VOL,           0x8808 },
    { AC97_RECORD_SELECT,     0x0000 },
    { AC97_RECORD_GAIN,       0x0B0B },
    { AC97_GENERAL_PURPOSE,   0x0000 },
    { 0xffff, 0xffff }
};

static int ymfsb_resetAC97( struct ac97_hwint *dev )
{
	int i;

	for ( i=0 ; ymfsb_ac97_initial_values[i].port != 0xffff ; i++ )
	{
		ac97_put_register ( dev,
				    ymfsb_ac97_initial_values[i].port,
				    ymfsb_ac97_initial_values[i].value );
	}

	return 0;
}

static int ymfsb_ac97_mixer_ioctl( int dev, unsigned int cmd, caddr_t arg )
{
	int i;

	for ( i=0 ; i < MAX_CARDS ; i++ )
	{
		if ( ymf_cards[i].mixer_oss_dev == dev ) break;
	}

	if ( i < MAX_CARDS )
		return ac97_mixer_ioctl(&(ymf_cards[i].ac97_dev), cmd, arg);
	else
		return -ENODEV;
}

static struct mixer_operations ymfsb_ac97_mixer_operations = {
	"YAMAHA",
	"YAMAHA PCI",
	ymfsb_ac97_mixer_ioctl
};

static struct ac97_mixer_value_list ymfsb_ac97_mixer_defaults[] = {
    { SOUND_MIXER_VOLUME,  { { 85, 85 } } },
    { SOUND_MIXER_SPEAKER, { { 100 } } },
    { SOUND_MIXER_PCM,     { { 65, 65 } } },
    { SOUND_MIXER_CD,      { { 65, 65 } } },
    { -1,                  {  { 0,  0 } } }
};

/* ---------------------------------------------------------------------- */

static void enableDSP( struct ymf_card *card )
{
	writeRegDWord( card, YMFSB_CONFIG, 0x00000001 );
	return;
}

static void disableDSP( struct ymf_card *card )
{
	int val;
	int i;

	val = readRegDWord( card, YMFSB_CONFIG );
	if ( val ) {
		writeRegDWord( card, YMFSB_CONFIG, 0 );
	}

	i=0;
	while( ++i < YMFSB_WORKBITTIMEOUT ) {
		val = readRegDWord( card, YMFSB_STATUS );
		if ( (val & 0x00000002) == 0x00000000 ) break;
	}

	return;
}

static int setupInstruction( struct ymf_card *card, struct pci_dev *pcidev )
{
	int i;
	int val;

	writeRegDWord( card, YMFSB_NATIVEDACOUTVOL, 0 ); /* mute dac */
	disableDSP( card );

	writeRegDWord( card, YMFSB_MODE, 0x00010000 );

	/* DS-XG Software Reset */
	writeRegDWord( card, YMFSB_MODE,         0x00000000 );
	writeRegDWord( card, YMFSB_MAPOFREC,     0x00000000 );
	writeRegDWord( card, YMFSB_MAPOFEFFECT,  0x00000000 );
	writeRegDWord( card, YMFSB_PLAYCTRLBASE, 0x00000000 );
	writeRegDWord( card, YMFSB_RECCTRLBASE,  0x00000000 );
	writeRegDWord( card, YMFSB_EFFCTRLBASE,  0x00000000 );

	val = readRegWord( card, YMFSB_GLOBALCTRL );
	writeRegWord( card, YMFSB_GLOBALCTRL, (val&~0x0007) );

	/* setup DSP instruction code */
	for ( i=0 ; i<YMFSB_DSPLENGTH ; i+=4 ) {
	  writeRegDWord( card, YMFSB_DSPINSTRAM+i, DspInst[i>>2] );
	}

	switch( pcidev->device ) {
	case PCI_DEVICE_ID_YMF724:
	case PCI_DEVICE_ID_YMF740:
		/* setup Control instruction code */
		for ( i=0 ; i<YMFSB_CTRLLENGTH ; i+=4 ) {
			writeRegDWord( card, YMFSB_CTRLINSTRAM+i, CntrlInst[i>>2] );
		}
		break;

	case PCI_DEVICE_ID_YMF724F:
	case PCI_DEVICE_ID_YMF740C:
	case PCI_DEVICE_ID_YMF744:
	case PCI_DEVICE_ID_YMF754:
		/* setup Control instruction code */
	
		for ( i=0 ; i<YMFSB_CTRLLENGTH ; i+=4 ) {
			writeRegDWord( card, YMFSB_CTRLINSTRAM+i, CntrlInst1E[i>>2] );
		}
		break;

	default:
		return -ENXIO;
	}

	enableDSP( card );

	return 0;
}

/* ---------------------------------------------------------------------- */

static int __init ymf7xx_init( struct ymf_card *card, struct pci_dev *pcidev )
{
	unsigned short v;
	int mixer;

	/* Read hardware information */
#ifdef YMF_DEBUG
	unsigned int   dv;
	pci_read_config_word(pcidev, YMFSB_PCIR_VENDORID, &v);
	printk(KERN_INFO PFX "Vendor ID = 0x%x\n",v);
	pci_read_config_word(pcidev, YMFSB_PCIR_DEVICEID, &v);
	printk(KERN_INFO PFX "Device ID = 0x%x\n",v);
	pci_read_config_word(pcidev, YMFSB_PCIR_REVISIONID, &v);
	printk(KERN_INFO PFX "Revision ID = 0x%x\n",v&0xff);
	pci_read_config_dword(pcidev, YMFSB_PCIR_BASEADDR, &dv);
	printk(KERN_INFO PFX "Base address = 0x%x\n",dv);
	pci_read_config_word(pcidev, YMFSB_PCIR_IRQ, &v);
	printk(KERN_INFO PFX "IRQ line = 0x%x\n",v&0xff);
#endif

	/* enables memory space access / bus mastering */
	pci_read_config_word(pcidev, YMFSB_PCIR_CMD, &v);
	pci_write_config_word(pcidev, YMFSB_PCIR_CMD, v|0x06);

	/* check codec */
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "check codec...\n");
#endif
	if (checkCodec(card, pcidev)) return -EBUSY;

	/* setup legacy I/O */
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup legacy I/O...\n");
#endif
	if (setupLegacyIO(card, pcidev)) return -EBUSY;
	
	/* setup instruction code */	
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup instructions...\n");
#endif
	if (setupInstruction(card, pcidev)) return -EBUSY;

	/* AC'97 setup */	
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup AC'97...\n");
#endif
	if ( ac97_init( &card->ac97_dev ) ) return -EBUSY;

	mixer = sound_alloc_mixerdev();
	if ( num_mixers >= MAX_MIXER_DEV ) return -EBUSY;
	
	mixer_devs[mixer] = &ymfsb_ac97_mixer_operations;
	card->mixer_oss_dev = mixer;
	ac97_set_values( &card->ac97_dev, ymfsb_ac97_mixer_defaults );

#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup Legacy Volume...\n");
#endif
	/* Legacy Audio Output Volume L & R ch */
	writeRegDWord( card, YMFSB_LEGACYOUTVOL, 0x3fff3fff );

#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "setup SPDIF output control...\n");
#endif
	/* SPDIF Output control */
	v = spdif_out != 0 ? 0x0001 : 0x0000;
	writeRegWord( card, YMFSB_SPDIFOUTCTRL, v );
	/* no copyright protection, 
	   sample-rate converted,
	   re-recorded software comercially available (the 1st generation),
	   original */
	writeRegWord( card, YMFSB_SPDIFOUTSTATUS, 0x9a04 );

	return 0;
}

/* ---------------------------------------------------------------------- */

static void __init ymf7xxsb_attach_sb(struct address_info *hw_config)
{
	hw_config->driver_use_1 |= SB_NO_MIXER;
	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;
}

static int __init ymf7xxsb_probe_sb(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_DEBUG PFX "SBPro port 0x%x is already in use\n",
		       hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config, SB_PCI_YAMAHA, 0);
}


static void ymf7xxsb_unload_sb(struct address_info *hw_config, int unload_mpu)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config, unload_mpu);
}

/* ---------------------------------------------------------------------- */

static int __init ymf7xxsb_install (struct pci_dev *pcidev)
{
	struct {
		unsigned short deviceid;
		char           *devicename;
	} devicetable[] = 
	{
		{ PCI_DEVICE_ID_YMF724,  "YMF724A-E" },
		{ PCI_DEVICE_ID_YMF724F, "YMF724F" },
		{ PCI_DEVICE_ID_YMF740,  "YMF740A-B" },
		{ PCI_DEVICE_ID_YMF740C, "YMF740C" },
		{ PCI_DEVICE_ID_YMF744,  "YMF744" },
		{ PCI_DEVICE_ID_YMF754,  "YMF754" },
	};

	char		*devicename = "unknown";
	int		i;
	unsigned long   iobase;
	struct ymf_card *card;

	if ( pcidev->irq == 0 ) return -ENODEV;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,0)
	if ( pci_enable_device(pcidev) ) {
		printk (KERN_ERR PFX "cannot enable PCI device\n");
		return -EIO;
	}
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
	iobase = pcidev->base_address[0]&PCI_BASE_ADDRESS_MEM_MASK;
#else
	iobase = pcidev->resource[0].start&PCI_BASE_ADDRESS_MEM_MASK;
#endif
	if ( iobase == 0x00000000 ) return -ENODEV;

	for ( i=0 ; i<sizeof(devicetable) / sizeof(devicetable[0]); i++ )
	{
		if (devicetable[i].deviceid == pcidev->device)
		{
			devicename = devicetable[i].devicename;
			break;
		}
	}

	card = &ymf_cards[cards];

	/* remap memory mapped I/O onto kernel virtual memory */
	if ( (card->ymfbase = ioremap_nocache(iobase, YMFSB_REGSIZE)) == 0 )
	{
		printk(KERN_ERR PFX "ioremap (0x%lx) returns zero\n", iobase);
		return -ENODEV;
	}
	printk(KERN_INFO PFX "found %s at 0x%lx\n", devicename, iobase);
#ifdef YMF_DEBUG
	printk(KERN_INFO PFX "remappling to 0x%p\n", card->ymfbase);
#endif

	memset (&card->sb_data,   0, sizeof (struct address_info));
	memset (&card->opl3_data, 0, sizeof (struct address_info));
	memset (&card->mpu_data,  0, sizeof (struct address_info));
	memset (&card->ac97_dev,  0, sizeof (struct ac97_hwint));

	card->card = cards;

	card->sb_data.name   = YMFSB_CARD_NAME;
	card->opl3_data.name = YMFSB_CARD_NAME;
	card->mpu_data.name  = YMFSB_CARD_NAME;

	card->sb_data.card_subtype = MDL_YMPCI;

	if ( io == 0 ) io      = 0x220;
	card->sb_data.io_base = io;
	card->sb_data.irq     = pcidev->irq;
	card->sb_data.dma     = dma;

	if ( synth_io == 0 ) synth_io = 0x388;
	card->opl3_data.io_base = synth_io;
	card->opl3_data.irq     = -1;

	if ( mpu_io == 0 ) mpu_io = 0x330;
	card->mpu_data.io_base = mpu_io;
	card->mpu_data.irq     = -1;

	card->ac97_dev.reset_device   = ymfsb_resetAC97;
	card->ac97_dev.read_reg       = ymfsb_readAC97Reg;
	card->ac97_dev.write_reg      = ymfsb_writeAC97Reg;
	card->ac97_dev.driver_private = (void *)card;

	if ( ymf7xx_init(card, pcidev) ) {
		printk (KERN_ERR PFX
			"Cannot initialize %s, aborting\n",
			devicename);
		return -ENODEV;
	}

	/* regist legacy SoundBlaster Pro */
	if (!ymf7xxsb_probe_sb(&card->sb_data)) {
		printk (KERN_ERR PFX
			"SB probe at 0x%X failed, aborting\n",
			io);
		return -ENODEV;
	}
	ymf7xxsb_attach_sb (&card->sb_data);

	/* regist legacy MIDI */
	if ( mpu_io > 0 && 0)
	{
		if (!ymf7xxsb_probe_midi (&card->mpu_data)) {
			printk (KERN_ERR PFX
				"MIDI probe @ 0x%X failed, aborting\n",
				mpu_io);
			ymf7xxsb_unload_sb (&card->sb_data, 0);
			return -ENODEV;
		}
		ymf7xxsb_attach_midi (&card->mpu_data);
	}

	/* regist legacy OPL3 */

	cards++;	
	return 0;
}

static int __init probe_ymf7xxsb (void)
{
	struct pci_dev *pcidev = NULL;
	int i;

	for (i=0 ; i<MAX_CARDS ; i++ )
		ymf_cards[i].ymfbase = NULL;

	while ( pcidev == NULL && (
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF724, pcidev)) ||
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF724F,pcidev)) ||
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF740, pcidev)) ||
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF740C,pcidev)) ||
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF744, pcidev)) ||
	       (pcidev = pci_find_device (PCI_VENDOR_ID_YAMAHA,
					  PCI_DEVICE_ID_YMF754, pcidev)))) {
		  if (ymf7xxsb_install (pcidev)) {
			  printk (KERN_ERR PFX "audio init failed\n");
			  return -ENODEV;
		  }

		  if (cards == MAX_CARDS) {
		  	  printk (KERN_DEBUG PFX "maximum number of cards reached\n");
			  break;
		  }
	}

	return 0;
}

static void free_iomaps( void )
{
	int i;

	for ( i=0 ; i<MAX_CARDS ; i++ ) {
		if ( ymf_cards[i].ymfbase!=NULL )
			iounmap(ymf_cards[i].ymfbase);
	}

	return;
}

int __init init_ymf7xxsb_module(void)
{
	if (!pci_present ()) {
		printk (KERN_DEBUG PFX "PCI not present, exiting\n");
		return -ENODEV;
	}

	if (probe_ymf7xxsb()) {
		printk(KERN_ERR PFX "probe failed, aborting\n");
		/* XXX unload cards registered so far, if any */
		free_iomaps();
		return -ENODEV;
	}

	if (cards == 0) {
		printk(KERN_DEBUG PFX "No chips found, aborting\n");
		free_iomaps();
		return -ENODEV;
	}

	printk (KERN_INFO PFX YMFSB_CARD_NAME " loaded\n");
	
	/*
	 *	Binds us to the sound subsystem	
	 */
	SOUND_LOCK;
	return 0;
}

static void cleanup_ymf7xxsb_module(void)
{
	int i;
	
	for (i = 0; i < cards; i++) {
		ymf7xxsb_unload_sb (&(ymf_cards[i].sb_data), 0);
		ymf7xxsb_unload_midi (&(ymf_cards[i].mpu_data));
		if ( ymf_cards[i].mixer_oss_dev >= 0 ) 
			sound_unload_mixerdev( ymf_cards[i].mixer_oss_dev );
	}

	free_iomaps();

	/*
	 *	Final clean up with the sound layer
	 */
	SOUND_LOCK_END;
}

#ifdef MODULE

MODULE_AUTHOR("Daisuke Nagano, breeze.nagano@nifty.ne.jp");
MODULE_DESCRIPTION("YMF7xx Legacy Audio Driver");

int init_module(void)
{
	return init_ymf7xxsb_module();
}

void cleanup_module(void)
{
	cleanup_ymf7xxsb_module();
}

#endif
