#include "inc.h"
#include <string.h>
#include <minix/com.h>
#include <minix/vfsif.h>
#include <sys/stat.h>

#include "buf.h"
#include "iso_rrip.h"

static char *get_name(char *name, char string[NAME_MAX+1]);
static int parse_path(ino_t dir_ino, ino_t root_ino, int flags, struct
	dir_record **res_inop, size_t *offsetp, int *syml_loops);
static int get_dotdot_dirino(struct dir_record *dotdot_dir, ino_t *inum);

/*===========================================================================*
 *                             fs_lookup				     *
 *===========================================================================*/
int fs_lookup() {
  cp_grant_id_t grant;
  int r, len, flags, syml_loops;
  size_t offset;
  ino_t dir_ino, root_ino;
  struct dir_record *dir;

  grant		= fs_m_in.REQ_GRANT;
  len		= fs_m_in.REQ_PATH_LEN;	/* including terminating nul */
  dir_ino	= fs_m_in.REQ_DIR_INO;
  root_ino	= fs_m_in.REQ_ROOT_INO;
  flags		= fs_m_in.REQ_FLAGS;
  caller_uid	= fs_m_in.REQ_UID;
  caller_gid	= fs_m_in.REQ_GID;

  /* Check length. */
  if(len > sizeof(user_path)) return(E2BIG);	/* too big for buffer */
  if(len < 1) return(EINVAL);			/* too small */

  /* Copy the pathname and set up caller's user and group id */
  r = sys_safecopyfrom(VFS_PROC_NR, grant, 0, (vir_bytes) user_path,
		       (phys_bytes) len);
  if (r != OK) {
	printf("ISOFS %s:%d sys_safecopyfrom failed: %d\n",
		__FILE__, __LINE__, r);
	return(r);
  }

  /* Verify this is a null-terminated path. */
  if(user_path[len-1] != '\0') return(EINVAL);

  /* Lookup inode */
  dir = NULL;
  offset = 0;
  r = parse_path(dir_ino, root_ino, flags, &dir, &offset, &syml_loops);
  if (r == ELEAVEMOUNT) {
	/* Report offset and the error */
	fs_m_out.RES_OFFSET = offset;
	fs_m_out.RES_SYMLOOP = 0;
	return(r);
  }

  if (r != OK && r != EENTERMOUNT) return(r);

  fs_m_out.RES_INODE_NR     = ID_DIR_RECORD(dir);
  fs_m_out.RES_MODE         = dir->inode.iso_mode;
  fs_m_out.RES_FILE_SIZE_LO = dir->d_file_size;
  fs_m_out.RES_SYMLOOP      = syml_loops;
  fs_m_out.RES_UID          = SYS_UID;	/* root */
  fs_m_out.RES_GID          = SYS_GID;	/* operator */

  if (r == EENTERMOUNT) {
	fs_m_out.RES_OFFSET = offset;
	release_dir_record(dir);
  }

  return(r);
}

/* The search dir actually performs the operation of searching for the
 * compoent ``string" in ldir_ptr. It returns the response and the number of
 * the inode in numb. */
/*===========================================================================*
 *				search_dir				     *
 *===========================================================================*/
