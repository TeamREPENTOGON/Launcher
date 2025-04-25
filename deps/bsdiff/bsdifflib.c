/*-
 * Copyright 2003-2005 Colin Percival
 * Copyright 2007 Andreas John <dynacore@tesla.inka.de>
 * Copyright 2023 Peter Vaskovic <petervaskovic@yahoo.de>
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <bzlib.h>
#include "bsdifflib.h"

#define ERRSTR_MAX_LEN 1024
#define HEADER_SIZE 32

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#if defined(_MSC_VER) && _MSC_VER < 1900		//VS before 2015
	#define snprintf	_snprintf
#endif

/***************************************************************************/

static void split(off_t *I, off_t *V, off_t start, off_t len, off_t h)
{
	off_t i, j, k, x, tmp, jj, kk;

	if (len < 16)
	{
		for (k = start; k < start + len; k += j)
		{
			j = 1;
			x = V[I[k] + h];

			for (i = 1; k + i < start + len; i++)
			{
				if (V[I[k + i] + h] < x)
				{
					x = V[I[k + i] + h];
					j = 0;
				};

				if (V[I[k + i] + h] == x)
				{
					tmp = I[k + j];
					I[k + j] = I[k + i];
					I[k + i] = tmp;
					j++;
				};
			};

			for (i = 0; i < j; i++)
			{
				V[I[k + i]] = k + j - 1;
			}

			if (j == 1)
			{
				I[k] = -1;
			}
		};

		return;
	};

	x = V[I[start + len / 2] + h];

	jj = 0;

	kk = 0;

	for (i = start; i < start + len; i++)
	{
		if (V[I[i] + h] < x)
		{
			jj++;
		}

		if (V[I[i] + h] == x)
		{
			kk++;
		}
	};

	jj += start;

	kk += jj;

	i = start;

	j = 0;

	k = 0;

	while (i < jj)
	{
		if (V[I[i] + h] < x)
		{
			i++;
		}
		else if (V[I[i] + h] == x)
		{
			tmp = I[i];
			I[i] = I[jj + j];
			I[jj + j] = tmp;
			j++;
		}
		else
		{
			tmp = I[i];
			I[i] = I[kk + k];
			I[kk + k] = tmp;
			k++;
		};
	};

	while (jj + j < kk)
	{
		if (V[I[jj + j] + h] == x)
		{
			j++;
		}
		else
		{
			tmp = I[jj + j];
			I[jj + j] = I[kk + k];
			I[kk + k] = tmp;
			k++;
		};
	};

	if (jj > start)
	{
		split(I, V, start, jj - start, h);
	}

	for (i = 0; i < kk - jj; i++)
	{
		V[I[jj + i]] = kk - 1;
	}

	if (jj == kk - 1)
	{
		I[jj] = -1;
	}

	if (start + len > kk)
	{
		split(I, V, kk, start + len - kk, h);
	}
}

/***************************************************************************/

static void qsufsort(off_t *I, off_t *V, unsigned char *old, off_t oldsize)
{
	off_t buckets[256];
	off_t i, h, len;

	for (i = 0; i < 256; i++)
	{
		buckets[i] = 0;
	}

	for (i = 0; i < oldsize; i++)
	{
		buckets[old[i]]++;
	}

	for (i = 1; i < 256; i++)
	{
		buckets[i] += buckets[i - 1];
	}

	for (i = 255; i > 0; i--)
	{
		buckets[i] = buckets[i - 1];
	}

	buckets[0] = 0;

	for (i = 0; i < oldsize; i++)
	{
		I[++buckets[old[i]]] = i;
	}

	I[0] = oldsize;

	for (i = 0; i < oldsize; i++)
	{
		V[i] = buckets[old[i]];
	}

	V[oldsize] = 0;

	for (i = 1; i < 256; i++)
		if (buckets[i] == buckets[i - 1] + 1)
		{
			I[buckets[i]] = -1;
		}

	I[0] = -1;

	for (h = 1; I[0] != -(oldsize + 1); h += h)
	{
		len = 0;

		for (i = 0; i < oldsize + 1;)
		{
			if (I[i] < 0)
			{
				len -= I[i];
				i -= I[i];
			}
			else
			{
				if (len)
				{
					I[i - len] = -len;
				}

				len = V[I[i]] + 1 - i;
				split(I, V, i, len, h);
				i += len;
				len = 0;
			};
		};

		if (len)
		{
			I[i - len] = -len;
		}
	};

	for (i = 0; i < oldsize + 1; i++)
	{
		I[V[i]] = i;
	}
}

/***************************************************************************/

