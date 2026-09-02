/* Copyright (C) 2026 Tenstorrent Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

/* Exhaustive equivalence sweep: ccmask keep-mask folds, ALL FOUR float
   order directions against +0.0, over every 2^32 lane encoding.

   Region (CUT) semantics -- the structured CC lowering the fold
   deletes, lifted verbatim from the pinned craq-sim and the compiler's
   own expansion:
     - rvtt_emit_sfpxfcmps (gcc/config/riscv/tt/rvtt.cc:513-556, the
       fval==0 arm): LT -> SETCC mod0; GE -> SETCC mod4;
       GT -> SETCC mod4 then SETCC mod2; LE -> GT's pair then COMPC.
     - TENSIX_EXECUTE_SFPSETCC (craq-sim src/tensix.cpp:8969-9000):
       mod0 selects raw bit31 set, mod4 its complement, mod2 raw != 0;
       a second SETCC runs only on still-enabled lanes (intersection).
     - TENSIX_EXECUTE_SFPCOMPC (craq-sim src/tensix.cpp:9458-9477):
       complement within the enclosing frame.
     - The predicated zeroing assign writes 0 exactly on enabled lanes.

   Fold (HW) semantics -- the two replacement words:
     - TENSIX_EXECUTE_SFPGT / TENSIX_EXECUTE_SFPLE mod1==8 SET_DEST
       (craq-sim src/tensix.cpp:10236-10261 / 10210-10235): write
       ~0/0 into the FIRST (tied destination) operand by
       sign_mag32_total_order comparison (src/tensix.cpp:3816-3822).
     - TENSIX_EXECUTE_SFPAND (src/tensix.cpp:9068-9084): bitwise.
   Direction -> mask word (matching gimple-rvtt-ccmask.cc):
     x <= 0: SFPGT (x, 0)   [zero on the read side, CREG ok]
     x >  0: SFPLE (x, 0)
     x <  0: SFPLE (0, x)   [swapped: zero is the written operand]
     x >= 0: SFPGT (0, x)

   Equivalence obligation: for every x, mask == (enabled(x) ? 0 : ~0);
   then z & mask == (enabled ? 0 : z) for every z by bit algebra, i.e.
   the fold reproduces the lane merge exactly (both zeros, both NaN
   sign classes, infinities included).

   Build/run: cc -O2 sweep_ccmask_dirs.c -o sweep && ./sweep  */

#include <stdint.h>
#include <stdio.h>

static inline int32_t sign_mag32_total_order (uint32_t x)
{
  /* craq-sim src/tensix.cpp:3816-3822.  */
  return (x & 0x80000000u) ? (int32_t) (x ^ 0x7FFFFFFFu) : (int32_t) x;
}

int main (void)
{
  const char *name[4] = { "LE (x<=0)", "GT (x>0)", "LT (x<0)", "GE (x>=0)" };
  uint64_t mism[4] = { 0, 0, 0, 0 };
  /* FNV-1a 64 stream commitments, cut vs hw, per direction.  */
  uint64_t hc[4], hh[4];
  for (int d = 0; d < 4; d++)
    hc[d] = hh[d] = 0xcbf29ce484222325ull;

  uint32_t x = 0;
  do
    {
      int neg = (x & 0x80000000u) != 0;
      int zero = (x == 0);
      int32_t sx = sign_mag32_total_order (x);

      /* CUT enabled sets (SETCC/COMPC lowering).  */
      int en[4];
      en[0] = !(!neg && !zero);        /* LE: GTE0, NE0, COMPC */
      en[1] = (!neg && !zero);         /* GT: GTE0, NE0        */
      en[2] = neg;                     /* LT: mod0             */
      en[3] = !neg;                    /* GE: mod4             */

      /* HW keep-masks (SET_DEST total-order compares).  */
      uint32_t mk[4];
      mk[0] = (sx > 0) ? 0xFFFFFFFFu : 0;   /* SFPGT (x, 0) */
      mk[1] = (sx <= 0) ? 0xFFFFFFFFu : 0;  /* SFPLE (x, 0) */
      mk[2] = (0 <= sx) ? 0xFFFFFFFFu : 0;  /* SFPLE (0, x) */
      mk[3] = (0 > sx) ? 0xFFFFFFFFu : 0;   /* SFPGT (0, x) */

      /* Position-salted FNV-1a: long constant runs of 0/~0 otherwise
	 leave the iterated affine step with too little per-element
	 entropy for the commitment to discriminate.  */
      uint64_t salt = (uint64_t) x * 0x9E3779B97F4A7C15ull;
      for (int d = 0; d < 4; d++)
	{
	  uint32_t want = en[d] ? 0 : 0xFFFFFFFFu;
	  if (mk[d] != want)
	    mism[d]++;
	  hc[d] = (hc[d] ^ (want ^ salt)) * 0x100000001b3ull;
	  hh[d] = (hh[d] ^ (mk[d] ^ salt)) * 0x100000001b3ull;
	}
      x++;
    }
  while (x != 0);

  uint64_t total = 0;
  for (int d = 0; d < 4; d++)
    {
      printf ("%-10s mismatches=%llu  cut-stream=%016llx  hw-stream=%016llx  %s\n",
	      name[d], (unsigned long long) mism[d],
	      (unsigned long long) hc[d], (unsigned long long) hh[d],
	      hc[d] == hh[d] && mism[d] == 0 ? "EQUAL" : "NOT-EQUAL");
      total += mism[d];
    }
  printf ("RESULT: %s over 2^32 x 4 directions\n",
	  total == 0 ? "EQUAL" : "NOT-EQUAL");
  return total != 0;
}