int search_dir(ldir_ptr,string,numb)
     register struct dir_record *ldir_ptr; /*  dir record parent */
     char string[ISO_MAXNAMLEN+1];	      /* component to search for */
     ino_t *numb;		      /* pointer to new dir record */
{
	register struct buf *bp;
	struct iso_directory_record *ep = NULL;
	u32_t pos,block,boff, dirlen;
	char tmp_string[ISO_MAXNAMLEN+1]; /* "1" for '\0' at the end. */
	uint32_t block_size = v_pri.logical_block_size_l;
	int assoc;
	struct iso_mnt *imp = ldir_ptr->i_mnt;
	u16_t namelen; /*exclude '\0'*/
	u16_t len = strlen(string); /*exclude '\0'*/
  	/* This function search a particular element (in string) in a inode and
   	* return its number */

	/*
	 * A leading `=' means, we are looking for an associated file
	 */
	assoc = (imp->iso_ftype != ISO_FTYPE_RRIP && *string == ASSOCCHAR);
	if (assoc) {
		len--;
		string++;
	}

	/* Initialize the tmp array */
	memset(tmp_string,'\0', ISO_MAXNAMLEN+1);

	if ((ldir_ptr->inode.iso_mode & I_TYPE) != I_DIRECTORY) {
    		return(ENOTDIR);
	}

	if (strcmp(string,".") == 0) {
		*numb = ID_DIR_RECORD(ldir_ptr);
		return OK;
	}

	if (strcmp(string,"..") == 0 
	&& ldir_ptr->loc_extent == v_pri.dir_rec_root->loc_extent) {
		*numb = ROOT_INO_NR;
	/*     *numb = ID_DIR_RECORD(ldir_ptr); */
    		return OK;
  	}

	/* Read the dir's content */
	block = ldir_ptr->d_phy_addr >> imp->im_bshift;
	pos = ldir_ptr->d_phy_addr & imp->im_bmask;
	boff = pos;
	for (;
	     pos < ldir_ptr->d_file_size;
	     pos += (block_size - pos%block_size), block++, boff = 0) {

		bp = get_block(block);
        	if (bp == NULL)
			return EINVAL;

		for (;
		     boff < block_size;
		     pos += dirlen, boff += dirlen) {
			ep = (struct iso_directory_record*)(b_data(bp) + boff);
			if (!(dirlen = isonum_711(ep->length)))
				break;
			*numb = 0;
			/*
			 *Check for a name match.
			 */
			switch (imp->iso_ftype) {
			default:
				namelen = isonum_711(ep->name_len);
				if ((!(isonum_711(ep->flags)&4)) == !assoc) {
					if (*(ep->name) == 0)
						break; /* "." has been searched before */
					if (*(ep->name) == 1) /* ".." */ {
						if (namelen == 1
						    && (!strcmp(string,"..")))
							*numb = isodirino(ep, imp);
					} else if (!(isofncmp(string,len,
						ep->name,namelen,
						imp->im_joliet_level))) {
					if (isonum_711(ep->flags)&2)
						*numb = isodirino(ep, imp);
					else
						*numb = ldir_ptr->d_phy_addr + pos;
					}
				}
			break;
			case ISO_FTYPE_RRIP:
			{
				if (isonum_711(ep->flags)&2)
					*numb = isodirino(ep, imp);
				else
					*numb = ldir_ptr->d_phy_addr + pos;
				cd9660_rrip_getname(ep, tmp_string, &namelen, numb, imp);
				tmp_string[namelen] = '\0';

				if(namelen != len
					|| strcmp(tmp_string,string) != 0)
					*numb = 0; /* not found */
			}
			break;
			}

			if(*numb) {
			/* If the element is found or we are searchig for... */
		        if (isonum_733(ep->extent) == dir_records->loc_extent) {
					/* In this case the inode is a root because the parent
					 * points to the same location than the inode. */
					*numb = 1;
			}
			put_block(bp);
			return OK;
			}

		} /* for */
		put_block(bp);
	} /* for */

	return EINVAL;
}


/*===========================================================================*
 *                             parse_path				     *
 *===========================================================================*/
static int parse_path(dir_ino, root_ino, flags, res_inop, offsetp, psym_loops)
ino_t dir_ino;
ino_t root_ino;
int flags;
struct dir_record **res_inop;
size_t *offsetp;
int *psym_loops;
{
  int r;
  char string[ISO_MAXNAMLEN+1];
  char *cp, *ncp;
  struct dir_record *start_dir, *old_dir;

  /* Find starting inode inode according to the request message */
  if ((start_dir = get_dir_record(dir_ino)) == NULL) {
	printf("ISOFS: couldn't find starting inode %u\n", dir_ino);
	return(ENOENT);
  }

  cp = user_path;
  *psym_loops = 0;
  /* Scan the path component by component. */
  while (TRUE) {
	if (cp[0] == '\0') {
	  /* Empty path */
	  *res_inop= start_dir;
	  *offsetp += cp-user_path;
	  /* Return EENTERMOUNT if we are at a mount point */
	  if (start_dir->d_mountpoint)
		return EENTERMOUNT;

	  return OK;
	}

	if (cp[0] == '/') {
	  /* Special case code. If the remaining path consists of just
	   * slashes, we need to look up '.'
	   */
	  while(cp[0] == '/')
		 cp++;
	  if (cp[0] == '\0') {
		strlcpy(string, ".", ISO_MAXNAMLEN + 1);
		ncp = cp;
	  } else
		ncp = get_name(cp, string);
	} else
	  /* Just get the first component */
	  ncp = get_name(cp, string);
	/* Special code for '..'. A process is not allowed to leave a chrooted
	 * environment. A lookup of '..' at the root of a mounted filesystem
	 * has to return ELEAVEMOUNT.
	 */
	if (strcmp(string, "..") == 0) {

	  /* This condition is not necessary since it will never be the root filesystem */
	  /*	   if (start_dir == dir_records) { */
	  /*	cp = ncp; */
	  /*	continue;	/\* Just ignore the '..' at a process' */
	  /*			 * root. */
	  /*			 *\/ */
	  /*	   } */

	  if (start_dir == dir_records) {
		/* Climbing up mountpoint */
		release_dir_record(start_dir);
		*res_inop = NULL;
		*offsetp += cp-user_path;
		return ELEAVEMOUNT;
	  }
	} else {
	  /* Only check for a mount point if we are not looking for '..'. */
	  if (start_dir->d_mountpoint) {
		*res_inop= start_dir;
		*offsetp += cp-user_path;
		return EENTERMOUNT;
	  }
	}

	/* There is more path.	Keep parsing. */
	old_dir = start_dir;

	r = advance(old_dir, string, &start_dir);
	if (r != OK) {
	  release_dir_record(old_dir);
	  return r;
	}

	if (!(S_ISLNK(start_dir->inode.iso_mode))
	    || (ncp[0] == '\0' && (flags & PATH_RET_SYMLINK))) {
		release_dir_record(old_dir);
		cp = ncp;
	} else {
		u16_t  llen = 0;
		size_t slen = strlen(ncp);
		char slink[MAXPATHLEN];
		struct buf *bp = NULL;
		struct iso_directory_record *ep;
		struct iso_mnt* imp = start_dir->i_mnt;
		u32_t block_nr, offset;
		if (ncp[0] != '\0' && ncp[0] != '/') {
			r = ENOENT;
			goto out;
		}

		block_nr = start_dir->d_phy_addr >> imp->im_bshift;
		offset = start_dir->d_phy_addr & imp->im_bmask;
		if(!(bp = get_block(block_nr))) {
			r = ENOENT;
			goto out;
		}
		ep = (struct iso_directory_record *)(bp->data + offset);

		/* llen exinlcude '\0' */
		if (cd9660_rrip_getsymname(ep, slink, &llen,
				   	start_dir->i_mnt) == 0) {
			r = ENOLINK;
			put_block(bp);
			goto out;
		}
		put_block(bp);

		/*  Linking to a absolute path is not supported,
		    eg. src -> /usr/src  */
	   	if (slink[0] == '/') {
			r = ENOLINK; /* ESYMLINK; */
			goto out;
	   	}

		if (cp - user_path + slen + llen + 1 > sizeof(user_path)) {
			r = ENAMETOOLONG;
			goto out;
		}

		if (ncp[0] != '\0' && (unsigned) (ncp - cp) != llen)
			memmove(cp+llen, ncp, slen);
		memmove(cp, slink, llen);
		cp[slen+llen] = '\0';

	   	/* Symloop limit reached? */
	   	if (++(*psym_loops) > SYMLOOP_MAX)
	   	{
			r = ELOOP;
			goto out;
	   	}
		release_dir_record(start_dir);
		start_dir  = old_dir;
	}
  }

out:
  release_dir_record(old_dir);
  release_dir_record(start_dir);
  return r;
}


