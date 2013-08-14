#include "inc.h"
#include <assert.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <minix/com.h>
#include <string.h>
#include <time.h>

#include <minix/vfsif.h>


/*===========================================================================*
 *				stat_dir_record				     *
 *===========================================================================*/
static int stat_dir_record(
  register struct dir_record *dir,	/* pointer to dir record to stat */
  int pipe_pos,   		/* position in a pipe, supplied by fstat() */
  endpoint_t who_e,		/* Caller endpoint */
  cp_grant_id_t gid		/* grant for the stat buf */
)
{
/* This function returns all the info about a particular inode. It's missing
 * the recording date because of a bug in the standard functions stdtime.
 * Once the bug is fixed the function can be called inside this function to
 * return the date. */

/* Common code for stat and fstat system calls. */
  struct stat statbuf;
  int r;
  struct tm ltime;
  time_t time1;
  u32_t blocks, block_size;

  block_size = GET_VPRIISOMNT()->logical_block_size;
  blocks = (dir->d_file_size + block_size -1) / block_size;
 /* The unit of blocks should be 512 */ /* fix it */
 /* assert(v_pri.logical_block_size_l >= 512);
    blocks = blocks * (v_pri.logical_block_size_l >> 9); */ 

  memset(&statbuf, 0, sizeof(struct stat));

  statbuf.st_dev	= fs_dev;
  statbuf.st_ino 	= ID_DIR_RECORD(dir);
  statbuf.st_mode 	= dir->inode.iso_mode;
  statbuf.st_nlink 	= dir->inode.iso_links;
  statbuf.st_uid 	= dir->inode.iso_uid;
  statbuf.st_gid 	= dir->inode.iso_gid;
  statbuf.st_rdev 	= dir->inode.iso_rdev;
  statbuf.st_size 	= dir->d_file_size;
  statbuf.st_blksize 	= block_size;
  statbuf.st_blocks 	= blocks;
  statbuf.st_atime 	= dir->inode.iso_atime.tv_sec;
  statbuf.st_mtime 	= dir->inode.iso_mtime.tv_sec;
  statbuf.st_ctime 	= dir->inode.iso_ctime.tv_sec;

  /* Copy the struct to user space. */
  r = sys_safecopyto(who_e, gid, 0, (vir_bytes) &statbuf,
		     (phys_bytes) sizeof(statbuf));

  return(r);
}


/*===========================================================================*
 *                             fs_stat					     *
 *===========================================================================*/
int fs_stat()
{
  register int r;              /* return value */
  struct dir_record *dir;
  r = EINVAL;

  if ((dir = get_dir_record(fs_m_in.REQ_INODE_NR)) != NULL) {
	r = stat_dir_record(dir, 0, fs_m_in.m_source, fs_m_in.REQ_GRANT);
	release_dir_record(dir);
  }
  return(r);
}


/*===========================================================================*
 *				fs_fstatfs				     *
 *===========================================================================*/
int fs_fstatfs()
{
  struct statfs st;
  int r;

  st.f_bsize = v_pri.logical_block_size_l;

  /* Copy the struct to user space. */
  r = sys_safecopyto(fs_m_in.m_source, fs_m_in.REQ_GRANT, 0,
		     (vir_bytes) &st, (phys_bytes) sizeof(st));

  return(r);
}


/*===========================================================================*
 *				fs_statvfs				     *
 *===========================================================================*/
int fs_statvfs()
{
  struct statvfs st;
  int r;


  st.f_bsize =  v_pri.logical_block_size_l;
  st.f_frsize = st.f_bsize;
  st.f_blocks = v_pri.volume_space_size_l;
  st.f_bfree = 0;
  st.f_bavail = 0;
  st.f_files = 0;
  st.f_ffree = 0;
  st.f_favail = 0;
  st.f_fsid = fs_dev;
  st.f_flag = ST_RDONLY;
  st.f_namemax = NAME_MAX;

  /* Copy the struct to user space. */
  r = sys_safecopyto(fs_m_in.m_source, fs_m_in.REQ_GRANT, 0, (vir_bytes) &st,
		     (phys_bytes) sizeof(st));

  return(r);
}

/*===========================================================================*
 *                              blockstats                                   *
  *===========================================================================*/
void fs_blockstats(u32_t *blocks, u32_t *free, u32_t *used)
{
        *used = *blocks = v_pri.volume_space_size_l;
        *free = 0;
}

