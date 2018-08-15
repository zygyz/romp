// -*-Mode: C++;-*- // technically C99

// * BeginRiceCopyright *****************************************************
//
// $HeadURL$
// $Id$
//
// --------------------------------------------------------------------------
// Part of HPCToolkit (hpctoolkit.org)
//
// Information about sources of support for research and development of
// HPCToolkit is at 'hpctoolkit.org' and in 'README.Acknowledgments'.
// --------------------------------------------------------------------------
//
// Copyright ((c)) 2002-2016, Rice University
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// * Redistributions of source code must retain the above copyright
//   notice, this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright
//   notice, this list of conditions and the following disclaimer in the
//   documentation and/or other materials provided with the distribution.
//
// * Neither the name of Rice University (RICE) nor the names of its
//   contributors may be used to endorse or promote products derived from
//   this software without specific prior written permission.
//
// This software is provided by RICE and contributors "as is" and any
// express or implied warranties, including, but not limited to, the
// implied warranties of merchantability and fitness for a particular
// purpose are disclaimed. In no event shall RICE or contributors be
// liable for any direct, indirect, incidental, special, exemplary, or
// consequential damages (including, but not limited to, procurement of
// substitute goods or services; loss of use, data, or profits; or
// business interruption) however caused and on any theory of liability,
// whether in contract, strict liability, or tort (including negligence
// or otherwise) arising in any way out of the use of this software, even
// if advised of the possibility of such damage.
//
// ******************************************************* EndRiceCopyright *

//*****************************************************************************
// File: messages-async.c 
//
// Description:
//   This half of the messaging system must be async-signal safe.
//   Namely, it must be safe to call these operations from within signal 
//   handlers. Operations in this file may not directly or indirectly 
//   allocate memory using malloc. They may not call functions that
//   acquire locks that may be already held by the application code or 
//   functions that may be called during synchronous profiler operations
//   that may be interrupted by asynchronous profiler operations.
//
// History:
//   19 July 2009 
//     created by partitioning pmsg.c into messages-sync.c and 
//     messages-async.c
//
//*****************************************************************************



//*****************************************************************************
// global includes 
//*****************************************************************************

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>



//*****************************************************************************
// local includes 
//*****************************************************************************
#include "messages.h"
#include "messages.i"
#include "fmt.h"

//*****************************************************************************
// macros
//*****************************************************************************

#define DEBUG_PMSG_ASYNC 0

//*****************************************************************************
// constants
//*****************************************************************************

static const unsigned int msg_limit = 5000; // limit log file lines

//*****************************************************************************
// global variables
//*****************************************************************************

//spinlock_t pmsg_lock = SPINLOCK_UNLOCKED;

//*****************************************************************************
// (file) local variables
//*****************************************************************************

static unsigned int msgs_out = 0;
static bool check_limit = true;    // by default, limit messages

//*****************************************************************************
// forward declarations
//*****************************************************************************

static void create_msg(char *buf, size_t buflen, int threadId,
		       const char *tag, const char *fmt, va_list_box* box);

//*****************************************************************************
// interface operations
//*****************************************************************************


// like nmsg, except echo message to stderr when flag is set

void
omptrace_print_msg(int output_type, int threadId, const char *tag, const char* fmt, ...)
{
    va_list_box box;
    va_list_box_start(box, fmt);
    omptrace_write_msg_to_log(output_type, threadId, tag, fmt, &box);
}

//*****************************************************************************
// interface operations (within message subsystem) 
//*****************************************************************************
void
omptrace_write_msg_to_log(int output_type, int threadId, 
		          const char *tag, 
			  const char *fmt, va_list_box *box)
{
    char buf[MSG_BUF_SIZE];  
    create_msg(&buf[0], sizeof(buf), threadId, tag, fmt, box);
    va_list_boxp_end(box);
    write(output_type,buf,strlen(buf));
}

//*****************************************************************************
// private operations
//*****************************************************************************
static void
create_msg(char *buf, size_t buflen, int threadId, const char *tag,
	   const char *fmt, va_list_box* box)
{
    char fstr[MSG_BUF_SIZE];

    fstr[0] = '\0';

    if(threadId >= 0) {
	hpcrun_msg_ns(fstr, sizeof(fstr), "[%d]: ", threadId);
    }

    if (tag) {
	char* fstr_end = fstr + strlen(fstr);
	hpcrun_msg_ns(fstr_end, sizeof(fstr) - strlen(fstr), "%-5s: ", tag);
    }
    strncat(fstr, fmt, MSG_BUF_SIZE - strlen(fstr) - 5);
    strcat(fstr,"\n");
    hpcrun_msg_vns(buf, buflen - 2, fstr, box);
}