/*===========================================================================*
 *				advance					     *
 *===========================================================================*/
int advance(dirp, string, resp)
struct dir_record *dirp;		/* inode for directory to be searched */
char string[ISO_MAXNAMLEN+1];		        /* component name to look for */
struct dir_record **resp;		/* resulting inode */
{
/* Given a directory and a component of a path, look up the component in
 * the directory, find the inode, open it, and return a pointer to its inode
 * slot.
 */

  register struct dir_record *rip = NULL;
  int r;
  ino_t numb;

  /* If 'string' is empty, yield same inode straight away. */
  if (string[0] == '\0') {
    return ENOENT;
  }

  /* Check for NULL. */
  if (dirp == NULL) {
    return EINVAL;
  }

  /* If 'string' is not present in the directory, signal error. */
  if ( (r = search_dir(dirp, string, &numb)) != OK) {
    return r;
  }

  /* The component has been found in the directory.  Get inode. */
  if ( (rip = get_dir_record((int) numb)) == NULL)  {
    return(err_code);
  }

  *resp= rip;
  return OK;
}


/*===========================================================================*
 *				get_name				     *
 *===========================================================================*/
static char *get_name(path_name, string)
char *path_name;		/* path name to parse */
char string[ISO_MAXNAMLEN+1];	/* component extracted from 'old_name' */
{
/* Given a pointer to a path name in fs space, 'path_name', copy the first
 * component to 'string' (truncated if necessary, always nul terminated).
 * A pointer to the string after the first component of the name as yet
 * unparsed is returned.  Roughly speaking,
 * 'get_name' = 'path_name' - 'string'.
 *
 * This routine follows the standard convention that /usr/ast, /usr//ast,
 * //usr///ast and /usr/ast/ are all equivalent.
 */
  size_t len;
  char *cp, *ep;

  cp= path_name;

  /* Skip leading slashes */
  while (cp[0] == '/')
	cp++;

  /* Find the end of the first component */
  ep= cp;
  while(ep[0] != '\0' && ep[0] != '/')
	ep++;

  len= ep-cp;

  /* Truncate the amount to be copied if it exceeds ISO_MAXNAMLEN */
  if (len > ISO_MAXNAMLEN)
	len= ISO_MAXNAMLEN;

  /* Special case of the string at cp is empty */
  if (len == 0)
  {
	/* Return "." */
	strlcpy(string, ".", ISO_MAXNAMLEN + 1);
  }
  else
  {
	memcpy(string, cp, len);
	string[len]= '\0';
  }

  return ep;
}
