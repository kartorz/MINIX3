#include "inc.h"
#include <minix/com.h>
#include <minix/vfsif.h>
#include <fcntl.h>
#include <stddef.h>
#include "buf.h"
#include "iso_rrip.h"

static char getdents_buf[GETDENTS_BUFSIZ];


/*===========================================================================*
 *				fs_read					     *
 *===========================================================================*/
int fs_read(void) {
  int r, chunk, block_size;
  int nrbytes;
  cp_grant_id_t gid;
  off_t position, f_size, bytes_left;
  unsigned int off, cum_io;
  int completed;
  struct dir_record *dir;
  int rw;

  switch(fs_m_in.m_type) {
  	case REQ_READ: rw = READING;
        break;
	case REQ_PEEK: rw = PEEKING;
        break;
	default: panic("odd m_type");
  }

  r = OK;

  /* Try to get inode according to its index */
  dir = get_dir_record(fs_m_in.REQ_INODE_NR);
  if (dir == NULL) return(EINVAL); /* no inode found */

  position = fs_m_in.REQ_SEEK_POS_LO; 
  nrbytes = (unsigned) fs_m_in.REQ_NBYTES; /* number of bytes to read */
  block_size = v_pri.logical_block_size_l;
  gid = fs_m_in.REQ_GRANT;
  f_size = dir->d_file_size;

  rdwt_err = OK;		/* set to EIO if disk error occurs */

  cum_io = 0;
  /* Split the transfer into chunks that don't span two blocks. */
  while (nrbytes != 0) {
	off = (unsigned int) (position % block_size);

	chunk = MIN(nrbytes, block_size - off);
	if (chunk < 0) chunk = block_size - off;

	bytes_left = f_size - position;
	if (position >= f_size) break;	/* we are beyond EOF */
	if (chunk > bytes_left) chunk = (int) bytes_left;

	/* Read or write 'chunk' bytes. */
	r = read_chunk(dir, ((u64_t)(position)), off, chunk, (unsigned) nrbytes, 
		       gid, cum_io, block_size, &completed, rw);

	if (r != OK) break;	/* EOF reached */
	if (rdwt_err < 0) break;

	/* Update counters and pointers. */
	nrbytes -= chunk;	/* bytes yet to be read */
	cum_io += chunk;	/* bytes read so far */
	position += chunk;	/* position within the file */
  }

  fs_m_out.RES_SEEK_POS_LO = position;

  if (rdwt_err != OK) r = rdwt_err;	/* check for disk error */
  if (rdwt_err == END_OF_FILE) r = OK;

  fs_m_out.RES_NBYTES = cum_io; /*dir->d_file_size;*/
  release_dir_record(dir);

  return(r);
}


/*===========================================================================*
 *				fs_bread				     *
 *===========================================================================*/
int fs_bread(void)
{
  int r, rw_flag, chunk, block_size;
  cp_grant_id_t gid;
  int nrbytes;
  u64_t position;
  unsigned int off, cum_io;
  int completed;
  struct dir_record *dir;

  r = OK;

  rw_flag = (fs_m_in.m_type == REQ_BREAD ? READING : WRITING);
  gid = fs_m_in.REQ_GRANT;
  position = make64(fs_m_in.REQ_SEEK_POS_LO, fs_m_in.REQ_SEEK_POS_HI);
  nrbytes = (unsigned) fs_m_in.REQ_NBYTES;
  block_size = v_pri.logical_block_size_l;
  dir = v_pri.dir_rec_root;

  if(rw_flag == WRITING) return (EIO);	/* Not supported */
  rdwt_err = OK;		/* set to EIO if disk error occurs */

  cum_io = 0;
  /* Split the transfer into chunks that don't span two blocks. */
  while (nrbytes != 0) {
    off = rem64u(position, block_size);	/* offset in blk*/

    chunk = MIN(nrbytes, block_size - off);
    if (chunk < 0) chunk = block_size - off;

	/* Read 'chunk' bytes. */
    r = read_chunk(dir, position, off, chunk, (unsigned) nrbytes,
		   gid, cum_io, block_size, &completed, READING);

    if (r != OK) break;	/* EOF reached */
    if (rdwt_err < 0) break;

    /* Update counters and pointers. */
    nrbytes -= chunk;	        /* bytes yet to be read */
    cum_io += chunk;	        /* bytes read so far */
    position= add64ul(position, chunk);	/* position within the file */
  }

  fs_m_out.RES_SEEK_POS_LO = ex64lo(position);
  fs_m_out.RES_SEEK_POS_HI = ex64hi(position);

  if (rdwt_err != OK) r = rdwt_err;	/* check for disk error */
  if (rdwt_err == END_OF_FILE) r = OK;

  fs_m_out.RES_NBYTES = cum_io;

  return(r);
}