static off_t matchlen(unsigned char *old, off_t oldsize, unsigned char *_new, off_t newsize)
{
	off_t i;

	for (i = 0; (i < oldsize) && (i < newsize); i++)
		if (old[i] != _new[i])
		{
			break;
		}

	return i;
}

/***************************************************************************/

static off_t search(off_t *I, unsigned char *old, off_t oldsize,
					unsigned char *_new, off_t newsize, off_t st, off_t en, off_t *pos)
{
	off_t x, y;

	if (en - st < 2)
	{
		x = matchlen(old + I[st], oldsize - I[st], _new, newsize);
		y = matchlen(old + I[en], oldsize - I[en], _new, newsize);

		if (x > y)
		{
			*pos = I[st];
			return x;
		}
		else
		{
			*pos = I[en];
			return y;
		}
	};

	x = st + (en - st) / 2;

	if (memcmp(old + I[x], _new, MIN(oldsize - I[x], newsize)) <= 0)
	{
		return search(I, old, oldsize, _new, newsize, x, en, pos);
	}
	else
	{
		return search(I, old, oldsize, _new, newsize, st, x, pos);
	};
}

/***************************************************************************/

static void offtout(off_t x, unsigned char *buf)
{
	off_t y;

	if (x < 0)
	{
		y = -x;
	}
	else
	{
		y = x;
	}

	buf[0] = y % 256;
	y -= buf[0];
	y = y / 256;
	buf[1] = y % 256;
	y -= buf[1];
	y = y / 256;
	buf[2] = y % 256;
	y -= buf[2];
	y = y / 256;
	buf[3] = y % 256;
	y -= buf[3];
	y = y / 256;
	buf[4] = y % 256;
	y -= buf[4];
	y = y / 256;
	buf[5] = y % 256;
	y -= buf[5];
	y = y / 256;
	buf[6] = y % 256;
	y -= buf[6];
	y = y / 256;
	buf[7] = y % 256;

	if (x < 0)
	{
		buf[7] |= 0x80;
	}
}

/***************************************************************************/

