// Narrow (chain-wise) source order of the fire body: compiles at plain
// flags with no relief -- the wide twin's refusal is purely an issue
// ORDER artifact, and this form is the CRAQ golden definer for the
// scheduled wide twin (identical dataflow, bit-identical outputs).
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops" }

void fire_seq_ten (void)
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 4, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 4, 7);
  auto c = __builtin_rvtt_sfpload (nullptr, 4, 0, 0, 4, 7);
  auto d = __builtin_rvtt_sfpload (nullptr, 6, 0, 0, 4, 7);
  auto e = __builtin_rvtt_sfpload (nullptr, 8, 0, 0, 4, 7);
  auto u0 = __builtin_rvtt_sfpxor (a, b);
  auto u1 = __builtin_rvtt_sfpxor (b, c);
  auto r0 = __builtin_rvtt_sfpxor (u0, u1);
  auto u2 = __builtin_rvtt_sfpxor (c, d);
  auto u3 = __builtin_rvtt_sfpxor (d, e);
  auto r1 = __builtin_rvtt_sfpxor (u2, u3);
  auto s0 = __builtin_rvtt_sfpxor (r0, r1);
  auto u4 = __builtin_rvtt_sfpxor (e, a);
  auto u5 = __builtin_rvtt_sfpxor (a, c);
  auto r2 = __builtin_rvtt_sfpxor (u4, u5);
  auto u6 = __builtin_rvtt_sfpxor (b, d);
  auto u7 = __builtin_rvtt_sfpxor (c, e);
  auto r3 = __builtin_rvtt_sfpxor (u6, u7);
  auto s1 = __builtin_rvtt_sfpxor (r2, r3);
  auto out = __builtin_rvtt_sfpxor (s0, s1);
  __builtin_rvtt_sfpstore (nullptr, out, 192, 0, 0, 4, 7);
}
