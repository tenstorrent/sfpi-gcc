<!-- Copyright (C) 2026 Tenstorrent Inc.

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
     <http://www.gnu.org/licenses/>.  -->

# MOP effect derivation for the prgm-const freedom proof — Lane BC design (2026-08-18)

> **STATUS: IMPLEMENTED (Lane BL, 2026-08-18, branch agent/mopcfg-derivation).**
> The derivation below is landed in `rvtt-mop-derive.{h,cc}` (new file) +
> `gimple-rvtt-prgm-const.cc` (scan integration) + `rvtt-mop-tables.h`
> (new provenanced facts: MMIO-store census, instruction apertures,
> PC_BUF words, debug block, link-image disjointness, reset-template).
> Steps 1–3 are as designed; the gap note's volatile-store census is
> closed (including the blocking-store asm idiom, which used to be
> admitted blind); the crt0 init-array blocker is discharged
> STRUCTURALLY (`rvtt_mop_init_array_call_p`: pointer derivation
> anchored on `__init_array_start` + the TU-registered-constructors
> argument under AXIOM kernel-single-TU, refusing on section-attribute
> extensions and toplevel asm).  Additional derivations the real exp TU
> required: foldable TU global-pointer values (assume + store-census
> verify; the pc_buf_base/regfile/profiler-pointer aperture convention),
> the pointer-parameter caller-closure join (copy_runtimes_from_L1),
> and the bounded-IV store-range proof (the GPR-file fill).  M3 fires
> on the real exp perf node with the STOCK harness (no source changes,
> no markers): capture 17→16 at pin-10's restructured exp source (the
> 16→15 figure below was the pre-restructure shape — same −1-slot,
> −1-stall fire class), SFPMUL+SFPADDI fused to SFPMAD reading L14.
> The sdpa identical-immediate dedup landed alongside (1 register + 1
> programming write + dominated reuses instead of L12+L13+L14).

## Problem

The M3 pass (`gimple-rvtt-prgm-const.cc`) allocates PRGM constant
registers only under a TU-wide freedom proof: nothing in the TU may
write a PRGM register or LaneConfig unaudited.  Raw `.ttinsn` words
decode through the audited table; the one word class the table cannot
decode by itself is **MOP** (frontend opcode 0x01): it expands the
instruction words previously programmed into the nine MOP template
registers, so its effects live in *other* stores, not in the word.
Every production math kernel reaches a MOP through the shared datacopy
LLK (`ckernel_template::run()`), so an unresolved MOP refuses the whole
TU and M3 never fires on real kernels.

The interim answer — `__builtin_rvtt_ttregion_begin/end` markers
carrying a TRUSTED `config_write_mask` over the datacopy MOP run — is
**retired by ruling (2026-08-18)**: the pitch is "the compiler compiles
ordinary code", so the compiler must PROVE region effects, never be
told them by a source annotation on the consumed library.  This note
is the designed replacement: derive the MOP's expanded words from the
TU's own `mop_cfg` programming writes and decode them through the same
audited table.

## What already exists (all provenanced facts, none new)

* `rvtt-mop-tables.h` (Lane AO/AX): MOP=0x01 / MOP_CFG=0x03 opcodes;
  MOP word layout (`mop_type` bit 23, loop_count 22:16, zmask 15:0);
  the nine MMIO template words at `XTT_MOP_CFG_MMIO_BASE` 0xFFB80000
  (+4*i, i=0..8, write-only from the RISC); slot consumption per type
  — type 0: word 1 = flags (<=3, not expanded), words 2..8 =
  per-iteration/zmask-arm instruction slots; type 1 (the production
  datacopy dispatch `TTI_MOP(1,0,0)`): words 0..1 = loop lengths
  (never expanded as instructions), words 2..8 = start/end/loop/last
  instruction slots; MOP_CFG zmask high half is a type-0-only fact;
  `mop_sync()` = blocking store to 0xFFE80008; the push-classification
  axiom (a runtime-composed push classifies by the constant opcode
  base of its PLUS/IOR composition under the TT_OP field discipline).
