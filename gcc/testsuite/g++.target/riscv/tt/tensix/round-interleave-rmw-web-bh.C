// Storage-collision rename THROUGH a read-modify-write redefinition
// (regression for the bring-up wrong-code catch): the round body's
// value chain runs mov -> in-place muli (SFPMULI's destination IS its
// source) -> reduction add.  The allocator packs both copies' chains
// into one LREG; the rename web must carry the RMW muli and the add's
// read ALONG with the fresh mov definition (3 insns) -- ending a web
// at the next writer exclusive would rename the mov away from its
// consumer and leave copy 2 consuming copy 1's half-scaled register.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -mtt-tensix-optimize-round-interleave -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_round_interleave -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-tree-dump "round-interleave: requested unroll 2 of loop" "rvtt_round_interleave" } }
// { dg-final { scan-rtl-dump "List-schedule rename: reg \\d+ -> \\d+ web at uid=\\d+ \\(3 insns\\)" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "List-schedule \\(round-interleave cyclic\\): bb \\d+ nodes=6 II \\d+ -> \\d+ renames=1 target=bh" "rvtt_schedule" } }

void rmw_web_rounds ()
{
  auto x   = __builtin_rvtt_sfpreadlreg (1);
  auto acc = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned r = 0; r != 32; ++r)
    {
      auto t = __builtin_rvtt_sfpmov (x, 1);
      t = __builtin_rvtt_sfpmuli (nullptr, t, 16128, 0, 0, 0);
      acc = __builtin_rvtt_sfpadd (acc, t, 0);
    }
  __builtin_rvtt_sfpwritelreg (acc, 0);
}
