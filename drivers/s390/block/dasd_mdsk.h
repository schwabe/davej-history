#ifndef DASD_MDSK_H
#define DASD_MDSK_H

#define MDSK_WRITE_REQ 0x01
#define MDSK_READ_REQ  0x02

#define INIT_BIO        0x00
#define RW_BIO          0x01
#define TERM_BIO        0x02

#define DEV_CLASS_FBA   0x01
#define DEV_CLASS_ECKD   0x02	/* sure ?? */
#define DEV_CLASS_CKD   0x04	/* sure ?? */

#define MDSK_BIOS(x) (2*(x))

typedef struct mdsk_dev_char_t {
	u8 type;
	u8 status;
	u16 spare1;
	u32 block_number;
	u32 alet;
	u32 buffer;
} __attribute__ ((packed, aligned (8))) 

mdsk_bio_t;

typedef struct {
	u16 dev_nr;
	u16 spare1[11];
	u32 block_size;
	u32 offset;
	u32 start_block;
	u32 end_block;
	u32 spare2[6];
} __attribute__ ((packed, aligned (8))) 

mdsk_init_io_t;

typedef struct {
	u16 dev_nr;
	u16 spare1[11];
	u8 key;
	u8 flags;
	u16 spare2;
	u32 block_count;
	u32 alet;
	u32 bio_list;
	u32 interrupt_params;
	u32 spare3[5];
} __attribute__ ((packed, aligned (8))) 

mdsk_rw_io_t;

typedef struct {
	long vdev;
	long size;
	long offset;
	long blksize;
	int force_mdsk;
} mdsk_setup_data_t;

#endif
