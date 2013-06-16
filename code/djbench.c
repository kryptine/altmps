/* djban.c -- "DJ" Benchmark on ANSI C library
 * $Id$
 */

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <alloca.h>
#include "getopt.h"
#include "testlib.h"

#include "mps.c"


static mps_arena_t arena;
static mps_pool_t pool;
static mps_ap_t ap;


/* The benchmark behaviour is defined as a macro in order to give realistic
   opportunities for compiler optimisation and the intended inlining of the
   MPS functions. */

static unsigned niter = 100;      /* iterations */
static unsigned npass = 1000;     /* passes over blocks */
static unsigned nblocks = 1000;   /* number of blocks */
static unsigned sshift = 18;      /* log2 max block size in words */
static double prob = 0.2;         /* probability per pass of acting */

#define DJRUN(alloc, free) \
  do { \
    struct {void *p; size_t s;} *blocks = alloca(sizeof(*blocks) * nblocks); \
    unsigned i, j, k; \
    \
    for (k = 0; k < nblocks; ++k) { \
      blocks[k].p = NULL; \
    } \
    \
    for (i = 0; i < niter; ++i) { \
      for (j = 0; j < npass; ++j) { \
        for (k = 0; k < nblocks; ++k) { \
          if (rnd() % 16384 < prob * 16384) { \
            if (blocks[k].p == NULL) { \
              size_t s = rnd() % ((sizeof(void *) << (rnd() % sshift)) - 1); \
              void *p = NULL; \
              if (s > 0) alloc(p, s); \
              blocks[k].p = p; \
              blocks[k].s = s; \
            } else { \
              free(blocks[k].p, blocks[k].s); \
              blocks[k].p = NULL; \
            } \
          } \
        } \
      } \
      \
      for (k = 0; k < nblocks; ++k) { \
        if (blocks[k].p) { \
          free(blocks[k].p, blocks[k].s); \
          blocks[k].p = NULL; \
        } \
      } \
    } \
  } while(0)


/* malloc/free benchmark */

#define MALLOC_ALLOC(p, s) do { p = malloc(s); } while(0)
#define MALLOC_FREE(p, s)  do { free(p); } while(0)

static void dj_malloc(void) {
  DJRUN(MALLOC_ALLOC, MALLOC_FREE);
}

/* mps_alloc/mps_free benchmark */

#define MPS_ALLOC(p, s) do { mps_alloc(&p, pool, s); } while(0)
#define MPS_FREE(p, s)  do { mps_free(pool, p, s); } while(0)

static void dj_alloc(void) {
  DJRUN(MPS_ALLOC, MPS_FREE);
}

/* reserve/free benchmark */

#define ALIGN_UP(s, a) (((s) + ((a) - 1)) & ~((a) - 1))
#define RESERVE_ALLOC(p, s) \
  do { \
    size_t _s = ALIGN_UP(s, (size_t)MPS_PF_ALIGN); \
    mps_reserve(&p, ap, _s); \
    mps_commit(ap, p, _s); \
  } while(0)
#define RESERVE_FREE(p, s)  do { mps_free(pool, p, s); } while(0)

static void dj_reserve(void) {
  DJRUN(RESERVE_ALLOC, RESERVE_FREE);
}


/* Wrap a call to dj benchmark that doesn't require MPS setup */

static void wrap(void (*dj)(void), mps_class_t dummy, const char *name)
{
  clock_t start, finish;
  (void)dummy;
  
  start = clock();
  dj();
  finish = clock();
  
  printf("%s: %g\n", name, (double)(finish - start) / CLOCKS_PER_SEC);
}


/* Wrap a call to a dj benchmark that requires MPS setup */

