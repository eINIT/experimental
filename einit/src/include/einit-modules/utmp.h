/*
 *  utmp.h
 *  eINIT
 *
 *  Created by Magnus Deininger on 03/01/2007.
 *  Copyright 2006 Magnus Deininger. All rights reserved.
 *
 */

/*
Copyright (c) 2006, Magnus Deininger
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
	  this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
	  this list of conditions and the following disclaimer in the documentation
	  and/or other materials provided with the distribution.
    * Neither the name of the project nor the names of its contributors may be
	  used to endorse or promote products derived from this software without
	  specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef _EINIT_MODULES_UTMP_H
#define _EINIT_MODULES_UTMP_H

#include <utmp.h>
#include <string.h>
#include <einit/utility.h>

/* make sure we have these default constants, even on systems that don't have
   ut_type to work around compilation problems */

#ifndef UT_UNKNOWN
#define UT_UNKNOWN      0xe0
#endif
#ifndef RUN_LVL
#define RUN_LVL         0xe1
#endif
#ifndef BOOT_TIME
#define BOOT_TIME       0xe2
#endif
#ifndef NEW_TIME
#define NEW_TIME        0xe3
#endif
#ifndef OLD_TIME
#define OLD_TIME        0xe4
#endif
#ifndef INIT_PROCESS
#define INIT_PROCESS    0xe5
#endif
#ifndef LOGIN_PROCESS
#define LOGIN_PROCESS   0xe6
#endif
#ifndef USER_PROCESS
#define USER_PROCESS    0xe7
#endif
#ifndef DEAD_PROCESS
#define DEAD_PROCESS    0xe8
#endif
#ifndef ACCOUNTING
#define ACCOUNTING      0xe9
#endif

#define UTMP_CLEAN  0x01
#define UTMP_ADD    0x02
#define UTMP_MODIFY 0x04

typedef char (*utmp_function) (unsigned char, struct utmp *);

utmp_function __utmp_update_function;

#define update_utmp(options, record) ((__utmp_update_function || (__utmp_update_function = function_find_one("einit-utmp-update", 1, NULL))) ? __utmp_update_function(options, record) : -1)

#define utmp_configure(mod) __utmp_update_function = NULL;
#define utmp_cleanup(mod) __utmp_update_function = NULL;

#ifdef LINUX
#define create_utmp_record(record, utype, upid, uline, uid, uuser, uhost, ueterm, ueexit, usession) struct utmp record = { .ut_type = utype, \
  .ut_pid = upid,\
  .ut_exit = { \
   .e_termination = ueterm, \
   .e_exit = ueexit \
  }, \
  .ut_session = usession \
 }; \
 if (uline) {\
  char *tmpstr = estrdup (uline);\
  if (tmpstr) {\
   strncpy (record.ut_line, (strstr(tmpstr, "/dev/") == tmpstr ? tmpstr + 5 : tmpstr), UT_LINESIZE); \
   free (tmpstr);\
  }\
 } \
 else memset (record.ut_line, 0, UT_LINESIZE);\
 if (uid)   strncpy (record.ut_id,   uid,   4);           else memset (record.ut_id, 0, 4); \
 if (uuser) strncpy (record.ut_user, uuser, UT_NAMESIZE); else memset (record.ut_user, 0, UT_NAMESIZE); \
 if (uhost) strncpy (record.ut_host, uhost, UT_HOSTSIZE); else memset (record.ut_host, 0, UT_HOSTSIZE); \
 { struct timeval tv; \
   gettimeofday(&tv, NULL); \
   record.ut_tv.tv_sec = tv.tv_sec; \
   record.ut_tv.tv_usec = tv.tv_usec; }

#else
#define create_utmp_record(record, utype, upid, uline, uid, uuser, uhost, ueterm, ueexit, usession) struct utmp record = {  };
#endif

#endif