#include "inc.h"
#include <sys/stat.h>
#include <string.h>
#include <minix/com.h>
#include <minix/callnr.h>
#include <minix/vfsif.h>

static int wput(u_char *s, size_t n, u_int16_t c, int joliet_level);
static u_int16_t wget(const u_char **str, size_t *sz, int joliet_level);
/*===========================================================================*
 *				do_noop					     *
 *===========================================================================*/
int do_noop(void)
{
/* Do not do anything. */
  return(OK);
}

/*===========================================================================*
 *				no_sys					     *
 *===========================================================================*/
int no_sys(void)
{
/* Somebody has used an illegal system call number */
  return(EINVAL);
}

int cd9660_utf8_joliet = 0;
/*
 * Get one character out of an iso filename
 * Return number of bytes consumed
 */
int
isochar(const u8_t *isofn, const u8_t *isoend,
	    int joliet_level, u16_t *c)
{
	*c = isofn[0];
	if (joliet_level == 0 || isofn + 1 == isoend) {
		/* (00) and (01) are one byte in Joliet, too */
		return 1;
	}

	if (cd9660_utf8_joliet) {
		*c = (*c << 8) + isofn[1];
	} else {
		/* characters outside ISO-8859-1 subset replaced with '?' */
		if (*c != 0)
			*c = '?';
		else
			*c = isofn[1];
	}

	return 2;
}

/*
 * translate and compare a filename
 * Note: Version number plus ';' may be omitted.
 */
int
isofncmp(const u_char *fn, size_t fnlen, const u_char *isofn, size_t isolen,
	int joliet_level)
{
	int i, j;
	u_int16_t fc, ic;
	const u_char *isoend = isofn + isolen;

	while (fnlen > 0) {
		fc = wget(&fn, &fnlen, joliet_level);

		if (isofn == isoend)
			return fc;
		isofn += isochar(isofn, isoend, joliet_level, &ic);
		if (ic == ';') {
			switch (fc) {
			default:
				return fc;
			case 0:
				return 0;
			case ';':
				break;
			}
			fn++;
			for (i = 0; fnlen-- != 0; i = i * 10 + *fn++ - '0') {
				if (*fn < '0' || *fn > '9') {
					return -1;
				}
			}
			for (j = 0; isofn != isoend; j = j * 10 + ic - '0')
				isofn += isochar(isofn, isoend,
						 joliet_level, &ic);
			return i - j;
		}
		if (ic != fc) {
			if (ic >= 'A' && ic <= 'Z') {
				if (ic + ('a' - 'A') != fc) {
					if (fc >= 'a' && fc <= 'z')
						fc -= 'a' - 'A';

					return (int) fc - (int) ic;
				}
			} else
				return (int) fc - (int) ic;
		}
	}
	if (isofn != isoend) {
		isofn += isochar(isofn, isoend, joliet_level, &ic);
		switch (ic) {
		default:
			return -1;
		case '.':
			if (isofn != isoend) {
				isochar(isofn, isoend, joliet_level, &ic);
				if (ic == ';')
					return 0;
			}
			return -1;
		case ';':
			return 0;
		}
	}
	return 0;
}

/*
 * translate a filename
 */
void
isofntrans(const u_char *infn, int infnlen, u_char *outfn,
		u_short *outfnlen, int original, int casetrans,
		int assoc, int joliet_level)
{
	int fnidx = 0;
	const u_char *infnend = infn + infnlen;
	u16_t c;
	int sz;

	if (assoc) {
		*outfn++ = ASSOCCHAR;
		fnidx++;
	}

	for(; infn != infnend; fnidx += sz) {
		infn += isochar(infn, infnend, joliet_level, &c);

		if (casetrans && joliet_level == 0 && c >= 'A' && c <= 'Z')
			c = c + ('a' - 'A');
		else if (!original && c == ';') {
			if (fnidx > 0 && outfn[-1] == '.')
				fnidx--;
			break;
		}
		sz = wput(outfn, ISO_MAXNAMLEN - fnidx, c, joliet_level);
		if (sz == 0) {
			/* not enough space to write the character */
			if (fnidx < ISO_MAXNAMLEN) {
				*outfn = '?';
				fnidx++;
			}
			break;
		}
		outfn += sz;
	}
	*outfnlen = fnidx;
}
static u_int16_t
wget(const u_char **str, size_t *sz, int joliet_level)
{
	if (joliet_level > 0 && cd9660_utf8_joliet) {
		/* decode UTF-8 sequence */
		/* return wget_utf8((const char **) str, sz); */
		return *str[0]; /*Todo: support utf8_joliet extension */
	} else {
		/*
		 * Raw 8-bit characters without any conversion. For Joliet,
		 * this effectively assumes provided file name is using
		 * ISO-8859-1 subset.
		 */
		u_int16_t c = *str[0];
		(*str)++;
		(*sz)--;

		return c;
	}
}

static int
wput(u_char *s, size_t n, u_int16_t c, int joliet_level)
{
	if (joliet_level > 0 && cd9660_utf8_joliet) {
		/* Store Joliet file name encoded into UTF-8 */
		/* return wput_utf8((char *)s, n, c); */
		return 0; /*Todo: support utf8_joliet extension */
	} else {
		/*
		 * Store raw 8-bit characters without any conversion.
		 * For Joliet case, this filters the Unicode characters
		 * to ISO-8859-1 subset.
		 */
		*s = (u_char)c;
		return 1;
	}
}
