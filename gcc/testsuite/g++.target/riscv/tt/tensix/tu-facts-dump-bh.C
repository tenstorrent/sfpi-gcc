// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fchecking=2 -mtt-tensix-optimize-const-residency -fdump-tree-rvtt_prgm_const-details" }
// Item #15: the TU-lifetime PRGM facts got a dump/verify surface -- the
// computed snapshot (claims, unique programmed values, creg readers,
// the CC audit) prints once at compute time, its structural invariants
// hard-assert under -fchecking, and the kernel-single-TU / crt0-benign
// axioms' decl-level footing (the entry-root enumeration) is recorded
// as a checked, dumpable property.
// { dg-final { scan-tree-dump "tu-facts: claimed 0x3000 value-known 0x3000 creg-read 0x1" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "tu-facts: dest 12 unique value 0x3f317218" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "tu-facts: cc audit clean" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "tu-facts: entry anchor none, 2 enumerable root\\(s\\), image rooted" "rvtt_prgm_const" } }

void owner_init_typed (void)
{
  auto ln2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (ln2, 12);
  auto k1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3dd8adac, 0, 0, 31);
  __builtin_rvtt_sfpwriteconfig_v (k1, 13);
}

void kernel (void)
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto gain = __builtin_rvtt_sfpxloadi (nullptr, 0x3f317218, 0, 0, 31);
      x = __builtin_rvtt_sfpmul (x, gain, 0);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc_v (x, 0);
      __builtin_rvtt_sfppopc (0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
