// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// A volatile acquire/copy diamond separates the page-loop header from the
// counted SFPU row preheader, and volatile pack/pop traffic follows the row.
// None owns replay state, so the one record must still precede the page loop.
// The row first promotes across four faces, then across eight pages.
// { dg-final { scan-rtl-dump-times "Persistent counted hoist: loop \\d+ across outer loop \\d+ \\(4 trips\\)" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Persistent counted hoist: loop \\d+ across outer loop \\d+ \\(8 trips\\)" 1 "rvtt_replay" } }
// { dg-final { scan-rtl-dump-times "Hoisted no-exec capture \\[0,\\+18\\) to preheader" 1 "rvtt_replay" } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 18, 0, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\\t0, 18, 0, 0" 8 } }

#define ROW_BODY                                                       \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, b, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, c, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, a, 0);                                 \
  a = __builtin_rvtt_sfpmul (a, a, 0);                                 \
  b = __builtin_rvtt_sfpmul (b, b, 0);                                 \
  c = __builtin_rvtt_sfpmul (c, c, 0)

extern "C" void
persistent_page_shell (volatile unsigned *acquire,
                       volatile unsigned *pack)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned page = 0; page != 8; ++page)
    {
      // Force a multi-block, replay-transparent page prelude.
      if (*acquire & 1)
        acquire[1] = page;
      else
        acquire[2] = page + 1;
      // A fixed non-REPLAY, non-MOP raw word is a sequence boundary, but
      // cannot overwrite or consume replay-buffer ownership.
      asm volatile (".ttinsn %0" :: "n" (0xa6a1000a));

      for (unsigned face = 0; face != 4; ++face)
        for (unsigned row = 0; row != 8; ++row)
          { ROW_BODY; }

      // Representative replay-transparent page epilogue.
      pack[page & 1] = page;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
