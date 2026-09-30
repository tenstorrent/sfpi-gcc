<!-- Copyright (C) 2026 Tenstorrent Inc.

     This file is part of GCC.

     GCC is free software; you can redistribute it and/or modify it
     under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 3, or (at your
     option) any later version.

     GCC is distributed in the hope that it will be useful, but WITHOUT
     ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
     or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
     License for more details.

     You should have received a copy of the GNU General Public License
     along with GCC; see the file COPYING3.  If not see
     <http://www.gnu.org/licenses/>.  -->

# tt/proofs — exhaustive denotational proofs for SFPU rewrites

Each subdirectory holds the proof obligation for one SFPU rewrite: a
host C harness that models the original instruction sequence (the
"cut") and its replacement, with instruction semantics transcribed
from the reference Tensix simulator; the harness output (RESULT.txt);
and, for some, the compiler output for the matched sequence.  Each
harness enumerates the whole input space -- every 32-bit lane
encoding, or every Dst bit pattern of a format pair -- and compares
the two sides pointwise, so an EQUAL verdict is a proof, not a
test.

A transformation that relies on one of these equivalences cites its
directory here and may fire only while that RESULT is EQUAL.  A
NOT-EQUAL result records why the corresponding rewrite is not
performed, so that it is not proposed again.

To reproduce a result, build and run the harness in its directory on
a 64-bit host and compare the output with the body of RESULT.txt:

    cc -O2 -o harness *.c -lcrypto && ./harness

(-lcrypto is needed only by the harnesses that hash their output
streams with OpenSSL.)

- cast-fp16a-rne/ — the castfp32tofp16a software round-to-nearest-even
  sequence (SFPSHFT/SFPAND/SFPIADD and a predicated move) vs
  SFP_STOCH_RND mod1=0 rnd=0.  NOT-EQUAL (33,810,429/2^32: ties-to-even
  vs ties-away, denormal flush, sign of denormals and zero, NaN
  payload), so the hardware rounding cannot replace the sequence.
- sm32-cast-elision-shift/ — leftshift SM32 software-cast chain vs the
  INT32_2S_COMP conversion-in-load (cast-free) form.  Amount dimension
  EQUAL (2^32); value dimension NOT-EQUAL (92,341,796,868 over 32x2^32,
  two exact closed-form classes, other=0; sound only at k=0 and k=31),
  so the casts cannot be elided.
- shft-imm-vs-reg/ — SFPSHFT dynamic-immediate form vs register form,
  per amount k in [0,31] x 2^32 values.  EQUAL (0 mismatches every
  stratum).  Discharges in advance the obligation of materializing a
  loop-invariant shift amount in a register; no transformation relies
  on it yet.
- cc-narrowing-writers/ — STRUCTURAL certificate (an audit, not a
  value enumeration; the obligation is decided by the simulator's
  enable-masked for_each_lane loop guard, not by operand values): the
  raw typed CC writers admitted into the audited-narrowing set by
  -mtt-tensix-optimize-cc-region-general (gimple-rvtt-invariant.cc
  cc_narrowing_modifier_p: SFPGT/SFPLE SET_CC, SFPEXEXP, SFPLZ,
  SFPIADD families) never touch a disabled lane's enable bit; SFPENCC
  and the empty-stack COMPC are recorded NOT-narrowing.  Also the
  soundness record for the tree's loop-scoped in-frame
  vocabulary-external case
  (rvtt_cc_region_tree::loop_cc_ambient_preserving_p).
