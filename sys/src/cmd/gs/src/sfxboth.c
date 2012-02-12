/* Copyright (C) 1997 Aladdin Enterprises.  All rights reserved.
  
  This software is provided AS-IS with no warranty, either express or
  implied.
  
  This software is distributed under license and may not be copied,
  modified or distributed except as expressly authorized under the terms
  of the license contained in the file LICENSE in this distribution.
  
  For more information about licensing, please refer to
  http://www.ghostscript.com/licensing/. For information on
  commercial licensing, go to http://www.artifex.com/licensing/ or
  contact Artifex Software, Inc., 101 Lucas Valley Road #110,
  San Rafael, CA  94903, U.S.A., +1(415)492-9861.
*/

/* $Id: sfxboth.c,v 1.4 2002/02/21 22:24:54 giles Exp $ */
/* File stream implementation using both stdio and direct OS calls */

#include "sfxstdio.c"

#define KEEP_FILENO_API		/* see sfxfd.c */
#include "sfxfd.c"
