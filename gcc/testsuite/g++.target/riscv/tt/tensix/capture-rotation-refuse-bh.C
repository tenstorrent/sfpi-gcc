// Near misses for capture rotation, each refusing by name.
// 1. A rotation that would break the entry-boundary dependency refuses:
//    the filler's destination is read EARLIER in the row, i.e. the read
//    consumes the value carried across the row boundary from the
//    previous trip -- the prologue copy would change what the first
//    row's reader sees.
// 2. Vacating the row's head exposes an equal loop-carried stall at the
//    seam: no modeled stall decrease, byte-identical refusal.
// 3. A stalled gap behind a producer without an audited result latency
//    (SFPARECIP carries no entry; SFPGT's pure SET_VD form is
//    audited since the ccmask increment) refuses by name.
// (The multi-writer wall -- the exp sfpmov refusal "writes a register
// another row member also writes" -- is register-allocation dependent
// at dg level; the exp dump evidence carries that name.)
// Note: the interior plain-reorder mover legitimately serves shape 1
// (no prologue executes, so the entry-boundary read is untouched: the
// invariant-input AND moves between the muls within each trip).  The
// prologue mover's named refusal still stands and the prologue itself
// must never fire here.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fno-shrink-wrap -fno-unroll-loops -mtt-tensix-optimize-capture-rotation -mno-tt-tensix-optimize-replay -fdump-rtl-rvtt_schedule-details" }
// { dg-final { scan-rtl-dump "Capture rotation refused: filler uid=\\d+ carries a live value across the row boundary" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: no modeled stall decrease rotating uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump "Capture rotation refused: unaudited result latency after uid=\\d+" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "with prologue" "rvtt_schedule" } }
// { dg-final { scan-rtl-dump-not "moved uid=\\d+ to the seam" "rvtt_schedule" } }

void carried_value_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto w = __builtin_rvtt_sfpreadlreg (1);
  auto k1 = __builtin_rvtt_sfpreadlreg (2);
  auto k2 = __builtin_rvtt_sfpreadlreg (3);
  auto t = __builtin_rvtt_sfpand (k1, k2);
  for (unsigned row = 0; row != 20; ++row)
    {
      // Reads the PREVIOUS trip's t: an entry-boundary dependency.
      w = __builtin_rvtt_sfpxor (w, t);
      t = __builtin_rvtt_sfpand (k1, k2);
      auto p = __builtin_rvtt_sfpmul (x, x, 0);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
  __builtin_rvtt_sfpwritelreg (w, 1);
}

void seam_exposure_refuses ()
{
  auto x = __builtin_rvtt_sfpreadlreg (4);
  auto k1 = __builtin_rvtt_sfpreadlreg (5);
  auto k2 = __builtin_rvtt_sfpreadlreg (6);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto t = __builtin_rvtt_sfpand (k1, k2);
      // Consumes the seam producer's x: moving t out of the head makes
      // this adjacent to the previous trip's last mul -- net zero.
      auto y = __builtin_rvtt_sfpadd (x, t, 0);
      auto p = __builtin_rvtt_sfpmul (y, y, 0);
      x = __builtin_rvtt_sfpmul (p, p, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 4);
}

void unaudited_gap_refuses ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto q = __builtin_rvtt_sfpreadlreg (2);
  auto f = __builtin_rvtt_sfpreadlreg (3);
  auto g = __builtin_rvtt_sfpreadlreg (4);
  for (unsigned row = 0; row != 20; ++row)
    {
      auto p = __builtin_rvtt_sfparecip (a, 0);
      q = __builtin_rvtt_sfpxor (q, p);
      f = __builtin_rvtt_sfpor (f, f);
      g = __builtin_rvtt_sfpand (g, g);
    }
  __builtin_rvtt_sfpwritelreg (q, 2);
  __builtin_rvtt_sfpwritelreg (f, 3);
  __builtin_rvtt_sfpwritelreg (g, 4);
}
