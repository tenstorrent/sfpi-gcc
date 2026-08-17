/* Two shapes whose only independent ready instruction sits deeper than the
   one-slot window of the adjacent latency fill: the dependent consumer is a
   swap (dynamic-delay erratum class), so without a filler the nop inserter
   pads the bubble with an SFPNOP.  The generalized shadow fill must move
   the deep independent instruction into the slot instead.  The second
   function is the renamed, constant-varied twin (different producer opcode,
   immediates, registers): the decision keys only on proven independence
   and the audited effect classes.  */

void shadow_fill_deep_swap ()
{
  auto a = __builtin_rvtt_sfpreadlreg (0);
  auto b = __builtin_rvtt_sfpreadlreg (1);
  auto c = __builtin_rvtt_sfpreadlreg (2);
  auto p = __builtin_rvtt_sfpmul (a, a, 0);
  auto r = __builtin_rvtt_sfpswap (p, b, 1);
  auto p1 = __builtin_rvtt_sfpselect2 (r, 0);
  auto b1 = __builtin_rvtt_sfpselect2 (r, 1);
  auto f = __builtin_rvtt_sfpmul (c, c, 0);
  __builtin_rvtt_sfpwritelreg (p1, 0);
  __builtin_rvtt_sfpwritelreg (b1, 1);
  __builtin_rvtt_sfpwritelreg (f, 2);
}

void renamed_scaled_exchange ()
{
  auto north = __builtin_rvtt_sfpreadlreg (4);
  auto south = __builtin_rvtt_sfpreadlreg (5);
  auto east = __builtin_rvtt_sfpreadlreg (6);
  auto scaled = __builtin_rvtt_sfpmuli (nullptr, north, 0x3f81, 0, 0, 0);
  auto pair = __builtin_rvtt_sfpswap (scaled, south, 1);
  auto lo = __builtin_rvtt_sfpselect2 (pair, 0);
  auto hi = __builtin_rvtt_sfpselect2 (pair, 1);
  auto other = __builtin_rvtt_sfpadd (east, east, 0);
  __builtin_rvtt_sfpwritelreg (lo, 4);
  __builtin_rvtt_sfpwritelreg (hi, 5);
  __builtin_rvtt_sfpwritelreg (other, 6);
}
