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
// Copyright ((c)) 2002-2020, Rice University
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

//***************************************************************************
//
// File:
//   $HeadURL$
//
// Purpose:
//   Define an API for the MCS lock: a fair queue-based lock.
//
// Reference:
//   John M. Mellor-Crummey and Michael L. Scott. 1991. Algorithms for scalable
//   synchronization on shared-memory multiprocessors. ACM Transactions on
//   Computing Systems 9, 1 (February 1991), 21-65.
//   http://doi.acm.org/10.1145/103727.103729
//***************************************************************************

// rename function/structure names to follow to romp code style 

#ifndef _mcsLock_h_
#define _mcsLock_h_

//******************************************************************************
// global includes
//******************************************************************************

#include <atomic>
#include <stdbool.h>


//******************************************************************************
// types
//******************************************************************************

typedef struct McsNode {
  std::atomic<struct McsNode*> next;
  std::atomic_bool blocked;
} McsNode;


typedef struct {
  std::atomic<McsNode*> tail;
} McsLock;



//******************************************************************************
// constants
//******************************************************************************

#define MCS_NIL (struct McsNode*) 0

//******************************************************************************
// interface functions
//******************************************************************************

static inline void
mcsInit(McsLock *l)
{
  std::atomic_init(&l->tail, MCS_NIL);
}


void
mcsLock(McsLock *l, McsNode *me);


bool
mcsTryLock(McsLock *l, McsNode *me);


void
mcsUnlock(McsLock *l, McsNode *me);

/*
 * A custom implementation of lock guard that wraps mcs locking/unlocking
 */
class LockGuard {
public:
  LockGuard(McsLock* lock, McsNode* node): _lock(lock), _node(node) {
    mcsLock(_lock,  _node);
  }
  ~LockGuard() {
    mcsUnlock(_lock, _node);   
  }
private:
  McsLock* _lock;
  McsNode* _node;
};
#endif