/*===========================================================================*
 *				fs_getdents				     *
 *===========================================================================*/
int fs_getdents(void) {
  struct dir_record *dir;
  ino_t ino;
  cp_grant_id_t gid;
  size_t block_size;
  off_t pos, block_pos, block, cur_pos;
  size_t size, tmpbuf_offset, userbuf_off;
  struct buf *bp;
  struct iso_directory_record *ep;
  struct dirent *dirp;
  int r,done,o,len,reclen;
  char *cp;
  char name[ISO_MAXNAMLEN + 1]; /* append '\0' at the end*/
  u16_t namelen; /* exclude '\0' */
  struct iso_mnt *imp;

  /* Initialize the tmp arrays */
  memset(name,'\0', ISO_MAXNAMLEN+1);

  /* Get input parameters */
  ino = fs_m_in.REQ_INODE_NR;
  gid = fs_m_in.REQ_GRANT;
  pos = fs_m_in.REQ_SEEK_POS_LO;
  size = (size_t) fs_m_in.REQ_MEM_SIZE;

  block_size = v_pri.logical_block_size_l;
  cur_pos = pos;		/* The current position */
  tmpbuf_offset = 0;
  userbuf_off = 0;
  memset(getdents_buf, '\0', GETDENTS_BUFSIZ);	/* Avoid leaking any data */

  if ((dir = get_dir_record(ino)) == NULL) return(EINVAL);

  if(!dir->length)
	  return EINVAL;

  block = dir->loc_extent;	/* First block of the directory */
  block += pos / block_size; 	/* Shift to the block where start to read */
  done = FALSE;

  imp = dir->i_mnt;
  while (cur_pos < dir->d_file_size) {
	bp = get_block(block);	/* Get physical block */
	if (bp == NULL) {
		release_dir_record(dir);
		return(EINVAL);
	}

	block_pos = cur_pos % block_size; /* Position where to start read */

	while (block_pos < block_size) {

		ep = (struct iso_directory_record *)(b_data(bp) + block_pos);
		if (isonum_711(ep->length) == 0) 
			break;	/*EOF of a block record, or of this direcotry record */

		if (isonum_711(ep->flags)&2)
			ino = isodirino(ep, imp);
		else
			ino = block*block_size + block_pos;

		/* The dir record is valid. Copy data... */
		switch (imp->iso_ftype) {
		case ISO_FTYPE_RRIP:
			cd9660_rrip_getname(ep, name, &namelen,
				     &ino, imp);
			break;
		default:	/* ISO_FTYPE_DEFAULT || ISO_FTYPE_9660 */
			isofntrans(ep->name, isonum_711(ep->name_len),
				   name, &namelen,
				   0,/*imp->iso_ftype == ISO_FTYPE_9660,*/
				   (imp->im_flags & ISOFSMNT_NOCASETRANS) == 0,
				   isonum_711(ep->flags)&4,
				   imp->im_joliet_level);
			switch (name[0]) {
			case 0:
				strlcpy(name, ".", ISO_MAXNAMLEN);
				namelen = 1;
				break;
			case 1:
				strlcpy(name, "..", ISO_MAXNAMLEN);
				namelen = 2;
				break;
			}
		}

		/* Compute record length */
		reclen = offsetof(struct dirent, d_name) + namelen + 1;
		o = (reclen % sizeof(long));
		if (o != 0)
			reclen += sizeof(long) - o;

		/* Check user buffer */
		if (userbuf_off + tmpbuf_offset + reclen >= size) {
			done = TRUE;
			break; /* drop this record because user has no more space for one more record */
		}

		/* If the new record does not fit, then copy the buffer
		 * and start from the beginning. */
		if (tmpbuf_offset + reclen > GETDENTS_BUFSIZ) {
			r = sys_safecopyto(VFS_PROC_NR, gid, userbuf_off, 
					   (vir_bytes)getdents_buf, tmpbuf_offset);

			if (r != OK)
				panic("fs_getdents: sys_safecopyto failed: %d", r);
			userbuf_off += tmpbuf_offset;
			tmpbuf_offset= 0;
		}

		/* The standard data structure is created using the
		 * data in the buffer. */
		dirp = (struct dirent *) &getdents_buf[tmpbuf_offset];
		dirp->d_ino = ino;
		dirp->d_off= cur_pos;
		dirp->d_reclen= reclen;
		memcpy(dirp->d_name, name, namelen);
		dirp->d_name[namelen]= '\0';

		tmpbuf_offset += reclen;
		cur_pos += isonum_711(ep->length);
		block_pos += isonum_711(ep->length);
	}

	put_block(bp);		/* release the block */
	if (done == TRUE) break;

	cur_pos += (block_size - cur_pos%block_size);
	block++;			/* read the next one */
  }

  if (tmpbuf_offset != 0) {
	r = sys_safecopyto(VFS_PROC_NR, gid, userbuf_off,
			   (vir_bytes) getdents_buf, tmpbuf_offset);
	if (r != OK)
		panic("fs_getdents: sys_safecopyto failed: %d", r);

	userbuf_off += tmpbuf_offset;
  }

  fs_m_out.RES_NBYTES = userbuf_off;
  fs_m_out.RES_SEEK_POS_LO = cur_pos;

  release_dir_record(dir);		/* release the inode */
  return(OK);
}


