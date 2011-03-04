/*	dirent.h - Declarations for directory reading routines.
 *							Author: Kees J. Bot
 *								24 Apr 1989
 *
 * Note: The V7 format directory entries used under Minix must be transformed
 * into a struct dirent with a d_name of at least 15 characters.  Given that
 * we have to transform V7 entries anyhow it is little trouble to let the
 * routines understand the so-called "flex" directory format too.
 */

#ifndef _DIRENT_H
#define _DIRENT_H

#include <sys/cdefs.h>
#include <minix/dir.h>

/* _fl_direct is a flexible directory entry.  Actually it's a union of 8
 * characters and the 3 fields defined below.
 */

/* Flexible directory entry: */
struct _fl_direct {		/* First slot in an entry */
	ino_t		d_ino;
	unsigned char	d_extent;
	char		d_name[3];  /* two characters for the shortest name */
};

	/* Name of length len needs _EXTENT(len) extra slots. */
#define _EXTENT(len)	(((len) + 5) >> 3)

/* Version 7 directory entry: */
struct _v7_direct {
	ino_t		d_ino;
	char		d_name[DIRSIZ];
};

/* The block size must be at least 1024 bytes, because otherwise
 * the superblock (at 1024 bytes) overlaps with other filesystem data.
 */
#define _MIN_BLOCK_SIZE		 1024

/* The below is allocated in some parts of the system as the largest
 * a filesystem block can be. For instance, the boot monitor allocates
 * 3 of these blocks and has to fit within 64kB, so this can't be
 * increased without taking that into account.
 */
#define _MAX_BLOCK_SIZE		 4096

/* This is the block size for the fixed versions of the filesystem (V1/V2) */
#define _STATIC_BLOCK_SIZE	1024

#define _STATIC_FLEX_PER_BLOCK (_STATIC_BLOCK_SIZE/sizeof(struct _fl_direct))
#define _FLEX_PER_V7 (_EXTENT(DIRSIZ) + 1)
#define _FLEX_PER_BLOCK (_STATIC_BLOCK_SIZE/sizeof(struct _fl_direct))

#endif /* _DIRENT_H */
