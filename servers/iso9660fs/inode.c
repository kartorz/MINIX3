
/* This file contains all the function that handle the dir records
 * (inodes) for the ISO9660 filesystem.*/

#include "inc.h"
#include "buf.h"

#include <minix/vfsif.h>
#include <sys/stat.h>

#include "iso_rrip.h"

/*===========================================================================*
 *				fs_putnode				     *
 *===========================================================================*/
int fs_putnode()
{
/* Find the inode specified by the request message and decrease its counter. */
  int count;
  struct dir_record *dir = NULL;

  dir = get_dir_record(fs_m_in.REQ_INODE_NR);
  release_dir_record(dir);

  count = fs_m_in.REQ_COUNT;

  if (count <= 0) return(EINVAL);

  if (count > dir->d_count) {
     printf("put_inode: count too high: %d > %d\n", count, dir->d_count);
     return(EINVAL);
  }

  if (dir->d_count > 1)
	dir->d_count = dir->d_count - count + 1;/*Keep at least one reference*/

  release_dir_record(dir); /* Actual inode release, might be last reference */

  return(OK);
}


/*===========================================================================*
 *				release_dir_record			     *
 *===========================================================================*/
int release_dir_record(dir)
struct dir_record *dir;
{
/* Release a dir record (decrement the counter) */
  if (dir == NULL)
	return(EINVAL);

  if (--dir->d_count == 0) {
	if (dir->ext_attr != NULL)
		dir->ext_attr->count = 0;
	dir->ext_attr = NULL;
	dir->d_mountpoint = FALSE;

	dir->d_prior = NULL;
	if (dir->d_next != NULL)
		release_dir_record(dir);
	dir->d_next = NULL;
	dir->i_mnt = NULL;
  }

  return(OK);
}


/*===========================================================================*
 *				get_free_dir_record			     *
 *===========================================================================*/
struct dir_record *get_free_dir_record(void)
{
/* Get a free dir record */
  struct dir_record *dir;

  for(dir = dir_records; dir < &dir_records[NR_ATTR_RECS]; dir++) {
	if (dir->d_count == 0) {	/* The record is free */
		dir->d_count = 1;		/* Set count to 1 */
		dir->ext_attr = NULL;
		return(dir);
	}
  }

  return(NULL);
}


/*===========================================================================*
 *				get_dir_record				     *
 *===========================================================================*/
struct dir_record *get_dir_record(id_dir_record)
ino_t id_dir_record;
{
  struct dir_record *dir = NULL;
  u32_t address;
  int i;

  /* Search through the cache if the inode is still present */
  for(i = 0; i < NR_DIR_RECORDS && dir == NULL; ++i) {
	if (dir_records[i].d_ino_nr == id_dir_record
	    		&& dir_records[i].d_count > 0) {
		dir = dir_records + i;
		dir->d_count++;
	}
  }

  if (dir == NULL) {
	address = (u32_t)id_dir_record;
	dir = load_dir_record_from_disk(address);
	dir->d_ino_nr = id_dir_record;
  }

  if (dir == NULL) return(NULL);

  return(dir);
}


/*===========================================================================*
 *				get_free_ext_attr				     *
 *===========================================================================*/
struct ext_attr_rec *get_free_ext_attr(void) {
/* Get a free extended attribute structure */
  struct ext_attr_rec *dir;
  for(dir = ext_attr_recs; dir < &ext_attr_recs[NR_ATTR_RECS]; dir++) {
	if (dir->count == 0) {	/* The record is free */
		dir->count = 1;
		return(dir);
	}
  }

  return(NULL);
}


/*===========================================================================*
 *				create_ext_attr				     *
 *===========================================================================*/
