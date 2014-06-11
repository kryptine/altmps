/* mpmss.c: MPM STRESS TEST
 *
 * $Id$
 * Copyright (c) 2001-2014 Ravenbrook Limited.  See end of file for license.
 * Portions copyright (C) 2002 Global Graphics Software.
 */

#include "mpscmv.h"
#include "mpscmvff.h"
#include "mpscmfs.h"
#include "mpslib.h"
#include "mpsavm.h"
#include "testlib.h"
#include "mpslib.h"
#include "mps.h"

#include <stdio.h> /* printf */


#define testArenaSIZE   ((((size_t)64)<<20) - 4)
#define smallArenaSIZE  ((((size_t)1)<<20) - 4)
#define testSetSIZE 200
#define testLOOPS 10


/* stress -- create a pool of the requested type and allocate in it */

static mps_res_t stress(mps_arena_t arena, size_t (*size)(size_t i),
                        const char *name, mps_class_t pool_class,
                        mps_arg_s *args)
{
  mps_res_t res;
  mps_pool_t pool;
  size_t i, k;
  int *ps[testSetSIZE];
  size_t ss[testSetSIZE];

  printf("%s\n", name);

  res = mps_pool_create_k(&pool, arena, pool_class, args);
  if (res != MPS_RES_OK)
    return res;

  /* allocate a load of objects */
  for (i=0; i<testSetSIZE; ++i) {
    ss[i] = (*size)(i);

    res = mps_alloc((mps_addr_t *)&ps[i], pool, ss[i]);
    if (res != MPS_RES_OK)
      return res;
    if (ss[i] >= sizeof(ps[i]))
      *ps[i] = 1; /* Write something, so it gets swap. */
  }

  mps_pool_check_fenceposts(pool);

  for (k=0; k<testLOOPS; ++k) {
    /* shuffle all the objects */
    for (i=0; i<testSetSIZE; ++i) {
      size_t j = rnd()%(testSetSIZE-i);
      void *tp;
      size_t ts;
     
      tp = ps[j]; ts = ss[j];
      ps[j] = ps[i]; ss[j] = ss[i];
      ps[i] = tp; ss[i] = ts;
    }
    /* free half of the objects */
    /* upper half, as when allocating them again we want smaller objects */
    /* see randomSize() */
    for (i=testSetSIZE/2; i<testSetSIZE; ++i) {
      mps_free(pool, (mps_addr_t)ps[i], ss[i]);
      /* if (i == testSetSIZE/2) */
      /*   PoolDescribe((Pool)pool, mps_lib_stdout); */
    }
    /* allocate some new objects */
    for (i=testSetSIZE/2; i<testSetSIZE; ++i) {
      ss[i] = (*size)(i);
      res = mps_alloc((mps_addr_t *)&ps[i], pool, ss[i]);
      if (res != MPS_RES_OK) return res;
    }
  }
   
  mps_pool_destroy(pool);

  return MPS_RES_OK;
}


/* randomSize -- produce sizes both large and small */

static size_t randomSize(size_t i)
{
  /* Make the range large enough to span three pages in the segment table: */
  /* 160 segments/page, page size max 0x2000. */
  size_t maxSize = 2 * 160 * 0x2000;
  /* Reduce by a factor of 2 every 10 cycles.  Total allocation about 40 MB. */
  return rnd() % max((maxSize >> (i / 10)), 2) + 1;
}


/* randomSize8 -- produce sizes both large and small, 8-byte aligned */

static size_t randomSize8(size_t i)
{
  size_t maxSize = 2 * 160 * 0x2000;
  /* Reduce by a factor of 2 every 10 cycles.  Total allocation about 40 MB. */
  return alignUp(rnd() % max((maxSize >> (i / 10)), 2) + 1, 8);
}


/* fixedSize -- produce always the same size */

static size_t fixedSizeSize = 0;

static size_t fixedSize(size_t i)
{
  testlib_unused(i);
  return fixedSizeSize;
}


static mps_pool_debug_option_s bothOptions = {
  /* .fence_template = */   "post",
  /* .fence_size = */       4,
  /* .free_template = */    "DEAD",
  /* .free_size = */        4
};

static mps_pool_debug_option_s fenceOptions = {
  /* .fence_template = */   "123456789abcdef",
  /* .fence_size = */       15,
  /* .free_template = */    NULL,
  /* .free_size = */        0
};

