#ifndef BSPATCHLIBH__
#define BSPATCHLIBH__
/*-
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

#ifdef __cplusplus
extern "C"
{
#endif

	/* Returns NULL on success, and an error message otherwise. */
	char *file_to_mem(const char *fname, unsigned char **buf, int *buf_sz);

	/* Same */
	char *mem_to_file(const unsigned char *buf, int buf_sz, const char *fname);

	/* Same */
	char *bspatch_mem(const unsigned char *old_buf, int old_size,
					  unsigned char **new_buf, int *new_size,
					  const unsigned char *compr_patch_buf, int compr_patch_buf_sz,
					  int uncompr_ctrl_sz, int uncompr_diff_sz, int uncompr_xtra_sz);

	/* Same */
	char *bspatch(const char *oldfile, const char *newfile,
				  const char *patchfile);

	/* Same */
	char *bspatch_info(const char *patchfile);

#ifdef __cplusplus
}
#endif

#endif /* BSPATCHLIBH__ */