int create_ext_attr(struct ext_attr_rec *ext,char *buffer)
{
/* Fill an extent structure from the data read on the device */
  if (ext == NULL) return(EINVAL);

  /* In input we have a stream of bytes that are physically read from the
   * device. This stream of data is copied to the data structure. */
  memcpy(&ext->own_id,buffer,sizeof(u32_t));
  memcpy(&ext->group_id,buffer + 4,sizeof(u32_t));
  memcpy(&ext->permissions,buffer + 8,sizeof(u16_t));
  memcpy(&ext->file_cre_date,buffer + 10,ISO9660_SIZE_VOL_CRE_DATE);
  memcpy(&ext->file_mod_date,buffer + 27,ISO9660_SIZE_VOL_MOD_DATE);
  memcpy(&ext->file_exp_date,buffer + 44,ISO9660_SIZE_VOL_EXP_DATE);
  memcpy(&ext->file_eff_date,buffer + 61,ISO9660_SIZE_VOL_EFF_DATE);
  memcpy(&ext->rec_format,buffer + 78,sizeof(u8_t));
  memcpy(&ext->rec_attrs,buffer + 79,sizeof(u8_t));
  memcpy(&ext->rec_length,buffer + 80,sizeof(u32_t));
  memcpy(&ext->system_id,buffer + 84,ISO9660_SIZE_SYS_ID);
  memcpy(&ext->system_use,buffer + 116,ISO9660_SIZE_SYSTEM_USE);
  memcpy(&ext->ext_attr_rec_ver,buffer + 180,sizeof(u8_t));
  memcpy(&ext->len_esc_seq,buffer + 181,sizeof(u8_t));

  return(OK);
}


/*===========================================================================*
 *				create_dir_record 				     *
 *===========================================================================*/
int create_dir_record(dir, buffer, address)
struct dir_record *dir;
char *buffer;
u32_t address;
{
	/* Fills a dir record structure from the data read on the device */
	/* If the flag assign id is active it will return the id associated;
	 * otherwise it will return OK. 
	 */
	struct iso_directory_record *isodir =
		(struct iso_directory_record *)buffer;

	if (dir == NULL)
		return(EINVAL);

	dir->length = isonum_711(isodir->length);
	if(dir->length == 0)
	  return(OK); /* Why? this means a end for a directory area.
		         caller should take care of this situation
			 releasing direcory record. */

	/* The data structure dir record is filled with the stream of data
	* that is read. */
	dir->ext_attr_rec_length = isonum_711(isodir->ext_attr_length);
	dir->loc_extent = isonum_733(isodir->extent);
	dir->data_length = isonum_733(isodir->size);
	dir->file_flags = isonum_711(isodir->flags);
	dir->file_unit_size = isonum_711(isodir->file_unit_size);
	dir->inter_gap_size = isonum_711(isodir->interleave);
	dir->vol_seq_number = isonum_723(isodir->volume_sequence_number);
	dir->length_file_id = isonum_711(isodir->name_len);
	memcpy(dir->file_id, buffer + 33, dir->length_file_id);
	dir->ext_attr = NULL;

	dir->d_mountpoint = FALSE;
	dir->d_next = NULL;
	dir->d_prior = NULL;
	dir->d_file_size = dir->data_length;
	dir->d_phy_addr = address;

	dir->i_mnt = GET_VPRIISOMNT();
	switch (dir->i_mnt->iso_ftype) {
	default:
		cd9660_defattr((struct iso_directory_record *) buffer,
			       dir, NULL);
		cd9660_deftstamp((struct iso_directory_record *)buffer,
				 dir, NULL);
		break;
	case ISO_FTYPE_RRIP:
		cd9660_rrip_analyze((struct iso_directory_record *)buffer,
				    dir, dir->i_mnt);
		break;
	}

	return(OK);
}


/*===========================================================================*
 *			load_dir_record_from_disk			     *
 *===========================================================================*/
