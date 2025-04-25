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
#include <limits.h>
#include <bzlib.h>
#include "bspatchlib.h"

#define ERRSTR_MAX_LEN 1024
#define HEADER_SIZE 32

/* TYPE_MINIMUM and TYPE_MAXIMUM taken from coreutils */
#ifndef TYPE_MINIMUM
#define TYPE_MINIMUM(t) \
	((t)((t)0 < (t)-1 ? (t)0 : ~TYPE_MAXIMUM(t)))
#endif
#ifndef TYPE_MAXIMUM
#define TYPE_MAXIMUM(t) \
	((t)((t)0 < (t)-1   \
			 ? (t)-1    \
			 : ((((t)1 << (sizeof(t) * CHAR_BIT - 2)) - 1) * 2 + 1)))
#endif

#ifndef OFF_MAX
#define OFF_MAX TYPE_MAXIMUM(off_t)
#endif

#ifndef OFF_MIN
#define OFF_MIN TYPE_MINIMUM(off_t)
#endif

#if defined(_MSC_VER) && _MSC_VER < 1900		//VS before 2015
	#define snprintf	_snprintf
#endif

#if defined(_MSC_VER) && _MSC_VER < 1800		//VS before 2013
	#if defined(_WIN64)
		#define SIZE_T_FMT "%llu"
	#else
		#define SIZE_T_FMT "%lu"
	#endif
#else
	#define SIZE_T_FMT "%zu"
#endif

#ifdef _WIN32
	#define LONG_LONG_FMT "%I64d"
#else
	#define LONG_LONG_FMT "%lld"
#endif

/***************************************************************************/

char *file_to_mem(const char *fname, unsigned char **buf, int *buf_sz)
{
	FILE *f;
	long f_sz;
	size_t l_sz;
	unsigned char *l_buf;
	static char errstr[ERRSTR_MAX_LEN];

	*buf = NULL;
	*buf_sz = 0;

	if ((f = fopen(fname, "rb")) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "Cannot open file \"%s\" for reading.", fname);
		return errstr;
	}
	if (fseek(f, 0, SEEK_END) || (f_sz = ftell(f)) < 0)
	{
		fclose(f);
		snprintf(errstr, sizeof(errstr), "Seek failure on file \"%s\".", fname);
		return errstr;
	}

	if (f_sz >= INT_MAX)
	{
		fclose(f);
		snprintf(errstr, sizeof(errstr), "File size (" LONG_LONG_FMT " bytes) exceeds maximum limit for an int.", (long long)f_sz);
		return errstr;
	}
	l_sz = (size_t)f_sz;

	if ((l_buf = (unsigned char *)malloc(l_sz + 1)) == NULL)
	{
		fclose(f);
		snprintf(errstr, sizeof(errstr), "Cannot allocate " SIZE_T_FMT " bytes to read file \"%s\" into memory.", l_sz + 1, fname);
		return errstr;
	}

	fseek(f, 0, SEEK_SET);

	if (fread(l_buf, 1, l_sz, f) != l_sz)
	{
		free(l_buf);
		fclose(f);
		snprintf(errstr, sizeof(errstr), "Cannot read from file \"%s\".", fname);
		return errstr;
	}
	fclose(f);
	*buf = l_buf;
	*buf_sz = (int)l_sz;
	return NULL;
}

/***************************************************************************/

char *mem_to_file(const unsigned char *buf, int buf_sz, const char *fname)
{
	FILE *f;
	static char errstr[ERRSTR_MAX_LEN];

	if ((f = fopen(fname, "wb")) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "Cannot open file \"%s\" for writing.", fname);
		return errstr;
	}
	if ((int)fwrite(buf, 1, buf_sz, f) != buf_sz)
	{
		fclose(f);
		snprintf(errstr, sizeof(errstr), "Cannot write to file \"%s\".", fname);
		return errstr;
	}
	fclose(f);
	return NULL;
}

/***************************************************************************/

static int add_off_t_overflow(off_t a, off_t b, off_t *res)
{

	if ((b > 0 && a > OFF_MAX - b) || (b < 0 && a < OFF_MIN - b))
		return -1;

	*res = a + b;
	return 0;
}

/***************************************************************************/

