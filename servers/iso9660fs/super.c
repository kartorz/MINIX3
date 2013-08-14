/* Functions to manage the superblock of the filesystem. These functions are
 * are called at the beginning and at the end of the server. */

#include "inc.h"

#include <string.h>
#include <minix/com.h>
#include <minix/u64.h>
#include <minix/bdev.h>

#include "iso_rrip.h"

static int iso_makemp(struct iso_mnt *isomp,char* buf);

/* This function is called when the filesystem is umounted. It releases the 
 * super block. */
int release_v_pri(v_pri)
     register struct iso9660_vd_pri *v_pri;
{
  /* Release the root dir record */
  release_dir_record(v_pri->dir_rec_root);
  if(v_pri->mnt_data) {
	  free(v_pri->mnt_data);
	  v_pri->mnt_data = NULL;
  }

  v_pri->count = 0;
  return OK;
}

/* This function fullfill the super block data structure using the information
 * contained in the stream buf. Such stream is physically read from the device
 * . */
int create_v_pri(v_pri,buf,address)
     register struct iso9660_vd_pri *v_pri;
     register char* buf;
     register unsigned long address;
{
  v_pri->vd_type = buf[0];
  memcpy(v_pri->standard_id,buf + 1,sizeof(v_pri->standard_id));
  v_pri->vd_version = buf[6];
  memcpy(v_pri->system_id,buf + 8,sizeof(v_pri->system_id));
  memcpy(v_pri->volume_id,buf + 40,sizeof(v_pri->volume_id));
  memcpy(&v_pri->volume_space_size_l,buf + 80,
	 sizeof(v_pri->volume_space_size_l));
  memcpy(&v_pri->volume_space_size_m,buf + 84,
	 sizeof(v_pri->volume_space_size_m));
  memcpy(&v_pri->volume_set_size,buf + 120,sizeof(v_pri->volume_set_size));
  memcpy(&v_pri->volume_sequence_number,buf + 124,
	 sizeof(v_pri->volume_sequence_number));
  memcpy(&v_pri->logical_block_size_l,buf + 128,
	 sizeof(v_pri->logical_block_size_l));
  memcpy(&v_pri->logical_block_size_m,buf + 130,
	 sizeof(v_pri->logical_block_size_m));
  memcpy(&v_pri->path_table_size_l,buf + 132,
	 sizeof(v_pri->path_table_size_l));
  memcpy(&v_pri->path_table_size_m,buf + 136,
	 sizeof(v_pri->path_table_size_m));
  memcpy(&v_pri->loc_l_occ_path_table,buf + 140,
	 sizeof(v_pri->loc_l_occ_path_table));
  memcpy(&v_pri->loc_opt_l_occ_path_table,buf + 144,
	 sizeof(v_pri->loc_opt_l_occ_path_table));
  memcpy(&v_pri->loc_m_occ_path_table, buf + 148,
	 sizeof(v_pri->loc_m_occ_path_table));
  memcpy(&v_pri->loc_opt_m_occ_path_table,buf + 152,
	 sizeof(v_pri->loc_opt_m_occ_path_table));
  memcpy(v_pri->volume_set_id,buf + 190,sizeof(v_pri->volume_set_id));
  memcpy(v_pri->publisher_id,buf + 318,sizeof(v_pri->publisher_id));
  memcpy(v_pri->data_preparer_id,buf + 446,sizeof(v_pri->data_preparer_id));
  memcpy(v_pri->application_id,buf + 574,sizeof(v_pri->application_id));
  memcpy(v_pri->copyright_file_id,buf + 702,sizeof(v_pri->copyright_file_id));
  memcpy(v_pri->abstract_file_id,buf + 739,sizeof(v_pri->abstract_file_id));
  memcpy(v_pri->bibl_file_id,buf + 776,sizeof(v_pri->bibl_file_id));
  memcpy(v_pri->volume_cre_date,buf + 813,sizeof(v_pri->volume_cre_date));
  memcpy(v_pri->volume_mod_date,buf + 830,sizeof(v_pri->volume_mod_date));
  memcpy(v_pri->volume_exp_date,buf + 847,sizeof(v_pri->volume_exp_date));
  memcpy(v_pri->volume_eff_date,buf + 864,sizeof(v_pri->volume_eff_date));
  v_pri->file_struct_ver = buf[881];
  return OK;
}

