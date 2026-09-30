// Foldable TU global pointers (the LLK aperture convention: a
// TU-defined, never-address-taken global pointer with a constant
// initializer and no divergent store) fold at their loads, so the
// blocking-store idiom through pc_buf_base classifies at the PC_BUF
// MOP_SYNC word, the parameter caller-join proves the runtime-copy
// helper's stores land in a caller-local object, and the bounded fill
// over the GPR aperture proves its whole range inert.  Fire leg.
// The renamed twin varies the anchors and constants.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump-times "prgm-const: allocated PRGM L\\d+ for invariant immediate" 2 "rvtt_prgm_const" } }
// { dg-final { scan-assembler "SFPMAD" } }

typedef volatile unsigned int vu32;

vu32 *pc_buf_base = (vu32 *) 0xFFE80000;
vu32 *regfile = (vu32 *) 0xFFE00000;

static inline void sync_blocking (vu32 *w)
{
  unsigned raw = 0;
  asm volatile ("sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0"
		: "+r" (raw) : "r" (w) : "memory");
}

struct params { unsigned words[16]; };

__attribute__ ((noinline)) void copy_into (params *dst)
{
  volatile char *d = (volatile char *) dst;
  for (unsigned i = 0; i != sizeof (params); ++i)
    d[i] = 0;
}

void kernel_with_apertures ()
{
  sync_blocking (&pc_buf_base[2]);	// mop_sync
  for (vu32 *p = regfile; p != regfile + 64; ++p)
    *p = 0;				// GPR-file fill, bounded IV
  params locals;
  copy_into (&locals);			// parameter caller-join

  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// Renamed twin: different anchor names, a < bound, other constants.
vu32 *lane_counters = (vu32 *) 0xFFE00100;

void renamed_counter_clear ()
{
  for (vu32 *q = lane_counters; q < lane_counters + 16; ++q)
    *q = 0;
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto gain = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto blended = __builtin_rvtt_sfpmul (acc, gain, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, blended, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