char *bsdiff(const char *oldfile, const char *newfile,
			 const char *patchfile)
{
	unsigned char *old = NULL;
	unsigned char *_new = NULL;
	off_t oldsize, newsize;
	off_t *I = NULL;
	off_t *V = NULL;
	off_t scan, len;
	off_t pos = 0;
	off_t lastscan, lastpos, lastoffset;
	off_t oldscore, scsc;
	off_t s, Sf, lenf, Sb, lenb;
	off_t overlap, Ss, lens;
	off_t i;
	off_t dblen, eblen;
	unsigned char *db = NULL;
	unsigned char *eb = NULL;
	unsigned char buf[8];
	unsigned char header[HEADER_SIZE];
	FILE *pf, *fp;
	BZFILE *pfbz2;
	int bz2err;
	static char errstr[ERRSTR_MAX_LEN];

	/* Allocate oldsize+1 bytes instead of oldsize bytes to ensure
		that we never try to malloc(0) and get a NULL pointer */
	if (((fp = fopen(oldfile, "rb")) == NULL) ||
		(fseek(fp, 0, SEEK_END) != 0) ||
		((oldsize = ftell(fp)) == -1) ||
		((old = (unsigned char *)malloc(oldsize + 1)) == NULL) ||
		(fseek(fp, 0, SEEK_SET) != 0) ||
		((off_t)fread(old, 1, oldsize, fp) != oldsize))
	{
		snprintf(errstr, sizeof(errstr), "Cannot process file \"%s\".", oldfile);
		free(old);
		if (fp)
			fclose(fp);
		return errstr;
	}

	if (fclose(fp) != 0)
	{
		snprintf(errstr, sizeof(errstr), "Cannot close file \"%s\".", oldfile);
		free(old);
		return errstr;
	}

	if (((I = (off_t *)malloc((oldsize + 1) * sizeof(off_t))) == NULL) ||
		((V = (off_t *)malloc((oldsize + 1) * sizeof(off_t))) == NULL))
	{
		snprintf(errstr, sizeof(errstr), "Cannot allocate memory.");
		free(I);
		free(V);
		free(old);
		return errstr;
	}

	qsufsort(I, V, old, oldsize);
	free(V);

	/* Allocate newsize+1 bytes instead of newsize bytes to ensure
		that we never try to malloc(0) and get a NULL pointer */
	if (((fp = fopen(newfile, "rb")) == NULL) ||
		(fseek(fp, 0, SEEK_END) != 0) ||
		((newsize = ftell(fp)) == -1) ||
		((_new = (unsigned char *)malloc(newsize + 1)) == NULL) ||
		(fseek(fp, 0, SEEK_SET) != 0) ||
		((off_t)fread(_new, 1, newsize, fp) != newsize))
	{
		snprintf(errstr, sizeof(errstr), "Cannot process file \"%s\".", newfile);
		if (fp)
			fclose(fp);
		goto GETOUT;
	}

	if (fclose(fp) != 0)
	{
		snprintf(errstr, sizeof(errstr), "Cannot process file \"%s\".", newfile);
		goto GETOUT;
	}

	if (((db = (unsigned char *)malloc(newsize + 1)) == NULL) ||
		((eb = (unsigned char *)malloc(newsize + 1)) == NULL))
	{
		snprintf(errstr, sizeof(errstr), "Cannot allocate memory.");
		goto GETOUT_DB;
	}

	dblen = 0;
	eblen = 0;

	/* Create the patch file */
	if ((pf = fopen(patchfile, "wb")) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "Cannot open file \"%s\".", patchfile);
		goto GETOUT_DB;
	}

	/* Header is
		0	8	 "BSDIFF40"
		8	8	length of bzip2ed ctrl block
		16	8	length of bzip2ed diff block
		24	8	length of new file */
	/* File is
		0	32	Header
		32	??	Bzip2ed ctrl block
		??	??	Bzip2ed diff block
		??	??	Bzip2ed extra block */
	memcpy(header, "BSDIFF40", 8);
	offtout(0, header + 8);
	offtout(0, header + 16);
	offtout(newsize, header + 24);

	if (fwrite(header, HEADER_SIZE, 1, pf) != 1)
	{
		snprintf(errstr, sizeof(errstr), "Cannot write file \"%s\".", patchfile);
		goto GETOUT_PF;
	}

	/* Compute the differences, writing ctrl as we go */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteOpen, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	scan = 0;
	len = 0;
	lastscan = 0;
	lastpos = 0;
	lastoffset = 0;

	while (scan < newsize)
	{
		oldscore = 0;

		/* If we come across a large block of data that only differs
		 * by less than 8 bytes, this loop will take a long time to
		 * go past that block of data. We need to track the number of
		 * times we're stuck in the block and break out of it. */
		int num_less_than_eight = 0;
		off_t prev_len, prev_oldscore, prev_pos;
		for (scsc = scan += len; scan < newsize; scan++)
		{
			prev_len = len;
			prev_oldscore = oldscore;
			prev_pos = pos;

			len = search(I, old, oldsize, _new + scan, newsize - scan,
						 0, oldsize, &pos);

			for (; scsc < scan + len; scsc++)
				if ((scsc + lastoffset < oldsize) &&
					(old[scsc + lastoffset] == _new[scsc]))
				{
					oldscore++;
				}

			if (((len == oldscore) && (len != 0)) ||
				(len > oldscore + 8))
			{
				break;
			}

			if ((scan + lastoffset < oldsize) &&
				(old[scan + lastoffset] == _new[scan]))
			{
				oldscore--;
			}

			const off_t fuzz = 8;
			if (prev_len-fuzz <= len && len <= prev_len &&
			    prev_oldscore-fuzz <= oldscore &&
			    oldscore <= prev_oldscore &&
			    prev_pos <= pos && pos <= prev_pos + fuzz &&
			    oldscore <= len && len <= oldscore + fuzz)
			{
				++num_less_than_eight;
			}
			else
			{
				num_less_than_eight = 0;
			}

			if (num_less_than_eight > 100) break;
		};

		if ((len != oldscore) || (scan == newsize))
		{
			s = 0;
			Sf = 0;
			lenf = 0;

			for (i = 0; (lastscan + i < scan) && (lastpos + i < oldsize);)
			{
				if (old[lastpos + i] == _new[lastscan + i])
				{
					s++;
				}

				i++;

				if (s * 2 - i > Sf * 2 - lenf)
				{
					Sf = s;
					lenf = i;
				};
			};

			lenb = 0;

			if (scan < newsize)
			{
				s = 0;
				Sb = 0;

				for (i = 1; (scan >= lastscan + i) && (pos >= i); i++)
				{
					if (old[pos - i] == _new[scan - i])
					{
						s++;
					}

					if (s * 2 - i > Sb * 2 - lenb)
					{
						Sb = s;
						lenb = i;
					};
				};
			};

			if (lastscan + lenf > scan - lenb)
			{
				overlap = (lastscan + lenf) - (scan - lenb);
				s = 0;
				Ss = 0;
				lens = 0;

				for (i = 0; i < overlap; i++)
				{
					if (_new[lastscan + lenf - overlap + i] ==
						old[lastpos + lenf - overlap + i])
					{
						s++;
					}

					if (_new[scan - lenb + i] ==
						old[pos - lenb + i])
					{
						s--;
					}

					if (s > Ss)
					{
						Ss = s;
						lens = i + 1;
					};
				};

				lenf += lens - overlap;
				lenb -= lens;
			};

			for (i = 0; i < lenf; i++)
			{
				db[dblen + i] = _new[lastscan + i] - old[lastpos + i];
			}

			for (i = 0; i < (scan - lenb) - (lastscan + lenf); i++)
			{
				eb[eblen + i] = _new[lastscan + lenf + i];
			}

			dblen += lenf;
			eblen += (scan - lenb) - (lastscan + lenf);
			offtout(lenf, buf);
			BZ2_bzWrite(&bz2err, pfbz2, buf, 8);

			if (bz2err != BZ_OK)
			{
				snprintf(errstr, sizeof(errstr), "BZ2_bzWrite, bz2err = %d", bz2err);
				goto GETOUT_PFBZ;
			}

			offtout((scan - lenb) - (lastscan + lenf), buf);
			BZ2_bzWrite(&bz2err, pfbz2, buf, 8);

			if (bz2err != BZ_OK)
			{
				snprintf(errstr, sizeof(errstr), "BZ2_bzWrite, bz2err = %d", bz2err);
				goto GETOUT_PFBZ;
			}

			offtout((pos - lenb) - (lastpos + lenf), buf);
			BZ2_bzWrite(&bz2err, pfbz2, buf, 8);

			if (bz2err != BZ_OK)
			{
				snprintf(errstr, sizeof(errstr), "BZ2_bzWrite, bz2err = %d", bz2err);
				goto GETOUT_PFBZ;
			}

			lastscan = scan - lenb;
			lastpos = pos - lenb;
			lastoffset = pos - scan;
		};
	};

	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);

	if (bz2err != BZ_OK)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteClose, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	/* Compute size of compressed ctrl data */
	if ((len = ftell(pf)) == -1)
	{
		snprintf(errstr, sizeof(errstr), "Cannot return current value of the position indicator in file \"%s\".", patchfile);
		goto GETOUT_PF;
	}

	offtout(len - HEADER_SIZE, header + 8);

	/* Write compressed diff data */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteOpen, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	BZ2_bzWrite(&bz2err, pfbz2, db, dblen);

	if (bz2err != BZ_OK)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWrite, bz2err = %d", bz2err);
		goto GETOUT_PFBZ;
	}

	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);

	if (bz2err != BZ_OK)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteClose, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	/* Compute size of compressed diff data */
	if ((newsize = ftell(pf)) == -1)
	{
		snprintf(errstr, sizeof(errstr), "Cannot return current value of the position indicator in file \"%s\".", patchfile);
		goto GETOUT_PF;
	}

	offtout(newsize - len, header + 16);

	/* Write compressed extra data */
	if ((pfbz2 = BZ2_bzWriteOpen(&bz2err, pf, 9, 0, 0)) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteOpen, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	BZ2_bzWrite(&bz2err, pfbz2, eb, eblen);

	if (bz2err != BZ_OK)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWrite, bz2err = %d", bz2err);
	GETOUT_PFBZ:
		BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);
	GETOUT_PF:
		fclose(pf);
	GETOUT_DB:
		free(db);
		free(eb);
	GETOUT:
		free(I);
		free(_new);
		free(old);
		return errstr;
	}

	BZ2_bzWriteClose(&bz2err, pfbz2, 0, NULL, NULL);

	if (bz2err != BZ_OK)
	{
		snprintf(errstr, sizeof(errstr), "BZ2_bzWriteClose, bz2err = %d", bz2err);
		goto GETOUT_PF;
	}

	/* Seek to the beginning, write the header, and close the file */
	if (fseek(pf, 0, SEEK_SET))
	{
		snprintf(errstr, sizeof(errstr), "Cannot change the file position indicator in file \"%s\".", patchfile);
		goto GETOUT_PF;
	}

	if (fwrite(header, HEADER_SIZE, 1, pf) != 1)
	{
		snprintf(errstr, sizeof(errstr), "Cannot write file \"%s\".", patchfile);
		goto GETOUT_PF;
	}

	if (fclose(pf))
	{
		snprintf(errstr, sizeof(errstr), "Cannot close file \"%s\".", patchfile);
		goto GETOUT_DB;
	}

	/* Free the memory we used */
	free(db);
	free(eb);
	free(I);
	free(old);
	free(_new);
	return NULL;
}
