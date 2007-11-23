#ifndef _ISO_FS_I
#define _ISO_FS_I

struct extent_str
{
	struct extent_str *next;
	unsigned int extent[2];
	unsigned int extent_size[2];
};

/*
 * iso fs inode data in memory
 */
struct iso_inode_info {
	struct extent_str *i_first_extent;
	unsigned int i_backlink;
	unsigned char i_file_format;
	unsigned long i_next_section_ino;
	off_t i_section_size;
};

#endif
