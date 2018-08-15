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

#ifndef messages_h
#define messages_h

//*****************************************************************************
// global includes
//*****************************************************************************
#include <stdarg.h>
#include <stdbool.h>

#include <fmt.h>

#define omptrace_a_debug 50
#define omptrace_b_debug 0
#define omptrace_c_debug 0

#define DEBUG_GLOBAL

#define KA_TRACE(d, type, tid, tag, fmt, ...) if (omptrace_a_debug >= d) { omptrace_print_msg(type, tid, tag, fmt, __VA_ARGS__) ; }

#define KB_TRACE(d, type, tid, tag, fmt, ...) if (omptrace_b_debug >= d) { omptrace_print_msg(type, tid, tag, fmt, __VA_ARGS__) ; }

#define KC_TRACE(d, type, tid, tag, fmt, ...) if (omptrace_c_debug == d) { omptrace_print_msg(type, tid, tag, fmt, __VA_ARGS__) ; }

#define KN_TRACE(d, type, tid, tag, fmt, ...) omptrace_print_msg(type, tid, tag, fmt, __VA_ARGS__) ; 

enum {
    STDOUT = 1,
    STDERR = 2
};

void omptrace_print_msg(int output_type, int threadId, const char* tag, const char* fmt, ...);

#endif // messages_h