/*===========================================================================*
 *				read_chunk				     *
 *===========================================================================*/
int read_chunk(dir, position, off, chunk, left, gid, buf_off, block_size, completed, rw)
register struct dir_record *dir;/* pointer to inode for file to be rd/wr */
u64_t position;			/* position within file to read or write */
unsigned off;			/* off within the current block */
int chunk;			/* number of bytes to read or write */
unsigned left;			/* max number of bytes wanted after position */
cp_grant_id_t gid;		/* grant */
unsigned buf_off;		/* offset in grant */
int block_size;			/* block size of FS operating on */
int *completed;			/* number of bytes copied */
int rw;				/* READING or PEEKING */
{

  register struct buf *bp;
  register int r = OK;
  block_t b;
  int file_unit, rel_block, offset;

  *completed = 0;

  if ((ex64lo(position) <= dir->d_file_size) && 
				(ex64lo(position) > dir->data_length)) {
    while ((dir->d_next != NULL) && (ex64lo(position) > dir->data_length)) {
      position = sub64ul(position, dir->data_length);
      dir = dir->d_next;
    }
  }

  if (dir->inter_gap_size != 0) {
    rel_block = div64u(position, block_size);
    file_unit = rel_block / dir->data_length;
    offset = rel_block % dir->file_unit_size;
    b = dir->loc_extent + (dir->file_unit_size +
    				 dir->inter_gap_size) * file_unit + offset;
  } else {
    b = dir->loc_extent + div64u(position, block_size); /* Physical position
							    * to read. */
  }

  bp = get_block(b);

  /* In all cases, bp now points to a valid buffer. */
  if (bp == NULL) {
    panic("bp not valid in rw_chunk; this can't happen");
  }

  if(rw == READING) {
 	 r = sys_safecopyto(VFS_PROC_NR, gid, buf_off,
		     (vir_bytes) (b_data(bp)+off), (phys_bytes) chunk);
  }

  put_block(bp);

  return(r);
}