struct dir_record *load_dir_record_from_disk(address)
u32_t address;
{
/* This function load a particular dir record from a specific address
 * on the device */

  int block_nr, offset, block_size, new_pos;
  struct buf *bp;
  struct dir_record *dir, *dir_next, *dir_parent, *dir_tmp;
  char name[NAME_MAX + 1];
  char old_name[NAME_MAX + 1];
  u32_t new_address, size;

  block_size = v_pri.logical_block_size_l; /* Block size */
  block_nr = address / block_size; /* Block number from the address */
  offset = address % block_size; /* Offset starting from the block */

  bp = get_block(block_nr);	/* Read the block from the device */
  if (bp == NULL)
	return(NULL);

  dir = get_free_dir_record();	/* Get a free record */
  if (dir == NULL)
	return(NULL);

  /* Fill the dir record with the data read from the device */
  create_dir_record(dir,b_data(bp) + offset, address);

  /* In case the file is composed of more file sections, load also the
   * next section into the structure */
  new_pos = offset + dir->length;
  dir_parent = dir;
  new_address = address + dir->length;
  while (new_pos < block_size) {
	dir_next = get_free_dir_record();
	create_dir_record(dir_next, b_data(bp) + new_pos, new_address);

	if (dir_next->length > 0) {
		strncpy(name,dir_next->file_id,dir_next->length_file_id);
		name[dir_next->length_file_id] = '\0';
		strncpy(old_name, dir_parent->file_id,
			dir_parent->length_file_id);
		old_name[dir_parent->length_file_id] = '\0';
		if (strcmp(name, old_name) == 0) {
			dir_parent->d_next = dir_next;
			dir_next->d_prior = dir_parent;

			/* Link the dir records */
			dir_tmp = dir_next;
			size = dir_tmp->data_length;

			/* Update the file size */
			while (dir_tmp->d_prior != NULL) {
				dir_tmp = dir_tmp->d_prior;
				size += dir_tmp->data_length;
				dir_tmp->d_file_size = size;
			}

			new_pos += dir_parent->length;
			new_address += dir_next->length;
			dir_parent = dir_next;
		} else {			/* This is another inode. */
			release_dir_record(dir_next);
			new_pos = block_size;
		}
	} else {				/* record not valid */
		release_dir_record(dir_next);
		new_pos = block_size;		/* Exit from the while */
	}
  }

  put_block(bp);		/* Release the block read. */
  return(dir);
}

/*
 * File attributes
 */
void cd9660_defattr(struct iso_directory_record *isodir,
		 struct dir_record *inop, struct buf *bp)
{
	struct buf *bp2 = NULL;
	struct iso_mnt *imp = inop->i_mnt;
	struct iso_extended_attributes *ap = NULL;

	if (isonum_711(isodir->flags)&2) {
		inop->inode.iso_mode = S_IFDIR;
		/*
		 * If we return 2, fts() will assume there are no subdirectories
		 * (just links for the path and .), so instead we return 1.
		 */
		inop->inode.iso_links = 1;
	} else {
		inop->inode.iso_mode = S_IFREG;
		inop->inode.iso_links = 1;
	}

	if (!bp
	    && (imp->im_flags & ISOFSMNT_EXTATT)
	    && (isonum_711(isodir->ext_attr_length))) {
		bp2 = get_block(isonum_733(isodir->extent));
		bp = bp2;
	}

	if(bp) {
		ap = (struct iso_extended_attributes *)(bp->data);

		if (isonum_711(ap->version) == 1) {
			if (!(ap->perm[1]&0x10))
				inop->inode.iso_mode |= S_IRUSR;
			if (!(ap->perm[1]&0x40))
				inop->inode.iso_mode |= S_IXUSR;
			if (!(ap->perm[0]&0x01))
				inop->inode.iso_mode |= S_IRGRP;
			if (!(ap->perm[0]&0x04))
				inop->inode.iso_mode |= S_IXGRP;
			if (!(ap->perm[0]&0x10))
				inop->inode.iso_mode |= S_IROTH;
			if (!(ap->perm[0]&0x40))
				inop->inode.iso_mode |= S_IXOTH;
			inop->inode.iso_uid = isonum_723(ap->owner); /* what about 0? */
			inop->inode.iso_gid = isonum_723(ap->group); /* what about 0? */
		} else
			ap = NULL;
	}
	if (!ap) {
		inop->inode.iso_mode |=
		    S_IRUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH;
		inop->inode.iso_uid = (uid_t)0;
		inop->inode.iso_gid = (gid_t)0;
	}

	if(bp2)
		put_block(bp2);
}

/*
 * Time stamps
 */
void cd9660_deftstamp(struct iso_directory_record *isodir, struct dir_record *inop,
	struct buf *bp)
{
	struct buf *bp2 = NULL;
	struct iso_mnt *imp = inop->i_mnt;
	struct iso_extended_attributes *ap = NULL;

