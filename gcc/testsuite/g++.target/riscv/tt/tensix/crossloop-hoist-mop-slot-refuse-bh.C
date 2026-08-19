// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-invariant-loadi -mtt-tensix-optimize-crossloop-hoist -fdump-tree-rvtt_crossloop" }
// Census near miss: a template slot programs an SFPLOADI whose
// destination is an allocatable register -- any MOP launch in a
// scanned loop could rewrite a hoisted live range; refuse by name.
// { dg-final { scan-tree-dump "refused .crossloop-mop-slot-unproven." "rvtt_crossloop" } }
// { dg-final { scan-tree-dump-not "hoisted across" "rvtt_crossloop" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];

static inline void xlh_mopr_program_template (unsigned outer, unsigned inner)
{
  ((vu32 *) 0xFFB80000)[0] = outer;
  ((vu32 *) 0xFFB80000)[1] = inner;
  ((vu32 *) 0xFFB80000)[2] = 0x02000000;
  ((vu32 *) 0xFFB80000)[3] = 0x37020044;
  ((vu32 *) 0xFFB80000)[4] = 0x02000000;
  ((vu32 *) 0xFFB80000)[5] = 0x71312345;    // SFPLOADI -> L3 (allocatable)
  ((vu32 *) 0xFFB80000)[6] = 0x02000000;
  ((vu32 *) 0xFFB80000)[7] = 0x02000000;
  ((vu32 *) 0xFFB80000)[8] = 0x02000000;
}

__attribute__((noinline)) void
xlh_mopr_kernel (int tiles)
{
  xlh_mopr_program_template (4, 2);
  for (int t = 0; t != tiles; ++t)
    {
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x01800000));
      __asm__ __volatile__ (".ttinsn %0" : : "n" (0x37000104));
      for (int row = 0; row != 8; ++row)
	{
	  auto c0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, -32);
	  auto c1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f2e8ba3, 0, 0, -32);
	  auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
	  x = __builtin_rvtt_sfpmad (x, c0, c1, 0);
	  __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
	  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
	}
    }
}

// Root the TU's executable closure (AXIOM kernel-single-TU): the
// census audits template stores only along call paths from the link
// entry.
extern "C" void _start () { xlh_mopr_kernel (4); }