static off_t offtin(const unsigned char *buf)
{
	off_t y;
	y = buf[7] & 0x7F;
	y = y * 256;
	y += buf[6];
	y = y * 256;
	y += buf[5];
	y = y * 256;
	y += buf[4];
	y = y * 256;
	y += buf[3];
	y = y * 256;
	y += buf[2];
	y = y * 256;
	y += buf[1];
	y = y * 256;
	y += buf[0];

	if (buf[7] & 0x80)
	{
		y = -y;
	}

	return y;
}

/***************************************************************************/

static char *decompress_block(const unsigned char *in_buf, unsigned in_sz, unsigned char **out_buf, int *out_sz)
{
	int rc, known_size;
	unsigned sz;
	unsigned char *buf;
	unsigned char *new_buf;
	static char errstr[ERRSTR_MAX_LEN];

	known_size = *out_sz;
	*out_buf = NULL;
	*out_sz = 0;

	if (in_sz == 0) /* BZ2 file cannot be empty. It must at least have a header */
	{
		snprintf(errstr, sizeof(errstr), "decompress_block: compressed file cannot be empty");
		return errstr;
	}

	if (in_sz >= INT_MAX) /* Compressed input file is bigger than 2 GB */
	{
		snprintf(errstr, sizeof(errstr), "decompress_block: compressed file is too big");
		return errstr;
	}

	if (known_size >= INT_MAX) /* Uncompressed output file is bigger than 2 GB */
	{
		snprintf(errstr, sizeof(errstr), "decompress_block: uncompressed file is too big");
		return errstr;
	}

	sz = (known_size >= 0) ? ((unsigned)known_size + 16) : 2 * in_sz;
	if ((buf = (unsigned char *)malloc(sz)) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "decompress_block: cannot allocate %u bytes for patch", sz);
		return errstr;
	}

	for (;;)
	{
		rc = BZ2_bzBuffToBuffDecompress((char *)buf, &sz, (char *)in_buf, in_sz, 0, 0);
		if (rc == BZ_OK)
		{
			*out_buf = buf;
			*out_sz = (int)sz;
			return NULL;
		}
		if (rc != BZ_OUTBUFF_FULL)
		{
			free(buf); /* Release memory if decompression failed for some other reason */
			snprintf(errstr, sizeof(errstr), "decompress_block: BZ2_bzBuffToBuffDecompress() returned %d", rc);
			return errstr;
		}

		if (sz >= INT_MAX) /* Seems to me that 2 GB should be more than enough for a patch */
			break;

		/*
		 * We come to this point if the decompression failed because
		 * we have not allocated enough memory
		 */
		if (known_size >= 0) /* This should not happen, but don't break if it does */
		{
			if (sz < 2 * in_sz)
				sz = 2 * in_sz;
			else
				sz *= 2;
			known_size = -1;
		}
		else /* Double the buffer size */
		{
			sz *= 2;
		}

		if ((new_buf = (unsigned char *)realloc(buf, sz)) == NULL) /* Allocate more memory */
		{
			free(buf);
			snprintf(errstr, sizeof(errstr), "decompress_block: cannot allocate %u bytes for patch", sz);
			return errstr;
		}
		buf = new_buf;
	}
	free(buf);
	snprintf(errstr, sizeof(errstr), "decompress_block: even %u bytes are not enough for the patch", sz);
	return errstr;
}

/***************************************************************************/

