/* Ten-simultaneously-live two-chain XOR body, WIDE order: five leaves
   loaded from Dst rows (loads are dst-access barriers; the arithmetic
   between the last load and the store is the scheduled region), eight
   distinct first-level pairs computed before any consumption spikes the
   region to ten live, then a balanced reduction.  Chain-wise
   consumption fits in the eight-register file, so the pre-RA
   pressure scheduler turns the refusing kernel into a compiling one.

   Parameters: FIRE_NAME, FIRE_ROW(i) input rows, FIRE_OUT output row,
   FIRE_FMT load/store mod0 format, FIRE_NOINC no-increment address
   mode (BH 7, WH 3).  XOR only (no constants, no CC, bit-exact under
   any issue order -- the dataflow is unchanged).  */
void FIRE_NAME (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, FIRE_ROW (0), 0, 0, FIRE_FMT,
				   FIRE_NOINC);
  auto b = __builtin_rvtt_sfpload (nullptr, FIRE_ROW (1), 0, 0, FIRE_FMT,
				   FIRE_NOINC);
  auto c = __builtin_rvtt_sfpload (nullptr, FIRE_ROW (2), 0, 0, FIRE_FMT,
				   FIRE_NOINC);
  auto d = __builtin_rvtt_sfpload (nullptr, FIRE_ROW (3), 0, 0, FIRE_FMT,
				   FIRE_NOINC);
  auto e = __builtin_rvtt_sfpload (nullptr, FIRE_ROW (4), 0, 0, FIRE_FMT,
				   FIRE_NOINC);
  auto u0 = __builtin_rvtt_sfpxor (a, b);
  auto u1 = __builtin_rvtt_sfpxor (b, c);
  auto u2 = __builtin_rvtt_sfpxor (c, d);
  auto u3 = __builtin_rvtt_sfpxor (d, e);
  auto u4 = __builtin_rvtt_sfpxor (e, a);
  auto u5 = __builtin_rvtt_sfpxor (a, c);
  auto u6 = __builtin_rvtt_sfpxor (b, d);
  auto u7 = __builtin_rvtt_sfpxor (c, e);
  auto r0 = __builtin_rvtt_sfpxor (u0, u1);
  auto r1 = __builtin_rvtt_sfpxor (u2, u3);
  auto r2 = __builtin_rvtt_sfpxor (u4, u5);
  auto r3 = __builtin_rvtt_sfpxor (u6, u7);
  auto s0 = __builtin_rvtt_sfpxor (r0, r1);
  auto s1 = __builtin_rvtt_sfpxor (r2, r3);
  auto out = __builtin_rvtt_sfpxor (s0, s1);
  __builtin_rvtt_sfpstore (nullptr, out, FIRE_OUT, 0, 0, FIRE_FMT,
			   FIRE_NOINC);
}
