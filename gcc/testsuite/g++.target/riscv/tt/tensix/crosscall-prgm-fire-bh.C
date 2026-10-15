// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-exceptions -fno-rtti -mtt-tensix-optimize-const-residency -mtt-tensix-optimize-crosscall-hoist -mtt-tensix-optimize-crosscall-prgm -fdump-tree-rvtt_prgm_const-details" }
// Cross-call PRGM programming FIRE: residency parks each noinline
// callee's in-loop constant in a programmable constant register on the
// callee's own loop entry, i.e. once per call.  The programming moves
// to the caller and is lifted to the batch loop's entry -- once per
// kernel, the hand-init discipline.  The second pair is a renamed twin
// with a different (single-word) constant; the register the first move
// claims stays claimed TU-wide, so the twin's residency takes another.
// { dg-final { scan-tree-dump-times "crosscall-prgm: moved 1 programming pair" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "contract placement lifted to enclosing loop" 2 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L12 for constant" 1 "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-times "allocated PRGM L13 for constant" 1 "rvtt_prgm_const" } }
// { dg-final { scan-assembler-times "SFPCONFIG" 2 } }

__attribute__((noinline)) void
cpf_callee ()
{
  for (int row = 0; row != 8; ++row)
    {
      auto x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b1a3d, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfpstore (nullptr, x, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

int cpf_pace;

void
cpf_caller (int batches, int tiles)
{
  for (int b = 0; b != batches; ++b)
    {
      cpf_pace = b;			/* scalar batch bookkeeping */
      for (int t = 0; t != tiles; ++t)
	cpf_callee ();
    }
}

__attribute__((noinline)) void
renamed_scale_rows ()
{
  for (int step = 0; step != 16; ++step)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, 7);
      auto two = __builtin_rvtt_sfpxloadi (nullptr, 0x40000000, 0, 0, 31);
      v = __builtin_rvtt_sfpmul (v, two, 0);
      __builtin_rvtt_sfpstore (nullptr, v, 0, 0, 0, 6, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}

void
renamed_tile_driver (int passes, int count)
{
  for (int p = 0; p != passes; ++p)
    for (int i = 0; i != count; ++i)
      renamed_scale_rows ();
}