/* testInArena -- test all the pool classes in the given arena */

static void testInArena(mps_arena_class_t arena_class, mps_arg_s *arena_args,
                        mps_pool_debug_option_s *options)
{
  mps_arena_t arena;

  die(mps_arena_create_k(&arena, arena_class, arena_args),
      "mps_arena_create");

  MPS_ARGS_BEGIN(args) {
    mps_align_t align = sizeof(void *) << (rnd() % 4);
    MPS_ARGS_ADD(args, MPS_KEY_ALIGN, align);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_ARENA_HIGH, TRUE);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_SLOT_HIGH, TRUE);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_FIRST_FIT, TRUE);
    die(stress(arena, randomSize8, "MVFF", mps_class_mvff(), args),
        "stress MVFF");
  } MPS_ARGS_END(args);

  MPS_ARGS_BEGIN(args) {
    mps_align_t align = sizeof(void *) << (rnd() % 4);
    MPS_ARGS_ADD(args, MPS_KEY_ALIGN, align);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_ARENA_HIGH, TRUE);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_SLOT_HIGH, TRUE);
    MPS_ARGS_ADD(args, MPS_KEY_MVFF_FIRST_FIT, TRUE);
    MPS_ARGS_ADD(args, MPS_KEY_POOL_DEBUG_OPTIONS, options);
    die(stress(arena, randomSize8, "MVFF debug", mps_class_mvff_debug(), args),
        "stress MVFF debug");
  } MPS_ARGS_END(args);

  MPS_ARGS_BEGIN(args) {
    mps_align_t align = (mps_align_t)1 << (rnd() % 6);
    MPS_ARGS_ADD(args, MPS_KEY_ALIGN, align);
    die(stress(arena, randomSize, "MV", mps_class_mv(), args),
        "stress MV");
  } MPS_ARGS_END(args);

  MPS_ARGS_BEGIN(args) {
    mps_align_t align = (mps_align_t)1 << (rnd() % 6);
    MPS_ARGS_ADD(args, MPS_KEY_ALIGN, align);
    MPS_ARGS_ADD(args, MPS_KEY_POOL_DEBUG_OPTIONS, options);
    die(stress(arena, randomSize, "MV debug", mps_class_mv_debug(), args),
        "stress MV debug");
  } MPS_ARGS_END(args);

  MPS_ARGS_BEGIN(args) {
    fixedSizeSize = 1 + rnd() % 64;
    MPS_ARGS_ADD(args, MPS_KEY_MFS_UNIT_SIZE, fixedSizeSize);
    MPS_ARGS_ADD(args, MPS_KEY_EXTEND_BY, 100000);
    die(stress(arena, fixedSize, "MFS", mps_class_mfs(), args),
      "stress MFS");
  } MPS_ARGS_END(args);

  mps_arena_destroy(arena);
}


int main(int argc, char *argv[])
{
  testlib_init(argc, argv);

  MPS_ARGS_BEGIN(args) {
    MPS_ARGS_ADD(args, MPS_KEY_ARENA_SIZE, testArenaSIZE);
    testInArena(mps_arena_class_vm(), args, &bothOptions);
  } MPS_ARGS_END(args);

  MPS_ARGS_BEGIN(args) {
    MPS_ARGS_ADD(args, MPS_KEY_ARENA_SIZE, smallArenaSIZE);
    testInArena(mps_arena_class_vm(), args, &fenceOptions);
  } MPS_ARGS_END(args);

  printf("%s: Conclusion: Failed to find any defects.\n", argv[0]);
  return 0;
}


/* C. COPYRIGHT AND LICENSE
 *
 * Copyright (c) 2001-2014 Ravenbrook Limited <http://www.ravenbrook.com/>.
 * All rights reserved.  This is an open source license.  Contact
 * Ravenbrook for commercial licensing options.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. Redistributions in any form must be accompanied by information on how
 * to obtain complete source code for this software and any accompanying
 * software that uses this software.  The source code must either be
 * included in the distribution or be available for no more than the cost
 * of distribution plus a nominal fee, and must be freely redistributable
 * under reasonable conditions.  For an executable file, complete source
 * code means the source code for all modules it contains. It does not
 * include source code for modules or files that typically accompany the
 * major components of the operating system on which the executable file
 * runs.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, OR NON-INFRINGEMENT, ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