char *bspatch_mem(const unsigned char *old_buf, int old_size,
				  unsigned char **new_buf, int *new_size,
				  const unsigned char *compr_patch_buf, int compr_patch_buf_sz,
				  int uncompr_ctrl_sz, int uncompr_diff_sz, int uncompr_xtra_sz)
{
	off_t l_new_size, res_add_off_t;
	int dec_ctrl_sz, dec_diff_sz, dec_xtra_sz;
	off_t bzctrllen, bzdifflen, bzextralen;
	unsigned char *l_new_buf, *dec_ctrl_buf, *dec_diff_buf, *dec_xtra_buf,
		*pctrl, *pdiff, *pxtra, *pctrl_end, *pdiff_end, *pxtra_end;
	off_t oldpos, newpos, i, ctrl[3];
	char *errmsg;
	static char errstr[ERRSTR_MAX_LEN];

	*new_buf = NULL;
	*new_size = 0;

	/*
	 * File format:
	 *	0	8	"BSDIFF40"
	 *	8	8	X
	 *	16	8	Y
	 *	24	8	sizeof(newfile)
	 *	32	X	bzip2(control block)
	 *	32+X	Y	bzip2(diff block)
	 *	32+X+Y	???	bzip2(extra block)
	 * with control block a set of triples (x,y,z) meaning "add x bytes
	 * from oldfile to x bytes from the diff block; copy y bytes from the
	 * extra block; seek forwards in oldfile by z bytes".
	 */
	if (compr_patch_buf_sz < HEADER_SIZE)
	{
		snprintf(errstr, sizeof(errstr), "Corrupt patch. Too short patch size.");
		return errstr;
	}

	/* Check for appropriate magic */
	if (memcmp(compr_patch_buf, "BSDIFF40", 8) != 0)
	{
		snprintf(errstr, sizeof(errstr), "Corrupt patch. Bad header signature.");
		return errstr;
	}

	/* Read lengths from header */
	bzctrllen = offtin(compr_patch_buf + 8);
	bzdifflen = offtin(compr_patch_buf + 16);
	l_new_size = offtin(compr_patch_buf + 24);

	/* Overflow-check */
	if (add_off_t_overflow(HEADER_SIZE, bzctrllen, &res_add_off_t) ||
		add_off_t_overflow(HEADER_SIZE + bzctrllen, bzdifflen, &res_add_off_t))
	{
		snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 1.");
		return errstr;
	}

	if (bzctrllen <= 0 || bzctrllen > OFF_MAX - HEADER_SIZE ||
		bzdifflen <= 0 || bzctrllen + HEADER_SIZE > OFF_MAX - bzdifflen ||
		l_new_size < 0 || l_new_size > INT_MAX ||
		compr_patch_buf_sz <= HEADER_SIZE + bzctrllen + bzdifflen)
	{
		snprintf(errstr, sizeof(errstr), "Corrupt patch. Bad header lengths.");
		return errstr;
	}
	bzextralen = compr_patch_buf_sz - HEADER_SIZE - bzctrllen - bzdifflen;

	/* Limiting the size to INT_MAX should be good enough */
	if (bzctrllen > INT_MAX || bzdifflen > INT_MAX || bzextralen > INT_MAX)
	{
		snprintf(errstr, sizeof(errstr), "Corrupt patch. Patch is too big.");
		return errstr;
	}

	dec_ctrl_sz = uncompr_ctrl_sz;
	if ((errmsg = decompress_block(compr_patch_buf + HEADER_SIZE, bzctrllen,
								   &dec_ctrl_buf, &dec_ctrl_sz)) != NULL)
		return errmsg;

	dec_diff_sz = uncompr_diff_sz;
	if ((errmsg = decompress_block(compr_patch_buf + HEADER_SIZE + bzctrllen, bzdifflen,
								   &dec_diff_buf, &dec_diff_sz)) != NULL)
	{
		free(dec_ctrl_buf);
		return errmsg;
	}

	dec_xtra_sz = uncompr_xtra_sz;
	if ((errmsg = decompress_block(compr_patch_buf + HEADER_SIZE + bzctrllen + bzdifflen, bzextralen,
								   &dec_xtra_buf, &dec_xtra_sz)) != NULL)
	{
		free(dec_diff_buf);
		free(dec_ctrl_buf);
		return errmsg;
	}

	/* Print decompressed ctrl/diff/xtra sizes */
	if (old_buf == NULL)
	{
		printf("Decompressed ctrl/diff/extra sizes are: %d/%d/%d.\n", dec_ctrl_sz, dec_diff_sz, dec_xtra_sz);
		free(dec_xtra_buf);
		free(dec_diff_buf);
		free(dec_ctrl_buf);
		return NULL;
	}

	if ((l_new_buf = (unsigned char *)malloc(l_new_size + 1)) == NULL)
	{
		snprintf(errstr, sizeof(errstr), "Cannot allocate %ld bytes to create the patch.", l_new_size + 1);
		free(dec_xtra_buf);
		free(dec_diff_buf);
		free(dec_ctrl_buf);
		return errstr;
	}

	pctrl_end = (pctrl = dec_ctrl_buf) + dec_ctrl_sz;
	pdiff_end = (pdiff = dec_diff_buf) + dec_diff_sz;
	pxtra_end = (pxtra = dec_xtra_buf) + dec_xtra_sz;

	oldpos = newpos = 0;

	while (newpos < l_new_size)
	{
		/* Read control data */
		if (pctrl > pctrl_end - 2 * 8)
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 1.");
		GETOUT:
			free(dec_xtra_buf);
			free(dec_diff_buf);
			free(dec_ctrl_buf);
			free(l_new_buf);
			return errstr;
		}
		for (i = 0; i <= 2; i++)
		{
			ctrl[i] = offtin(pctrl);
			pctrl += 8;
		}

		/* Sanity-check */
		if (ctrl[0] < 0 || ctrl[0] > INT_MAX ||
			(ctrl[1] < 0) || ctrl[1] > INT_MAX)
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 2.");
			goto GETOUT;
		}

		/* Overflow-check */
		if (add_off_t_overflow(newpos, ctrl[0], &res_add_off_t))
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 2.");
			goto GETOUT;
		}

		/* Sanity-check */
		if (res_add_off_t > l_new_size)
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 3.");
			goto GETOUT;
		}

		/* Read diff string */
		if (pdiff > pdiff_end - ctrl[0])
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 4.");
			goto GETOUT;
		}
		memcpy(l_new_buf + newpos, pdiff, ctrl[0]);
		pdiff += ctrl[0];

		/* Overflow-check */
		if (add_off_t_overflow(oldpos, ctrl[0], &res_add_off_t))
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 3.");
			goto GETOUT;
		}

		/* Add old data to diff string */
		for (i = 0; i < ctrl[0]; i++)
		{
			if (oldpos + i < old_size)
				l_new_buf[newpos + i] += old_buf[oldpos + i];
		}

		/* Adjust pointers */
		oldpos = res_add_off_t;
		if (add_off_t_overflow(newpos, ctrl[0], &res_add_off_t))
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 4.");
			goto GETOUT;
		}
		newpos = res_add_off_t;

		/* Overflow-check */
		if (add_off_t_overflow(newpos, ctrl[1], &res_add_off_t))
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 5.");
			goto GETOUT;
		}

		/* Sanity-check */
		if (res_add_off_t > l_new_size)
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 5.");
			goto GETOUT;
		}

		/* Read extra string */
		if (pxtra > pxtra_end - ctrl[1])
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch 6.");
			goto GETOUT;
		}
		memcpy(l_new_buf + newpos, pxtra, ctrl[1]);
		pxtra += ctrl[1];

		/* Adjust pointers */
		newpos = res_add_off_t;
		if (add_off_t_overflow(oldpos, ctrl[2], &res_add_off_t))
		{
			snprintf(errstr, sizeof(errstr), "Corrupt patch. Overflow 6.");
			goto GETOUT;
		}
		oldpos = res_add_off_t;
	}

	/* Clean up the bzip2 reads */
	free(dec_xtra_buf);
	free(dec_diff_buf);
	free(dec_ctrl_buf);

	*new_buf = l_new_buf;
	*new_size = l_new_size;
	return NULL;
}

