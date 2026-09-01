/* Exhaustive equivalence sweep: ccmask keep-mask folds, the EQ and NE
   float directions against +0.0, over every 2^32 lane encoding
   (FABLE_GOES_BURR R2 widening 2: the EQ/NE class named by the
   standing ccmask-compare-direction-unsupported refusal).

   Region (CUT) semantics -- the structured CC lowering the fold
   deletes, lifted verbatim from the pinned craq-sim and the compiler's
   own expansion:
     - rvtt_emit_sfpxfcmps (gcc/config/riscv/tt/rvtt.cc, the fval==0
       arm, rvtt_cmp_ex_to_setcc_mod1_map): EQ -> SETCC mod6
       (LREG_EQ0); NE -> SETCC mod2 (LREG_NE0).  Single word each; no
       COMPC.
     - TENSIX_EXECUTE_SFPSETCC (craq-sim src/tensix.cpp:8969-9000
       @ 9f324140): mod bit1 selects raw-bits != 0, mod bit2 the
       complement; the comparison tests the RAW 32-bit encoding ("SFPI
       uses 0x80000000 as a lane predicate/sign sentinel.  SFPSETCC
       tests that raw bit, rather than folding it into IEEE -0"), so
       EQ enables exactly the +0 encoding (raw == 0; -0 = 0x80000000
       stays DISABLED) and NE its complement.
     - The predicated zeroing assign writes 0 exactly on enabled lanes.

   Fold (HW) semantics -- the replacement words:
     - TENSIX_EXECUTE_SFPGT / TENSIX_EXECUTE_SFPLE mod1==8 SET_DEST
       (craq-sim src/tensix.cpp:10236-10261 / 10210-10235 @ 9f324140):
       write ~0/0 into the FIRST (tied destination) operand by
       sign_mag32_total_order comparison (src/tensix.cpp:3816-3822).
     - TENSIX_EXECUTE_SFPOR (src/tensix.cpp:9085-9101): bitwise or.
     - TENSIX_EXECUTE_SFPAND (src/tensix.cpp:9068-9084): bitwise and.
   Direction -> keep-mask composition (matching gimple-rvtt-ccmask.cc):
     x == 0: SFPOR  (SFPGT (x, 0), SFPGT (0, x))   [keep raw != 0]
     x != 0: SFPAND (SFPLE (x, 0), SFPLE (0, x))   [keep raw == 0]
   where the direct-operand compare reads the constant +0 register and
   the swapped-operand compare overwrites the region's own writable
   zero materialization with SET_DEST (the LT/GE arms' discipline).

   Equivalence obligation: for every x, mask == (enabled(x) ? 0 : ~0);
   then z & mask == (enabled ? 0 : z) for every z by bit algebra, i.e.
   the fold reproduces the lane merge exactly (both zeros, both NaN
   sign classes, infinities included: sign_mag32_total_order is a
   bijection with sx(+0) == 0 and sx(-0) == -1, so sx != 0 is exactly
   raw != 0).

   Build/run: cc -O2 sweep_ccmask_eqne.c -o sweep && ./sweep  */

#include <stdint.h>
#include <stdio.h>

static inline int32_t sign_mag32_total_order (uint32_t x)
{
  /* craq-sim src/tensix.cpp:3816-3822.  */
  return (x & 0x80000000u) ? (int32_t) (x ^ 0x7FFFFFFFu) : (int32_t) x;
}

int main (void)
{
  const char *name[2] = { "EQ (x==0)", "NE (x!=0)" };
  uint64_t mism[2] = { 0, 0 };
  /* Position-salted FNV-1a 64 stream commitments, cut vs hw, per
     direction (the ccmask-direction-complete discipline).  */
  uint64_t hc[2], hh[2];
  for (int d = 0; d < 2; d++)
    hc[d] = hh[d] = 0xcbf29ce484222325ull;

  uint32_t x = 0;
  do
    {
      int zero = (x == 0);	/* raw-bit zero: +0 only */
      int32_t sx = sign_mag32_total_order (x);

      /* CUT enabled sets (single-SETCC lowering).  */
      int en[2];
      en[0] = zero;		/* EQ: SETCC mod6 (LREG_EQ0) */
      en[1] = !zero;		/* NE: SETCC mod2 (LREG_NE0) */

      /* HW keep-masks (SET_DEST total-order compare compositions).  */
      uint32_t gt_x0 = (sx > 0) ? 0xFFFFFFFFu : 0;   /* SFPGT (x, 0) */
      uint32_t gt_0x = (0 > sx) ? 0xFFFFFFFFu : 0;   /* SFPGT (0, x) */
      uint32_t le_x0 = (sx <= 0) ? 0xFFFFFFFFu : 0;  /* SFPLE (x, 0) */
      uint32_t le_0x = (0 <= sx) ? 0xFFFFFFFFu : 0;  /* SFPLE (0, x) */
      uint32_t mk[2];
      mk[0] = gt_x0 | gt_0x;	/* EQ keep: SFPOR of the two GTs */
      mk[1] = le_x0 & le_0x;	/* NE keep: SFPAND of the two LEs */

      uint64_t salt = (uint64_t) x * 0x9E3779B97F4A7C15ull;
      for (int d = 0; d < 2; d++)
	{
	  uint32_t cut = en[d] ? 0 : 0xFFFFFFFFu;	/* expected keep */
	  uint32_t hw = mk[d];
	  if (cut != hw)
	    mism[d]++;
	  hc[d] = (hc[d] ^ (cut + salt)) * 0x100000001B3ull;
	  hh[d] = (hh[d] ^ (hw + salt)) * 0x100000001B3ull;
	}
    }
  while (++x != 0);

  int equal = 1;
  for (int d = 0; d < 2; d++)
    {
      printf ("%s  mismatches=%llu  cut-stream=%016llx  hw-stream=%016llx"
	      "  %s\n",
	      name[d], (unsigned long long) mism[d],
	      (unsigned long long) hc[d], (unsigned long long) hh[d],
	      (mism[d] == 0 && hc[d] == hh[d]) ? "EQUAL" : "NOT-EQUAL");
      if (mism[d] != 0 || hc[d] != hh[d])
	equal = 0;
    }
  printf ("RESULT: %s over 2^32 x 2 directions\n",
	  equal ? "EQUAL" : "NOT-EQUAL");
  return equal ? 0 : 1;
}
