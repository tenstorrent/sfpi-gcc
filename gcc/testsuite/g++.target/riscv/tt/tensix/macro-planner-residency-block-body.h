/* WP13 descriptor-residency shapes.

   BLOCK_KERNEL: the WP11 cross-tile elision shape (TILE_ROW / the
   per-tile LLK boilerplate from macro-planner-tile-hoist-body.h)
   nested in one FURTHER enclosing guarded loop (the "block" loop of
   the ttnn perf harness).  WP11 alone hoists the descriptor words to
   the tile loop's entry -- INSIDE the block loop, once per block; the
   residency extension re-proves the epoch discipline over the block
   loop's body and moves them once further out, once per kernel.

   BLOCK_PRE / BLOCK_POST let a variant inject a word at block level
   (extension-stop near miss) or after the block loop (skip-path near
   miss).  */

#include "macro-planner-tile-hoist-body.h"

#ifndef BLOCK_PRE
#define BLOCK_PRE()
#endif
#ifndef BLOCK_POST
#define BLOCK_POST()
#endif

#define BLOCK_KERNEL(NAME)                                                    \
  __attribute__((noinline)) void NAME (unsigned blocks, unsigned tiles,       \
				       unsigned faces,                        \
				       volatile unsigned *sync_word)          \
  {                                                                           \
    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xa6a1000a));                  \
    unsigned block = 0;                                                       \
    do                                                                        \
      {                                                                       \
	BLOCK_PRE ();                                                         \
	unsigned tile = 0;                                                    \
	do                                                                    \
	  {                                                                   \
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
