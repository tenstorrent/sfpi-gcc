// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Renamed + varied twin of the entry-rooted slot refusal: a
// differently-named extern "C" entry (the firmware->run_kernel shape,
// no `main' at all), different coefficient values, a runtime trip
// count, and the slot SFPLOADI destination moved to another
// allocatable register (L5).  Rooting must key on external visibility,
// never on the entry's name; the refusal must be identical.
// { dg-final { scan-tree-dump "refused .crossloop-mop-slot-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump "TU template audit: proven loadi-dests=0x20" "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];

static inline void yq_seed_template (unsigned yo, unsigned yi)
{
  ((vu32 *) 0xFFB80000)[0] = yo;
  ((vu32 *) 0xFFB80000)[1] = yi;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;    // Tensix NOP start op
  ((vu32 *) 0xFFB80000)[3] = 0x37020044;    // SETRWC end op
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = 0x7159abcd;    // SFPLOADI -> L5 (allocatable)
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
}

__attribute__((noinline)) void
yq_tile_sweep (int yn)
{
  yq_seed_template (6, 3);
  for (int yt = 0; yt != yn; ++yt)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));   // TTI_MOP
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int yr = 0; yr != 8; ++yr)
	{
	  auto q0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3d75c28f, 0, 0, -32);
	  auto q1 = __builtin_rvtt_sfpxloadi (nullptr, 0xbf35c28f, 0, 0, -32);
	  auto yx = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  yx = __builtin_rvtt_sfpmad (yx, q0, q1, 0);
	  __builtin_rvtt_sfpstore (nullptr, yx, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

extern "C" void
yq_kernel_entry (int yn)
{
  yq_tile_sweep (yn);
}