* `rtl-rvtt-mop-form.cc` (Lane AO + AX's outward-ownership refusal):
  the must-dataflow discipline over `mop_cfg[]` — caller-closure walk,
  re-arm proof, unclassifiable-push-consumes-everything conservatism.
  That machinery runs at RTL for the FORMING direction (the pass
  writes templates); the derivation below is the same facts run
  FORWARD at gimple for the READING direction (prove what a
  pre-existing template can expand).
* `audited_raw_word_p` in `gimple-rvtt-prgm-const.cc`: the decode
  target.  A derived template word is proven exactly when this table
  admits it (NOP / sync family / thread-config family /
  CLEARDVALID/SETRWC / allocatable-dest SFPLOADI / SFPCONFIG with
  decoded constant dest, claiming it / the audited LaneConfig
  default-reset word).

## Design: flow-insensitive TU derivation (matches the proof's shape)

The freedom proof is already TU-wide and flow-insensitive ("nothing in
the TU may touch PRGM unaudited"), so the MOP arm does not need to
know WHICH template a given run expands — only that NO template word
programmable in this TU can touch owned state unaudited:

1. **Collect the TU's mop_cfg writes.**  Extend `scan_function_body`
   to classify gimple volatile stores (see the gap note below).  A
   store whose address is a compile-time constant in
   `[0xFFB80000, 0xFFB80020]` is a template-slot write with slot index
   `(addr - base)/4`.  `ckernel_template::program` and
   `ckernel_unpack_template::program` write through exactly this
   constant-folded form in the production LLKs.
2. **Slot taxonomy.**  Union over both mop types (we cannot know
   flow-insensitively which type will launch):
   - slots 0..1: loop lengths (type 1) / flags (type 0) — never
     expanded as instruction words; any value admissible.
   - slots 2..8: instruction slots under at least one type — the
     stored value must be a compile-time constant admitted by
     `audited_raw_word_p` (SFPCONFIG claims accumulate into
     `tu_facts.claimed` like any direct word).  A REPLAY word in a
     slot additionally requires the replay-recording words to be
     audited — first increment refuses REPLAY-in-slot
     (`mop-template-replay-unproven`).
   - a non-constant value in slots 2..8 refuses
     (`mop-template-word-unproven`) UNLESS its constant opcode base
     classifies it under the push axiom AND that base's whole class is
     effect-inert for PRGM/LaneConfig (thread-config 0xB0-0xB8 and
     SETRWC 0x37 compositions qualify: runtime operands stay inside
     their bit fields, and no field reaches the opcode byte or an
     SFPCONFIG dest).  This is the same axiom `rvtt-mop-tables.h`
     already records for push classification — datacopy's runtime
     `zmask`/count composition lands in slots 0..1 anyway.
3. **Admit the MOP word.**  With every TU mop_cfg instruction slot
   audited, `audited_raw_word_p` gains a 0x01 arm: MOP is proven
   (loop_count/zmask fields are expansion-count facts, not effect
   facts).  MOP_CFG (0x03) is proven unconditionally (writes only the
   zmask high half — a type-0 count fact).
4. **Refusal taxonomy** (all TU-wide, byte-identical):
   `mop-template-word-unproven` (non-constant, unclassifiable slot
   write), `mop-template-replay-unproven` (REPLAY in a slot),
   `mop-template-store-unresolvable` (volatile store whose address
   cannot be proven inside/outside the template file — see gap),
   plus the existing `unaudited raw opcode` for everything else.

No flow sensitivity, no liveness, no mop_sync reasoning is needed for
the freedom proof (a race reprograms WHICH audited words run, never
whether they are audited).  Flow-sensitive per-launch derivation stays
the mop-form pass's territory.

## Known gap this design must close (pre-existing, now load-bearing)

The gimple scan classifies `.ttinsn` asm and calls but is BLIND to
volatile-store instruction pushes (`*__instrn_buffer = word`,
`mop_cfg[i] = word`, pcbuf writes): today a volatile store of a raw
SFPCONFIG word would evade the TU proof entirely.  The derivation
turns volatile stores into first-class scan objects, so the same
increment closes the hole: constant-address stores classify by target
(mop_cfg range → slot rules; instrn-buffer/pc-buf anchors → push
axiom on the stored word; other constant MMIO → inert for this
proof); non-constant-address volatile stores must prove they cannot
alias an instruction FIFO (the `__instrn_buffer` ABI anchor and
`canonical_buffer_arg_p` give the address vocabulary) or refuse.

## Consequences until the derivation lands

* Markers retired everywhere: the trusted channel is deleted from
  `gimple-rvtt-prgm-const.cc` (this branch), the datacopy/boot marker
  macros are deleted from tt-metal, and `-DLLK_ENABLE_TTREGION_MARKERS`
  + `-mtt-tensix-optimize-prgm-const` leave the sweep ON set.
* M3 refuses on every TU containing a raw MOP (all production math
  kernels) — the exp capture-15 fire is honestly UN-SHIPPED; the
  refusal is byte-identical, so the sweep sees plain flag-off codegen.
  (Verified on the real exp perf node with the retired-channel
  compiler: flag-on vs flag-off .text byte-identical across all 5
  variants x 3 elfs.  In practice the FIRST blocker on the harness TU
  is the crt0 init-array walk's indirect constructor call in _start —
  formerly marker-covered in boot.h; a future increment can prove
  init-array targets are this TU's own scanned constructors — with the
  datacopy MOP the next blocker behind it.)
* M3 still fires (and is regression-tested) on MOP-free TUs:
  `prgm-const-bh.C`, `prgm-const-inlined-away-callee-bh.C`.
* Expected re-entry: implement steps 1-3 above (estimated as one
  focused lane: one new classifier in `scan_function_body`, one
  audited-table arm, tests per refusal + renamed/varied twins), then
  M3 + the exp prediction (capture 16->15, ~83.5-88 booked units)
  return to the ON set with a dump-proven engagement gate.
