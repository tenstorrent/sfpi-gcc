# The M2 LREG allocator (`rtl-rvtt-lp-alloc.cc`)

Status: **built** (lane DP, 2026-08-20, branch `agent/lreg-allocator`).
This document describes what the shipped pass does — nothing more.  The
`SFPI_COMPILER_UPGRADE.md` §4 pseudocode ("exact closed-island
transactional coloring", 14-step pipeline, in-place hard-register
substitution) described a DESIGN; the implementation below deliberately
diverges from it where the design's cost was not justified, and this
file is the authoritative record of the divergence.

## What it is

A Chaitin-style build/color/spill loop, run immediately before IRA
(after `pass_rvtt_dst_ownership` and `pass_rvtt_lreg_livein`), under
the default-off flag `-mtt-tensix-optimize-lreg-alloc`:

1. **Trigger.** Function-wide peak simultaneous SFPU pressure (XTT32SI
   pseudos + SFPU hard regs, DF-simulated) is computed first.  Peak
   <= 8 is a proven no-op: nothing is built, nothing changes, the
   compilation is byte-identical with the flag on or off (corpus-gated
   plus the `lreg-alloc-within-file-bh.C` witness).

2. **Interference graph** over XTT32SI pseudo webs (web == pseudo; a
   deliberate, conservative approximation — no du-chain web splitting).
   `pass_rvtt_lreg_livein` has already materialized every raw-LREG
   reservation as a sentinel pseudo interval, so raw reservations
   participate as ordinary graph nodes.  Precolors come from the
   singleton-class constraints of the `rvtt_sfpreadlregN` /
   `rvtt_sfpwritelregN` metadata patterns; conflicting precolors on one
   web fail closed.  Interference is live-at-def over a backward DF
   simulation, with the move-source exemption.  Sentinel reservation
   webs (recognized by their bare-USE interval terminators) are
   never spill candidates.

3. **DSATUR coloring** over the 8 colors of the LREG file
   (deterministic tie-breaks: saturation desc, degree desc, node index
   asc).  Success = colorability certificate; the RTL is handed to IRA
   **unchanged** — assignment is deliberately delegated so IRA keeps
   its coalescing and untouched functions allocate exactly as before.
   (The §4 design's step 11-13 in-place `validate_change` substitution
   was rejected: assigning hard registers pre-IRA would forfeit
   coalescing and could not be byte-identical below the wall.)

4. **Spill** when coloring blocks: the cheapest spillable web in the
   blocked neighborhood (cost = occurrences / (degree+1)) is spilled
   through a Dst scratch-row round trip — `SFPSTORE mod0 4 (INT32)`
   after each def, `SFPLOAD mod0 4` before each reading insn, fresh
   pseudo per insn, no-increment address mode (WH 3 / BH 7, the
   audited `rvtt_no_increment_address_mode ()` capability).  The INT32
   pair is the bit-exact 32-bit round trip on both WH and BH, verified
   over all 2^32 bit patterns against the adjudicated simulator's
   `encode_fp32/decode_fp32` involution.  FP32 (mod0 3) is refused —
   the BH store flushes denormals; SM32 (mod0 12) is refused — the WH
   store maps `0x80000000` to sign-magnitude zero.

5. **Transactionality.** Every emitted insn and operand rewrite is
   logged; any refusal discovered after mutation rolls the stream back
   to the pre-allocation shape (`lreg-alloc-rows-exhausted-rollback-bh.C`
   is the witness: 178 mutations rolled back, the named error reports
   on the pristine stream).

## The refusal surface (all fail-closed, all keep today's
`lreg-pressure-exceeded` error)

