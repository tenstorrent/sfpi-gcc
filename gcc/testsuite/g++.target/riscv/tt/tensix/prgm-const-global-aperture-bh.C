// Check that stores through foldable TU global pointers classify, so the PRGM allocation fires.
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

// Caller-join provenance applies only to a genuinely closed private helper;
// a public entry can receive an arbitrary MMIO pointer from outside this TU.
__attribute__ ((noinline, noclone, noipa)) static void copy_into (params *dst)
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

  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}

// Same shape with different anchor names, a < bound and other constants.
vu32 *lane_counters = (vu32 *) 0xFFE00100;

void renamed_counter_clear ()
{
  for (vu32 *q = lane_counters; q < lane_counters + 16; ++q)
    *q = 0;
  // Both predicate flags and ROW_MASK are unknown at entry.
  __builtin_rvtt_sfpencc_all_lanes ();
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
  auto acc = __builtin_rvtt_sfpreadlreg (2);
  auto gain = __builtin_rvtt_sfpreadlreg (3);
  for (unsigned step = 0; step != 12; ++step)
    {
      auto blended = __builtin_rvtt_sfpmul (acc, gain, 0);
      acc = __builtin_rvtt_sfpaddi (nullptr, blended, 0x3f81, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 2);
}