#define MUST(expr) \
  do { \
    mps_res_t res = (expr); \
    if (res != MPS_RES_OK) { \
      fprintf(stderr, #expr " returned %d\n", res); \
      exit(EXIT_FAILURE); \
    } \
  } while(0)

static void arena_wrap(void (*dj)(void), mps_class_t pool_class, const char *name)
{
  MPS_ARGS_BEGIN(args) {
    MPS_ARGS_ADD(args, MPS_KEY_ARENA_SIZE, 256ul * 1024 * 1024); /* FIXME: Why is there no default? */
    MPS_ARGS_DONE(args);
    MUST(mps_arena_create_k(&arena, mps_arena_class_vm(), args));
  } MPS_ARGS_END(args);
  MUST(mps_pool_create_k(&pool, arena, pool_class, mps_args_none));
  MUST(mps_ap_create_k(&ap, pool, mps_args_none));
  wrap(dj, NULL, name);
  mps_ap_destroy(ap);
  mps_pool_destroy(pool);
  mps_arena_destroy(arena);
}


/* Command-line options definitions.  See getopt_long(3). */

static struct option longopts[] = {
  {"niter",   required_argument,  NULL,   'i'},
  {"npass",   required_argument,  NULL,   'p'},
  {"nblocks", required_argument,  NULL,   'b'},
  {"sshift",  required_argument,  NULL,   's'},
  {"prob",    required_argument,  NULL,   'r'},
  {NULL,      0,                  NULL,   0}
};


/* Test definitions. */

static mps_class_t dummy_class(void)
{
  return NULL;
}

static struct {
  const char *name;
  void (*wrap)(void (*)(void), mps_class_t, const char *name);
  void (*dj)(void);
  mps_class_t (*pool_class)(void);
} pools[] = {
  {"mvt",   arena_wrap, dj_reserve, mps_class_mvt},
  {"mvff",  arena_wrap, dj_reserve, mps_class_mvff},
  {"mv",    arena_wrap, dj_alloc,   mps_class_mv},
  {"mvb",   arena_wrap, dj_reserve, mps_class_mv}, /* mv with buffers */
  {"an",    wrap,       dj_malloc,  dummy_class},
};


/* Command-line driver */

int main(int argc, char *argv[]) {
  int ch;
  unsigned i;
  
  while ((ch = getopt_long(argc, argv, "i:p:b:s:r:", longopts, NULL)) != -1)
    switch (ch) {
    case 'i':
      niter = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 'p':
      npass = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 'b':
      nblocks = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 's':
      sshift = (unsigned)strtoul(optarg, NULL, 10);
      break;
    case 'r':
      prob = strtod(optarg, NULL);
      break;
    default:
      fprintf(stderr,
              "Usage: %s [option...] [test...]\n"
              "Options:\n"
              "  -i n, --niter=n\n"
              "    Iterate each test n times (default %u).\n"
              "  -p n, --npass=n\n"
              "    Pass over the block array n times (default %u).\n"
              "  -b n, --nblocks=n\n"
              "    Length of the block array (default %u).\n"
              "  -s n, --sshift=n\n"
              "    Log2 max block size in words (default %u).\n"
              "  -r p, --prob=p\n"
              "    Probability of acting on a block (default %g).\n"
              "Tests:\n"
              "  mvt   pool class MVT\n"
              "  mvff  pool class MVFF\n"
              "  mv    pool class MV\n"
              "  mvb   pool class MV with buffers\n"
              "  an    malloc\n",
              argv[0],
              niter,
              npass,
              nblocks,
              sshift,
              prob);
      return EXIT_FAILURE;
    }
  argc -= optind;
  argv += optind;
  
  while (argc > 0) {
    for (i = 0; i < sizeof(pools) / sizeof(pools[0]); ++i)
      if (strcmp(argv[0], pools[i].name) == 0) {
        pools[i].wrap(pools[i].dj, pools[i].pool_class(), pools[i].name);
        goto found;
      }
    fprintf(stderr, "unknown pool test \"%s\"\n", argv[0]);
    return EXIT_FAILURE;
  found:
    --argc;
    ++argv;
  }
  
  return EXIT_SUCCESS;
}
