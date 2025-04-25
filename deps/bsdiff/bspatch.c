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

#include <stdio.h>
#include "bspatchlib.h"

int main(int argc, char *argv[])
{
	char *errmsg;

	if (argc == 2)
	{
		/* Call bspatch_info with the single argument (patchfile) */
		if ((errmsg = bspatch_info(argv[1])) != NULL)
		{
			printf("%s\n", errmsg);
			return 1;
		}
	}
	else if (argc == 4)
	{
		/* Call bspatch with three arguments (oldfile, newfile, patchfile) */
		if ((errmsg = bspatch(argv[1], argv[2], argv[3])) != NULL)
		{
			printf("%s\n", errmsg);
			return 1;
		}
	}
	else
	{
		printf("usage:\n");
		printf("  for patching: %s oldfile newfile patchfile\n", argv[0]);
		printf("  for info: %s patchfile\n", argv[0]);
		return 1;
	}

	return 0;
}
