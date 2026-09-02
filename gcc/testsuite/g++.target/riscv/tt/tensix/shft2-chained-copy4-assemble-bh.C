// SFPSHFT2 SUBVEC_CHAINED_COPY4 must ASSEMBLE (a binutils
// fix): the insn template printed "SFPSHFT2\t%x0 %x0, ..."
// with the comma missing, so gas rejected every emission of the chained
// form.  dg-do assemble makes the assembler the oracle; the
// scan-assembler twin pins the corrected spelling.
// { dg-do assemble }
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

void chained_copy4 ()
{
  auto v0 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v1 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v2 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto v3 = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);

  auto r = __builtin_rvtt_sfpshft2_subvec_copy4 (v0, v1, v2, v3, 1);
  v0 = __builtin_rvtt_sfpselect4 (r, 0);
  v1 = __builtin_rvtt_sfpselect4 (r, 1);
  v2 = __builtin_rvtt_sfpselect4 (r, 2);
  v3 = __builtin_rvtt_sfpselect4 (r, 3);

  __builtin_rvtt_sfpstore (nullptr, v0, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v1, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v2, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpstore (nullptr, v3, 0, 0, 0, 0, 0);
}
// (No scan-assembler here: dg-do assemble keeps only the object file --
// the assembler exit status IS the oracle.  The corrected spelling is
// pinned by the shft2-26462-{bh,wh,qsr}.C check-function-bodies twins.)
