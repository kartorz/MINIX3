/* Function prototypes for iso9660 file system. */

struct dir_record;
struct ext_attr_rec;
struct iso9660_vd_pri;

#include <minix/libminixfs.h>

#define get_block(n) lmfs_get_block(fs_dev, n, NORMAL)
#define put_block(n) lmfs_put_block(n, FULL_DATA_BLOCK)

#define GET_VPRIISOMNT()  ((struct iso_mnt *)(v_pri.mnt_data))

#define GET_ISODIRECTORY(address, bp)

/* main.c */
int main(void);
void reply(int who, message *m_out);

/* inode.c */
int create_dir_record(struct dir_record *dir, char *buffer, u32_t
	address);
int create_ext_attr(struct ext_attr_rec *ext, char *buffer);
int fs_getnode(void);
int fs_putnode(void);

struct dir_record *get_dir_record(ino_t id_dir);
struct dir_record *get_free_dir_record(void);
struct ext_attr_rec *get_free_ext_attr(void);
struct dir_record *load_dir_record_from_disk(u32_t address);

int release_dir_record(struct dir_record *dir);

void dir_get_filename(struct dir_record *dir,
		      struct iso_directory_record *isodir,
		      		char* name, u16_t namelen);

void cd9660_defattr(struct iso_directory_record *isodir,
		 struct dir_record *inop, struct buf *bp);

void cd9660_deftstamp(struct iso_directory_record *isodir,
		   struct dir_record *inop, struct buf *bp);

int cd9660_tstamp_conv7(const u8_t *pi, struct timespec *pu);
int cd9660_tstamp_conv17(const u8_t *pi, struct timespec *pu);
ino_t isodirino(struct iso_directory_record *isodir, struct iso_mnt *imp);

/* misc.c */
int fs_sync(void);
int fs_new_driver(void);

/* mount.c */
int fs_readsuper(void);
int fs_mountpoint(void);
int fs_unmount(void);

/* path.c */
int fs_lookup(void);
int advance(struct dir_record *dirp, char string[NAME_MAX],
	    struct dir_record **resp);
int search_dir(struct dir_record *ldir_ptr, char string [NAME_MAX],
	       ino_t *numb);

/* protect.c */
int fs_access(void);

/* read.c */
int fs_read(void);
int fs_bread(void);
int fs_getdents(void);
int read_chunk(struct dir_record *rip, u64_t position, unsigned off, int
	chunk, unsigned left, cp_grant_id_t gid, unsigned buf_off, int
	block_size, int *completed, int rw);
/* link.c */
int fs_rdlink(void);

/* stadir.c */
int fs_stat(void);
int fs_fstatfs(void);
int fs_statvfs(void);

/* super.c */
int release_v_pri(struct iso9660_vd_pri *v_pri);
int read_vds(struct iso9660_vd_pri *v_pri, dev_t dev);
int create_v_pri(struct iso9660_vd_pri *v_pri, char *buffer, unsigned
	long address);

/* utility.c */
int do_noop(void);
int no_sys(void);
void isofntrans(const u_char *infn, int infnlen, u_char *outfn,
		u_short *outfnlen, int original, int casetrans,
		int assoc, int joliet_level);
int isofncmp(const u_char *fn, size_t fnlen, const u_char *isofn, size_t isolen,
	     int joliet_level);
int isochar(const u8_t *isofn, const u8_t *isoend,
	    int joliet_level, u16_t *c);