/***************************************************************************/

char *bspatch(const char *oldfile, const char *newfile, const char *patchfile)
{
	unsigned char *in_buf, *out_buf, *patch_buf;
	int in_sz, out_sz, patch_sz;
	char *errmsg;

	errmsg = file_to_mem(oldfile, &in_buf, &in_sz);
	if (errmsg)
		return errmsg;

	errmsg = file_to_mem(patchfile, &patch_buf, &patch_sz);
	if (errmsg)
	{
		free(in_buf);
		return errmsg;
	}

	errmsg = bspatch_mem(in_buf, in_sz, &out_buf, &out_sz, patch_buf, patch_sz, -1, -1, -1);
	free(in_buf);
	free(patch_buf);
	if (errmsg)
		return errmsg;

	errmsg = mem_to_file(out_buf, out_sz, newfile);
	free(out_buf);
	return (errmsg) ? errmsg : NULL;
}

/***************************************************************************/

char *bspatch_info(const char *patchfile)
{
	unsigned char *dummy_buf, *patch_buf;
	int dummy_sz, patch_sz;
	char *errmsg;

	errmsg = file_to_mem(patchfile, &patch_buf, &patch_sz);
	if (errmsg)
	{
		return errmsg;
	}

	/* Passing NULL as the first argument to bspatch_mem will make it print
	   the decompressed ctrl/diff/xtra sizes of the patch file to the console.
	   These can then be used when calling bspatch_mem to speed up the memory
	   allocation */
	errmsg = bspatch_mem(NULL, 0, &dummy_buf, &dummy_sz, patch_buf, patch_sz, -1, -1, -1);
	free(patch_buf);
	return (errmsg) ? errmsg : NULL;
}

/***************************************************************************/
