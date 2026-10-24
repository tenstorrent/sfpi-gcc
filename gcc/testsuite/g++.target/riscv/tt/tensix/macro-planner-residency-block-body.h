#ifndef RVTT_TEST_LANE_RESET
#define RVTT_TEST_LANE_RESET() ((void) 0)
#endif

/* Descriptor-residency shape: the tile-hoist kernel nested in a guarded
   block loop.  BLOCK_PRE and BLOCK_POST inject a word at block level or
   after the block loop.  */

#include "macro-planner-tile-hoist-body.h"

#ifndef BLOCK_PRE
#define BLOCK_PRE()
#endif
#ifndef BLOCK_POST
#define BLOCK_POST()
#endif
#ifndef TILE_LANE_PROLOGUE
#define TILE_LANE_PROLOGUE()
#endif

#define BLOCK_KERNEL(NAME)                                                    \
  __attribute__((noinline)) void NAME (unsigned blocks, unsigned tiles,       \
				       unsigned faces,                        \
				       volatile unsigned *sync_word)          \
  {                                                                           \
    RVTT_TEST_LANE_RESET ();                                                  \
    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xa6a1000a));                  \
    unsigned block = 0;                                                       \
    do                                                                        \
      {                                                                       \
	BLOCK_PRE ();                                                         \
	unsigned tile = 0;                                                    \
	do                                                                    \
	  {                                                                   \
	    TILE_LANE_PROLOGUE ();                                            \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xb20f0000));          \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2120000));          \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2220002));          \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2350000));          \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0x910000f1));          \
	    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xa2800010));          \
	    TILE_EXTRA_BOILERPLATE ();                                        \
	    *sync_word = 0xb2010000u + ((tile & 1u) << 9);                    \
	    unsigned face = 0;                                                \
	    do                                                                \
	      {                                                               \
		TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();           \
		TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();           \
	      }                                                               \
	    while (++face < faces);                                           \
	  }                                                                   \
	while (++tile < tiles);                                               \
      }                                                                       \
    while (++block < blocks);                                                 \
    BLOCK_POST ();                                                            \
  }
