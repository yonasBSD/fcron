/*
 * FCRON - periodic command scheduler 
 *
 *  Copyright 2000-2001 Thibault Godouet <fcron@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 * 
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *  The GNU General Public License can also be found in the file
 *  `LICENSE' that comes with the fcron source distribution.
 */

 /* $Id: fcrontab.h,v 1.9 2001-05-17 00:52:55 thib Exp $ */

#ifndef __FCRONTAB_H__
#define __FCRONTAB_H__

#include "global.h"


/* macros */
#define Skip_blanks(ptr) \
        while((*ptr == ' ') || (*ptr == '\t')) \
	    ptr++;

/* global variables */
extern char debug_opt;
extern CF *file_base;
extern char *user;
extern uid_t uid;
extern uid_t asuid;
extern uid_t fcrontab_uid;

/* prototype definition */

#include "log.h"
#include "subs.h"
#include "allow.h"
#include "fileconf.h"

#endif /* __FCRONTAB_H__ */
