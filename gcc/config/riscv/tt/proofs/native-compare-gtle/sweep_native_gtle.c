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

/* sweep_native_gtle.c — native-compare lowering equivalence, GT and LE
   float compare-against-+0.0 arms, exhaustive over 2^32 lane encodings.

   Cut under proof (-mtt-tensix-optimize-native-compare, lane GW):
     GT web  {SFPSETCC mod4 (sign clear); SFPSETCC mod2 (bits != 0)}
       -> SFPGT VD=v, VC=L9(+0.0), Mod1=SET_CC
     LE web  {SFPSETCC mod4; SFPSETCC mod2; SFPCOMPC}
       -> SFPLE VD=v, VC=L9(+0.0), Mod1=SET_CC

   Oracle provenance: craq-sim pinned @ 9f324140
     SFPSETCC  src/tensix.cpp:8969-9000 (mod4: cc = !sign; mod2: cc = bits!=0;
               sequential setccs AND under the running enable mask)
     SFPCOMPC  complement against the fence push (stack top == entry enables)
     SFPGT     src/tensix.cpp:10236-10261 (mod1==1 SET_CC arm)
     SFPLE     src/tensix.cpp:10210-10235 (mod1==1 SET_CC arm)
     sign_mag32_total_order src/tensix.cpp:3816-3822
   ISA provenance: tt-isa-documentation BlackholeA0 SFPGT.md/SFPLE.md
     (SignMagIsSmaller; total order -NaN < -Inf < .. < -0 < +0 < .. < +NaN).
   Compiler-side lowering provenance: rvtt_emit_sfpxfcmps / rvtt_emit_sfpxfcmpv
     (GT/LE arms), gcc/config/riscv/tt/rvtt.cc.

   Per-lane model (lane enabled at entry; disabled lanes are unchanged
   by both spellings — SETCC/COMPC only touch enabled lanes and the
   fenced complement reconstructs exactly the entry enables; SFPGT/SFPLE
   SET_CC writes only enabled lanes):
     web_gt(v)    = !(v >> 31) && (v != 0)
     web_le(v)    = !web_gt(v)
     native_gt(v) = smto(v) >  smto(0x00000000)
     native_le(v) = smto(v) <= smto(0x00000000)

   Build: cc -O2 -fopenmp sweep_native_gtle.c -o sweep_native_gtle
   Streams are position-salted FNV-1a 64 over the per-x expected (web)
   and computed (native) lane predicate; equality per direction plus
   mismatches == 0 is the licensing condition.  Retire the
   -mtt-tensix-optimize-native-compare arms if this ever stops EQUAL.  */

#include <stdint.h>
#include <stdio.h>

static inline int32_t smto (uint32_t v)
{
  /* sign_mag32_total_order: remap sign-mag -0..-(2^31-1) to
     two's-comp -1..-2^31 (craq-sim tensix.cpp:3816; identical to the
     SFPGT.md/SFPLE.md SignMagIsSmaller exposition).  */
  v ^= (uint32_t) ((int32_t) v >> 30) >> 1;
  return (int32_t) v;
}

static inline uint64_t fnv1a64_step (uint64_t h, uint64_t v)
{
  h ^= v;
  h *= 0x100000001b3ULL;
  return h;
}

int main (void)
{
  const int NCHUNK = 256;
  uint64_t mm_gt = 0, mm_le = 0;
  uint64_t web_gt_h[256], nat_gt_h[256], web_le_h[256], nat_le_h[256];

#pragma omp parallel for schedule(dynamic)
  for (int c = 0; c < NCHUNK; c++)
    {
      uint64_t wg = 0xcbf29ce484222325ULL, ng = 0xcbf29ce484222325ULL;
      uint64_t wl = 0xcbf29ce484222325ULL, nl = 0xcbf29ce484222325ULL;
      uint64_t m_gt = 0, m_le = 0;
      uint64_t lo = (uint64_t) c << 24, hi = ((uint64_t) c + 1) << 24;
      for (uint64_t xi = lo; xi < hi; xi++)
	{
	  uint32_t v = (uint32_t) xi;
	  int web_gt = (!(v >> 31)) && (v != 0);
	  int web_le = !web_gt;
	  int nat_gt = smto (v) > smto (0);
	  int nat_le = smto (v) <= smto (0);
	  m_gt += (web_gt != nat_gt);
	  m_le += (web_le != nat_le);
	  /* Position-salted: fold the x value with the predicate so a
	     degenerate constant run cannot alias across directions.  */
	  wg = fnv1a64_step (wg, xi * 2 + web_gt);
	  ng = fnv1a64_step (ng, xi * 2 + nat_gt);
	  wl = fnv1a64_step (wl, xi * 2 + web_le);
	  nl = fnv1a64_step (nl, xi * 2 + nat_le);
	}
      web_gt_h[c] = wg; nat_gt_h[c] = ng;
      web_le_h[c] = wl; nat_le_h[c] = nl;
#pragma omp atomic
      mm_gt += m_gt;
#pragma omp atomic
      mm_le += m_le;
    }

  uint64_t WG = 0xcbf29ce484222325ULL, NG = 0xcbf29ce484222325ULL;
  uint64_t WL = 0xcbf29ce484222325ULL, NL = 0xcbf29ce484222325ULL;
  for (int c = 0; c < NCHUNK; c++)
    {
      WG = fnv1a64_step (WG, web_gt_h[c]);
      NG = fnv1a64_step (NG, nat_gt_h[c]);
      WL = fnv1a64_step (WL, web_le_h[c]);
      NL = fnv1a64_step (NL, nat_le_h[c]);
    }

  printf ("GT (x>0)   mismatches=%llu  web-stream=%016llx  native-stream=%016llx  %s\n",
	  (unsigned long long) mm_gt, (unsigned long long) WG,
	  (unsigned long long) NG, (mm_gt == 0 && WG == NG) ? "EQUAL" : "DIVERGES");
  printf ("LE (x<=0)  mismatches=%llu  web-stream=%016llx  native-stream=%016llx  %s\n",
	  (unsigned long long) mm_le, (unsigned long long) WL,
	  (unsigned long long) NL, (mm_le == 0 && WL == NL) ? "EQUAL" : "DIVERGES");
  int ok = (mm_gt == 0 && mm_le == 0 && WG == NG && WL == NL);
  printf ("RESULT: %s over 2^32 x 2 directions\n", ok ? "EQUAL" : "DIVERGES");
  return !ok;
}