| name | trigger |
|---|---|
| `lreg-spill-inexact-dst-mode` | any typed Dst access with a 16-bit format (FP16A/FP16B/INT8/UINT16/INT16/INT8_COMP/LO16_ONLY/HI16_ONLY), the runtime-resolved SRCB mode 0, or a non-constant mode operand |
| `lreg-spill-no-free-dst` | a non-constant Dst address; a load in mod0 10 (INT32_ALL masks the RWC base); scratch rows exhausted; unproven no-increment mode |
| `lreg-spill-laneconfig-unproven` | a function-local SFPCONFIG write to dest 15 (LaneConfig): column-exchange / lane-block bits silently redirect or drop round-trip lanes |
| `cc-enable-unproved` | any CC bracket (PUSHC/POPC/COMPC) or typed CC write that is not the proven all-lanes SFPENCC, anywhere in the function (SFPSTORE/SFPLOAD move only CC-enabled lanes; no all-lanes store variant exists) |
| `dst-rwc-effect-unproved` | any opaque insn (call, asm, unaudited pattern), RWC boundary, or non-LaneConfig layout boundary |
| `lreg-alloc-unknown-use` | a bare USE of the web being spilled (livein sentinels' own USEs are skipped; they are never candidates) |
| `lreg-spill-rewrite-refused` | an insn that does not admit the operand rewrite |
| `lreg-spill-no-candidate` | only reservations / reload temporaries in the blocked neighborhood (e.g. more pinned values than registers at one point — architecturally unallocatable) |

## Scratch-row derivation

A Dst address is base-relative: `(imm + RWC_Dst + MATH_Offset +
REGW_Base) & 0x3FF`, and the physical 32-bit row map
(`dst32b_adjust_row`) aliases two logical rows only when they are
congruent within ±3 modulo 256 (the same physical-row model
`gimple-rvtt-transp-involution.cc` audits).  A scratch immediate is
therefore proven free iff it keeps that distance from every kernel
Dst immediate; rows are taken 4-aligned descending from 252, the
whole-function base is required stable (no RWC/layout boundary
anywhere), and `lreg-alloc-dst32-scratch-derivation-bh.C` witnesses the
derivation moving off a kernel-claimed row.

## Ambient contracts the flag carries

Function-local analysis cannot see state established before the
function.  These are the same trust boundary as the ambient all-lanes
CC contract the shipped CC synthesis bakes in (`gimple-rvtt-cc.cc`):

1. **Dst data width**: with zero typed Dst accesses in the function,
   the flag asserts the surrounding kernel does not view the scratch
   rows through a 16-bit format.
2. **LaneConfig**: the architectural default (no column exchange, no
   block bits — the simulator reset state, what LLK init leaves per
   the audited dest-15 table) is assumed active at the calc body.
3. **Concurrent Dst consumers**: scratch rows are proven free only
   against the function's own typed accesses; the kernel contract is
   that no concurrent consumer (packer, neighbouring thread) touches
   Dst rows the calc body does not itself address while it runs.
   CRAQ on every newly-compiling kernel is the empirical backstop.

## Fire tests (g++.target/riscv/tt/tensix/)

- `lreg-alloc-fire-bh.C` / `-wh.C`: nine loop-carried computed values
  (the `spill-diag-named-error` shape — a hard error by default)
  compile; round trips at row 252, mod0 4, no-inc mode.
- `lreg-alloc-constants-renamed-varied-bh.C`: the shared
  `const-remat-body.h` shape, renamed/varied (generality bar).
- `lreg-alloc-reserved-sentinel-fire-bh.C`: raw producer holds L7
  through a 9-live loop; only unreserved webs spill.
- `lreg-alloc-dst32-scratch-derivation-bh.C`: kernel rows 0/252 push
  the scratch row to 248.
- `lreg-alloc-bf16-refuse-bh.C`, `lreg-alloc-cc-refuse-bh.C`,
  `lreg-alloc-rows-exhausted-rollback-bh.C`: named refusals, error
  preserved (the rollback twin proves transactionality).
- `lreg-alloc-within-file-bh.C`: 8 live values, `colorability=trivial`,
  zero stores.

The historical dump-only audit (`colorability=unchecked` lines under
`-mtt-tensix-optimize-pressure-schedule`) is preserved byte-identically.
