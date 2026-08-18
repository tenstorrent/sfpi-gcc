// Global-aperture near misses: (1) a global pointer that is stored a
// SECOND value is not foldable -- its assumed loads refuse the TU by
// name; (2) a bounded fill whose range reaches the instruction-buffer
// block is not inert; (3) a fill with a non-divisible != bound cannot
// prove it stops.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "global-pointer-value-unproven: reprogrammable_base" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-store-alias-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

typedef volatile unsigned int vu32;

vu32 *reprogrammable_base = (vu32 *) 0xFFE80000;
vu32 *wide_aperture = (vu32 *) 0xFFE3FF00;

__attribute__ ((noinline)) void sync_via_base ()
{
  unsigned raw = 0;
  asm volatile ("sw %0, (%1)\n\tlw %0, (%1)\n\tand x0, x0, %0"
		: "+r" (raw) : "r" (&reprogrammable_base[2]) : "memory");
}

void retarget_then_sync ()
{
  reprogrammable_base = (vu32 *) 0xFFE7F000;	// divergent store
  sync_via_base ();
}

// Range [0xFFE3FF00, 0xFFE40100) overlaps the instruction aperture.
void fill_reaches_the_fifo ()
{
  for (vu32 *p = wide_aperture; p != wide_aperture + 128; ++p)
    *p = 0;
}

// Non-divisible != bound: ascent by 8 can step over the bound.
void fill_bound_not_divisible (void)
{
  vu32 *base = (vu32 *) 0xFFE00000;
  volatile char *c = (volatile char *) base;
  for (unsigned i = 0; i != 100; i += 8)
    *(vu32 *) (c + i + 1) = 0;
}

void wants_a_constant ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