/* This function reads from a ISO9660 filesystem (in the device dev) the
 * super block and saves it in v_pri. */
int read_vds(
  register struct iso9660_vd_pri *v_pri,
  register dev_t dev
)
{
  u64_t offset;
  int vol_ok = FALSE;
  int r;
  static char sbbuf[ISO9660_MIN_BLOCK_SIZE];
  int i = 0;
  struct iso_mnt *isomp = (struct iso_mnt *)0;
  struct buf* bp;
  struct dir_record *dir;

  offset = ((u64_t)(ISO9660_SUPER_BLOCK_POSITION));
  while (!vol_ok && i++<MAX_ATTEMPTS) {

    /* Read the sector of the super block. */
    r = bdev_read(dev, offset, sbbuf, ISO9660_MIN_BLOCK_SIZE, BDEV_NOFLAGS);

    if (r != ISO9660_MIN_BLOCK_SIZE) /* Damaged sector or what? */
      continue;

    if ((sbbuf[0] & BYTE) == VD_PRIMARY) {
	create_v_pri(v_pri,sbbuf,offset); /* copy the buffer in the data structure. */

	/* Create iso mountpoint */
	isomp = malloc(sizeof(*isomp));
	memset(isomp, 0, sizeof(*isomp));

	if(iso_makemp(isomp, sbbuf))
		return EINVAL;
	isomp->im_dev = dev;
	v_pri->mnt_data = isomp; /* Creating root dir will use "v_pri->mnt_data" */

	 /* Initialize cache here, checking Rock Ridge Extension 
	    and creating root directory need cache */
	lmfs_set_blocksize(v_pri->logical_block_size_l, major(fs_dev));
	if(!(bp = get_block(isomp->root_extent)))
		return EINVAL; /* can't get root block */

	/* Check the Rock Ridge Extension support, creating root dir will use "rrip" */
	if ((isomp->rr_skip = cd9660_rrip_offset((struct iso_directory_record *)bp->data, isomp)) != -1)
		isomp->iso_ftype = ISO_FTYPE_RRIP;

	if (!(dir = get_free_dir_record()))
		return EINVAL;
	create_dir_record(dir, bp->data, isomp->root_extent<<isomp->im_bshift);
	v_pri->dir_rec_root = dir;
	dir->d_ino_nr = ROOT_INO_NR;
	v_pri->dir_rec_root->i_mnt = isomp;

	put_block(bp);
    }

    if ((sbbuf[0] & BYTE) == VD_SET_TERM)
      /* I dont need to save anything about it */
      vol_ok = TRUE;

    offset = add64u(offset,ISO9660_MIN_BLOCK_SIZE);
  }

  if (vol_ok == FALSE)
    return EINVAL;		/* If no superblock was found... */
  else
    return OK;			/* otherwise. */
}

/*
 * Make a mount point from a volume descriptor
 */
static int iso_makemp(struct iso_mnt *isomp,char* buf)
{
	struct iso_primary_descriptor *pri;
	int logical_block_size;
	struct iso_directory_record *rootp;

	pri = (struct iso_primary_descriptor *)buf;

	logical_block_size = isonum_723 (pri->logical_block_size);

	if (logical_block_size < DEV_BSIZE || logical_block_size > MAXBSIZE
	    || (logical_block_size & (logical_block_size - 1)) != 0)
		return -1;

	rootp = (struct iso_directory_record *)pri->root_directory_record;

	isomp->logical_block_size = logical_block_size;
	isomp->volume_space_size = isonum_733 (pri->volume_space_size);
	memcpy(isomp->root, rootp, sizeof(isomp->root));
	isomp->root_extent = isonum_733 (rootp->extent);
	isomp->root_size = isonum_733 (rootp->size);
	isomp->im_joliet_level = 0;

	isomp->im_bmask = logical_block_size - 1;
	isomp->im_bshift = 0;
	while ((1 << isomp->im_bshift) < isomp->logical_block_size)
		isomp->im_bshift++;

	/* Set some default values */
	isomp->iso_ftype = ISO_FTYPE_9660;
	isomp->im_flags = ISOFSMNT_EXTATT;
	return 0;
}
