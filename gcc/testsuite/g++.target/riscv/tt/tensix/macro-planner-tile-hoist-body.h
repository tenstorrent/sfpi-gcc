/* QSR's sfpsetcc and sfpswap carry an operand-type argument the other
   architectures do not have.  0 is IMM_TYPE_INT, which is what these
   bit-pattern subjects want.  Spelled as a token macro rather than an
   #if around the argument because some of these calls sit inside
   backslash-continued macro bodies.  */
#ifndef XTT_IMM_TYPE
#if __riscv_xtttensixqsr
#define XTT_IMM_TYPE , 0
#else
#define XTT_IMM_TYPE
#endif
#endif

/* The cross-tile prefix-elision shape: the compact select body
   inside a counted face loop, nested in a TILE loop carrying the
   LLK-shaped per-tile boilerplate -- raw addr-mod SETC16 words, the
   LaneConfig default reset, a sync word, and a dynamic MMIO push of a
   partially-constant thread-config word.  The tile loop is guarded
   (do/while under an unknown count), so the hoist target is the
   commit-time split of the loop's unique entry edge.

   TILE_EXTRA_BOILERPLATE lets a variant inject an additional per-tile
   word or store (the near-miss and unproven tests).  */

#ifndef TILE_TRUE_ADDR
#define TILE_TRUE_ADDR 32
#define TILE_FALSE_ADDR 64
#endif
#ifndef TILE_EXTRA_BOILERPLATE
#define TILE_EXTRA_BOILERPLATE()
#endif

#define TILE_ROW()                                                            \
  do                                                                          \
    {                                                                         \
      auto condition = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);       \
      auto on_true                                                            \
	= __builtin_rvtt_sfpload (nullptr, TILE_TRUE_ADDR, 0, 0, 6, 7);       \
      auto on_false                                                           \
	= __builtin_rvtt_sfpload (nullptr, TILE_FALSE_ADDR, 0, 0, 6, 7);      \
      __builtin_rvtt_sfppushc (0);                                            \
      __builtin_rvtt_sfpsetcc (condition, 2 XTT_IMM_TYPE);                               \
      auto result = __builtin_rvtt_sfpassign_lv (on_false, on_true);          \
      __builtin_rvtt_sfppopc (0);                                             \
      __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 6, 7);               \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

#define TILE_KERNEL(NAME)                                                     \
  __attribute__((noinline)) void NAME (unsigned tiles, unsigned faces,        \
				       volatile unsigned *sync_word)          \
  {                                                                           \
    __asm__ __volatile__ (".ttinsn %0" :: "n" (0xa6a1000a));                  \
    unsigned tile = 0;                                                        \
    do                                                                        \
      {                                                                       \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0xb20f0000));              \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2120000));              \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2220002));              \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0xb2350000));              \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0x910000f1));              \
	__asm__ __volatile__ (".ttinsn %0" :: "n" (0xa2800010));              \
	TILE_EXTRA_BOILERPLATE ();                                            \
	*sync_word = 0xb2010000u + ((tile & 1u) << 9);                        \
	unsigned face = 0;                                                    \
	do                                                                    \
	  {                                                                   \
	    TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();               \
	    TILE_ROW (); TILE_ROW (); TILE_ROW (); TILE_ROW ();               \
	  }                                                                   \
	while (++face < faces);                                               \
      }                                                                       \
    while (++tile < tiles);                                                   \
  }
