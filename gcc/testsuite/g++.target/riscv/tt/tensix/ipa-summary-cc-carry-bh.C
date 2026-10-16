// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -fchecking=2 -mtt-tensix-optimize-crosscall-hoist -fdump-tree-rvtt_crosscall" }
// Check the IPA summary's CC-carry fact: CC-quiet and balanced pushc/popc bodies preserve the ambient CC, an opaque raw word does not.
// { dg-final { scan-tree-dump "ipa-summary: cc-carry void kernel_clean\\(unsigned int\\)/\\d+: ambient-preserving" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "ipa-summary: cc-carry void kernel_framed\\(unsigned int\\)/\\d+: ambient-preserving" "rvtt_crosscall" } }
// { dg-final { scan-tree-dump "ipa-summary: cc-carry void kernel_opaque\\(unsigned int\\)/\\d+: unproven" "rvtt_crosscall" } }

using vec_t = __xtt_vector;

void
kernel_clean (unsigned n)
{
  for (unsigned i = 0; i != n; ++i)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
    }
}

void
kernel_framed (unsigned n)
{
  for (unsigned i = 0; i != n; ++i)
    {
      vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppushc (0);
      __builtin_rvtt_sfpsetcc (a, 0);
      vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
      __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
      __builtin_rvtt_sfppopc (0);
    }
}

void
kernel_opaque (unsigned n)
{
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  asm volatile (".ttinsn %0" :: "i" (0x91000080));
  vec_t p = __builtin_rvtt_sfpmul (a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, p, 0, 0, 0, 0, 7);
}
