// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-unroll-loops -mtt-tensix-optimize-replay-hoist -mtt-tensix-optimize-replay-record-hoist -mtt-tensix-replay-hoist-min-benefit=0 -fdump-rtl-rvtt_replay-details" }
// The page shell contains a typed replay owner outside the row loop.  It could
// overwrite persistent slots between pages, so outer promotion must refuse;
// ordinary per-page counted formation remains legal.
// { dg-final { scan-rtl-dump "Persistent counted ownership blocker: typed-replay-owner" "rvtt_replay" } }
// { dg-final { scan-rtl-dump "Persistent counted hoist refused: outer-loop-opaque" "rvtt_replay" } }
// { dg-final { scan-rtl-dump-not "Persistent counted hoist:" "rvtt_replay" } }

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
persistent_page_owner_refuse (volatile unsigned *io)
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  for (unsigned page = 0; page != 8; ++page)
    {
      if (*io & 1)
        io[1] = page;
      else
        io[2] = page;
      __builtin_rvtt_ttreplay (nullptr, 2, 0, 0, 30, 0, 0);
      for (unsigned row = 0; row != 8; ++row)
        { ROW_BODY; }
      io[3] = page;
    }
  __builtin_rvtt_sfpwritelreg (a, 0);
  __builtin_rvtt_sfpwritelreg (b, 1);
  __builtin_rvtt_sfpwritelreg (c, 2);
}