	if (!bp
	    && (imp->im_flags & ISOFSMNT_EXTATT)
	    && (isonum_711(isodir->ext_attr_length))) {
		bp2 = get_block(isonum_733(isodir->extent));
		bp = bp2;
	}

	if (bp) {
		ap = (struct iso_extended_attributes *)(bp->data);

		if (isonum_711(ap->version) == 1) {
			if (!cd9660_tstamp_conv17(ap->ftime,&inop->inode.iso_atime))
				cd9660_tstamp_conv17(ap->ctime,&inop->inode.iso_atime);
			if (!cd9660_tstamp_conv17(ap->ctime,&inop->inode.iso_ctime))
				inop->inode.iso_ctime = inop->inode.iso_atime;
			if (!cd9660_tstamp_conv17(ap->mtime,&inop->inode.iso_mtime))
				inop->inode.iso_mtime = inop->inode.iso_ctime;
		} else
			ap = NULL;
	}
	if (!ap) {
		cd9660_tstamp_conv7(isodir->date,&inop->inode.iso_ctime);
		inop->inode.iso_atime = inop->inode.iso_ctime;
		inop->inode.iso_mtime = inop->inode.iso_ctime;
	}

	if(bp2)
		put_block(bp2);
}

int cd9660_tstamp_conv7(const u8_t *pi, struct timespec *pu)
{
	int crtime, days;
	int y, m, d, hour, minute, second, tz;

	y = pi[0] + 1900;
	m = pi[1];
	d = pi[2];
	hour = pi[3];
	minute = pi[4];
	second = pi[5];
	tz = pi[6];

	if (y < 1970) {
		pu->tv_sec  = 0;
		pu->tv_nsec = 0;
		return 0;
	} else {
#ifdef	ORIGINAL
		/* computes day number relative to Sept. 19th,1989 */
		/* don't even *THINK* about changing formula. It works! */
		days = 367*(y-1980)-7*(y+(m+9)/12)/4-3*((y+(m-9)/7)/100+1)/4+275*m/9+d-100;
#else
		/*
		 * Changed :-) to make it relative to Jan. 1st, 1970
		 * and to disambiguate negative division
		 */
		days = 367*(y-1960)-7*(y+(m+9)/12)/4-3*((y+(m+9)/12-1)/100+1)/4+275*m/9+d-239;
#endif
		crtime = ((((days * 24) + hour) * 60 + minute) * 60) + second;

		/* timezone offset is unreliable on some disks */
		if (-48 <= tz && tz <= 52)
			crtime -= tz * 15 * 60;
	}
	pu->tv_sec  = crtime;
	pu->tv_nsec = 0;
	return 1;
}

static u_int cd9660_chars2ui(const u_char *begin, int len)
{
	u_int rc;

	for (rc = 0; --len >= 0;) {
		rc *= 10;
		rc += *begin++ - '0';
	}
	return rc;
}

int cd9660_tstamp_conv17(const u8_t *pi, struct timespec *pu)
{
	u_char tbuf[7];

	/* year:"0001"-"9999" -> -1900  */
	tbuf[0] = cd9660_chars2ui(pi,4) - 1900;

	/* month: " 1"-"12"      -> 1 - 12 */
	tbuf[1] = cd9660_chars2ui(pi + 4,2);

	/* day:   " 1"-"31"      -> 1 - 31 */
	tbuf[2] = cd9660_chars2ui(pi + 6,2);

	/* hour:  " 0"-"23"      -> 0 - 23 */
	tbuf[3] = cd9660_chars2ui(pi + 8,2);

	/* minute:" 0"-"59"      -> 0 - 59 */
	tbuf[4] = cd9660_chars2ui(pi + 10,2);

	/* second:" 0"-"59"      -> 0 - 59 */
	tbuf[5] = cd9660_chars2ui(pi + 12,2);

	/* difference of GMT */
	tbuf[6] = pi[16];

	return cd9660_tstamp_conv7(tbuf,pu);
}

ino_t isodirino(struct iso_directory_record *isodir, struct iso_mnt *imp)
{
	ino_t ino;

	ino = (isonum_733(isodir->extent) + isonum_711(isodir->ext_attr_length))
	      << imp->im_bshift;
	return (ino);
}
