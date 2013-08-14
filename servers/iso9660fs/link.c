#include <sys/stat.h>
#include <string.h>
#include <minix/com.h>
#include <minix/vfsif.h>

#include "inc.h"
#include "iso_rrip.h"

int fs_rdlink(void)
{
	struct buf *bp;
	struct iso_directory_record *iso_dir;
	u16_t symlen, symlen0;
	char  symname[MAXPATHLEN];
	register int r;
	u32_t block_nr, offset;
	struct iso_mnt *imp = GET_VPRIISOMNT();
	symlen = fs_m_in.REQ_MEM_SIZE > MAXPATHLEN
		? MAXPATHLEN
		: fs_m_in.REQ_MEM_SIZE;

	if (!symlen)
		return EINVAL;

	block_nr = fs_m_in.REQ_INODE_NR >> imp->im_bshift;
	offset = fs_m_in.REQ_INODE_NR & imp->im_bmask;
	if(!(bp = get_block(block_nr)))
		return EINVAL;

	iso_dir = (struct iso_directory_record *)(bp->data + offset);
	if (cd9660_rrip_getsymname(iso_dir, symname, &symlen0, imp) == 0) {
		put_block(bp);
		return (EINVAL);
	}

	symlen = symlen > symlen0 ? symlen0 : symlen;

	r = sys_safecopyto(VFS_PROC_NR, (cp_grant_id_t) fs_m_in.REQ_GRANT,
			   (vir_bytes) 0, (vir_bytes)(symname),
			   (size_t) symlen);
	if (r == OK)
		fs_m_out.RES_NBYTES = symlen;
	
	put_block(bp);
	return(r);
}

