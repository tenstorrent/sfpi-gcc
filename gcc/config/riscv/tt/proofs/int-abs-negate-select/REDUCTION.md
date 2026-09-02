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

# int-abs spelling reductions (laneDN, 2026-08-20)

The RESULT here proves ONE value function over all 2^32 raw lane
encodings w:

    r(w) = (w & 0x80000000) ? 0 - w : w        (wrapping)

against SFPABS mod1=0.  The rvtt_int_abs fold admits a SPELLING only
when its region reduces to exactly this value function; the reductions
below are the complete admitted set (gimple-rvtt-int-abs.cc), each a
pointwise-identity argument on top of the swept RESULT — no new sweep
obligation arises because the denotation is unchanged.

Effective enabled set E of the region = the compare's CC lowering
(craq-sim src/tensix.cpp:8969-9000 SETCC; rvtt.cc:602-696 int lowering
vs immediate 0), complemented within the enclosing frame when the
region carries a single SFPCOMPC directly after sfpxcondb (an empty
then-arm; craq-sim src/tensix.cpp:9458-9477).  The region computes
select(E, 0 - w, w).

| spelling | cc | compc | E | reduction |
|---|---|---|---|---|
| `v_if (v < 0) { r = 0 - v; }` | LT | no | {bit31 set} | the proven set itself |
| `v_if (v <= 0) { r = 0 - v; }` | LE | no | {bit31 set} ∪ {w == 0} | two's-complement zero is unique (raw w == 0); there 0 - 0 = 0 = w, so select(E,...) = select({bit31},...) pointwise |
| `v_if (v >= 0) { } v_else { r = 0 - v; }` | GE | yes | ¬{bit31 clear} = {bit31 set} | complement collapses to the proven set |
| `v_if (v > 0) { } v_else { r = 0 - v; }` | GT | yes | ¬{w > 0} = {bit31 set} ∪ {w == 0} | complement collapses to the LE row |

Refused by construction (value function differs from r(w) at 2^31-1 or
more points — these are negate-on-complement, not absolute value):
direct GE (`v_if (v >= 0) { r = 0 - v; }`), direct GT, and the
else-forms of LT/LE.  Named refusal `int-abs-region-shape`.  EQ/NE are
not order tests (`int-abs-compare-kind-unsupported`).  A COMPC that is
not directly after sfpxcondb, or follows a lane-predicated
materialization (non-empty then-arm), is not this shape.

The nesting argument is unchanged from the pass header: under an
enclosing CC frame both COMPC and the deleted region operate within
the frame's lanes, and SFPABS writes exactly the frame-enabled lanes.

Contract: these reductions stand ONLY while RESULT.txt is EQUAL; if
the RESULT is ever retired, every admitted spelling retires with it.
