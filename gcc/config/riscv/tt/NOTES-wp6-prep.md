# WP6 preparation — SFPLOADMACRO capability-table transcription audit

Branch `agent/wp6-capability-tables`, worktree
`/localdev/nkapre/sfpi-gcc-wp6-tables`, base `08930f61e` (WP2 head of
`agent/generic-macro-planner`).  NEW FILES ONLY, so the commits
cherry-pick conflict-free onto the WP chain.

This document records the provenance of **every raw constant** in
`rvtt-macro-tables.{h,cc}` and `rvtt-macro-tables-{wh,bh}.def`, so the
WP7 byte-parity gate can audit the transcription mechanically, and it
flags every constant whose **architectural meaning could not be
established** from the frozen pass plus in-tree docs (DESIGN.md §11
"Descriptor-word transcription risk").

Reference sources (all read-only):

| Abbrev | Source |
|---|---|
| `LM` | `/localdev/nkapre/sfpi-gcc-minmax-macro/gcc/config/riscv/tt/rtl-rvtt-loadmacro.cc` at frozen review SHA `4e045d31d` |
| `BH` | same tree, `sfpu-ops-bh.h` |
| `WH` | same tree, `sfpu-ops-wh.h` |
| `QS` | same tree, `sfpu-ops-qsr.h` |
| `FM` | same tree, `SFPLOADMACRO_FORMATION.md` |
| `PR` | same tree, `rvtt-protos.h` |
| `T`  | same tree, `gcc/testsuite/g++.target/riscv/tt/tensix/loadmacro-periodic-minmax-{bh,wh}.C` assertions |
| `WP1` | this tree, commit `30d3c6207` (typed `rvtt_ttdstface`) |

Verification: standalone unit test `rvtt-macro-tables-test.cc`,
**490 checks / 0 failures**, built as

```
g++ -std=c++11 -Wall -Wextra -Werror -I. \
    rvtt-macro-tables-test.cc rvtt-macro-tables.cc -o <out> && <out>
```

(also clean under `-std=c++17 -O2`).  The test's hex literals are
themselves transcriptions; each is cited below, so a WP7 auditor can
check test and table against the frozen pass independently.

## 1. Launch word (WH/BH)

| Constant | Value | Provenance | Meaning | Status |
|---|---|---|---|---|
| opcode | `0x93` | LM:998 (`0x93000000`), 976, 893, 1612; BH:269, WH:256 | SFPLOADMACRO opcode byte | ESTABLISHED |
| `lreg_ind` shift | 20 | LM:997-999 (`lreg_ind = (macro_index << 2) \| (vd & 3)`, `<< 20`); BH:269, WH:256 | macro index (2b) and launch VD (2b) packed into LRegInd | ESTABLISHED |
| `instr_mod0` shift | 16 | LM:1000, 894; BH:269, WH:256 | data-format modifier of the launch's load | ESTABLISHED |
| addr-mode shift, WH | 14 | LM:1001 (`TARGET_XTT_TENSIX_BH ? 13 : 14`); WH:256 | 2-bit address-modifier selector | ESTABLISHED |
| addr-mode shift, BH | **13** | LM:1001, 896, 979; consistent with BH SFPLOAD/SFPSTORE `<< 13` (BH:263, 326) and with 3-bit BH values 6/7 fitting bits 15:13 under InstrMod0 at 16; PROVEN by CRAQ sim decode `15:13`, production tt_llk_blackhole `<< 13`/3-bit, and the shipped oracle launch words `0x9300E000`/`0x9370C000` | 3-bit address-modifier selector | **RESOLVED — see §9(a) resolution** |
| address field | bits 9:0, even | LM:743-751 (range/parity proofs); FM "odd rows alias the macro VD-high encoding bit, and rows above 1023 exceed the ... ten-bit address field" | Dst row address | ESTABLISHED |
| select launch form | `macro << 22`, addr-mode field 0 | LM:1608-1614, 1620-1631 | identical to the general form with VD = 0; macro raw Dst-row mode 0 deliberately not copied from the typed loads (BH overlap with InstrMod0) | ESTABLISHED |

Reference launch values asserted in the unit test (derived from the
LM formulas with the canonical minmax test operands addresses 0/64/128,
mode 0 from `loadmacro-periodic-minmax-body.h`): BH `0x9300e000`,
`0x9310e000`, `0x9370c080`, `0x9310c000` (signbit form LM:975-980); WH
`0x9300c000`, `0x9310c000`, `0x93708080`; select `0x93020040`,
`0x93460040`, `0x93860040` (LM:1608-1614 with mode 2/6, address 0x40).

## 2. Address-modifier modes and slot programs

| Constant | Value | Provenance | Meaning | Status |
|---|---|---|---|---|
| no-increment mode | BH 7 / WH 3 | LM:442, 579, 717, 1489; FM "canonical no-increment address mode (WH 3, BH 7)" | launch addr-mode leaving Dst RWC unchanged | ESTABLISHED |
| auto-increment Dst += 2 | BH 6 / WH 2 | LM:503, 619, 1024-1025; FM "auto-increment-by-two address modifier (WH 2, BH 6)" | launch addr-mode absorbing `TTINCRWC(0,2,0,0)` | ESTABLISHED |
| BH slot map | phys slot 6: SETC16 regs (18,0),(34,2),(53,0) | LM:826-831 with per-line comments | Src increment+clear / Dst increment / fidelity increment+bias of physical slot 6; BH launch field names the slot directly | ESTABLISHED |
| WH slot maps | phys slot 2: (11,0),(25,2),(50,0); phys slot 6: (19,0),(29,2),(54,0) | LM:834-843; FM "Wormhole programs both physical slots 2 and 6 because the unencoded incoming Base selector can map the launch's two-bit index to either physical bank" | dual-slot ownership rule = `needs_bank_base_ownership` data | ESTABLISHED |
| only Dst += 2 programmable | (refusal for other deltas) | LM programs no other delta anywhere | delta→register-value mapping beyond +2 unknown | UNESTABLISHED for other deltas (by design: table refuses) |

Raw SETC16 words asserted by tests: BH `0xB2120000/0xB2220002/0xB2350000`
(T bh, `.ttinsn` 2987524096/2988572674/2989817856); WH
`0xB20B0000/0xB2190002/0xB2320000/0xB2130000/0xB21D0002/0xB2360000` (T wh).

## 3. SETC16 / SFPCONFIG / SFPENCC field layouts

| Constant | Value | Provenance | Status |
|---|---|---|---|
| SETC16 word | `0xb2000000 \| reg << 16 \| value` | LM:816-821 (`emit_owned_setc16`); BH:209, WH:203 | ESTABLISHED |
| SFPCONFIG word | `0x91 << 24 \| imm16 << 8 \| dest << 4 \| mod1` | BH:245, WH SFPCONFIG; the pass materializes config via LREG (`sfpwriteconfig_v`) rather than the imm16 form — encoder provided for reference | ESTABLISHED (layout) |
| SFPENCC all-lanes | imm12 = 3 (`SFPENCC_IMM12_BOTH`), mod1 = 10 (`SFPENCC_MOD1_EI_RI`) → word `0x8a00300a` | PR:177,184; LM:282-296 (`all_lanes_enable_before`); BH:251, WH:238 (opcode 0x8a, field layout) | ESTABLISHED |
| owned config dests | {0,1,4,5,6,8} = mask `0x0173` | LM:164-169 (`owned_loadmacro_config_dest_p`) | ESTABLISHED |

## 4. Descriptor template words (SFPCONFIG dests 0-1)

All template words decode as ordinary Tensix instruction words
(`op << 24 | imm12 << 12 | lreg_c << 8 | lreg_dest << 4 | mod1`, per the
TT_OP tables) whose `lreg_dest` nibble carries a macro **routing
selector** (observed values 0xC, 0xD) instead of a physical LREG.

| Word | Shape / dest | Decode (TT_OP cross-check) | Provenance | Status |
|---|---|---|---|---|
| `0x920002c1` / `0x920002c9` | minmax dest 0 | SFPSWAP (0x92, BH:329): imm12 0, VC = physical L2, dest = selector 0xC, mod1 1/9 | LM:881-882 comment "Template zero is SFPSWAP(VC=L2, VD=macro-VD)"; mod selection LM:781-786 "Macro Mod1=9 places max in VD; Mod1=1 places min there" (data-selected, `stores_out0 ? 1 : 9`) | ESTABLISHED (fields); selector meaning PARTIAL §9(d) |
| `0x940000d6` | minmax dest 1 | SFPSHFT2 (0x94, BH:323): imm12 0, src 0, dest = selector 0xD, mod1 6 | LM:878-880 "template one is the SHFT2 copy into macro LReg16" | PARTIAL — mod1 = 6 meaning §9(e) |
| `0x94fe10c6` | signbit dest 0 | SFPSHFT2: imm12 0xfe1 = −31 (matches the explicit shift amount, LM:469), dest = 0xC, mod1 6 | LM:852 | PARTIAL — §9(d),(e); note explicit BH shift mode is 5 (LM:441), template mod1 is 6 |
| `0x900000d0` | signbit dest 1 | SFPCAST (0x90, BH:239): src 0, dest = 0xD, mod1 0 | LM:853 | PARTIAL §9(d) |
| `0x900000c0` | cast-round dest 0 | SFPCAST: src 0, dest = 0xC, mod1 0 | LM:863-864 "cast VD into the macro transient LReg16 slot" | PARTIAL §9(d) — comment names LReg16 while the selector is 0xC |
| `0x8e0000d1` | cast-round dest 1 | SFP_STOCH_RND (0x8e, BH:338-339): rnd_mode/imm8/src_b all 0, src_c 0, dest = 0xD, mod1 1 | LM:865-866 "FP32-to-BF16 round from the transient slot" | PARTIAL §9(f); 0x8e's field layout differs above bit 12, encoder refuses nonzero imm12 for it |
| `0x7b0000c6` | select dest 0 | SFPSETCC (0x7b, BH:308): imm12 0, dest = 0xC, mod1 6 | LM:1568 "SETCC loaded value == zero" | PARTIAL §9(d) |
| `0x8a0000d0` | select dest 1 | SFPENCC (0x8a, BH:251): imm12 0, dest = 0xD, mod1 0 | LM:1569 "Restore all lanes" | PARTIAL §9(d) |

## 5. Sequence words (SFPCONFIG dest 4+k = macro k)

**Structural fact (ESTABLISHED):** the sequence word for macro K is
written to SFPCONFIG dest 4+K.  Evidence: select writes dests 4/5/6 for
its three launches with macro indices 0/1/2 and an explicitly "idle"
dest 5 (LM:1596-1606, launch indices LM:1612 `macro_index << 22`);
binary writes dests 4/5 for macros 0/1 (LM:884-885 vs 1032, 1048);
unary shapes write dest 4 for their single macro 0 (LM:854, 867).

**Bit-level word format: UNESTABLISHED — §9(b).**  The table therefore
exposes sequences only as whole-word proven programs:

| Word | Program | Events (documented delays only) | Provenance |
|---|---|---|---|
| `0x00dd008c` | minmax macro 0 | SFPSWAP template event, no store; delay unestablished | LM:884 |
| `0x53000000` | minmax macro 1 | template-1 copy + delayed store; "maximum elapsed-instruction delay is three slots" | LM:885, 1056-1058 |
| `0x5384004d` | signbit macro 0 | shift, cast, store; store "retires ... three issue slots after launch" → store delay 3 | LM:854, 985-987 |
| `0x534d0004` | cast-round macro 0 | "Simple d0, Round d1, Store d2" — the one fully delay-documented program | LM:867-868 |
| `0x13000004` | select macro 0 | "SETCC d0, store d2" | LM:1570 |
| `0x00000000` | select macro 1 | "sequence index 1: idle" | LM:1603 |
| `0x00000005` | select macro 2 | "ENCC d0" | LM:1571 |

Delay fields are 3-bit (FM "sequence selection and each three-bit
delay"); `delay_bits = 3`.

## 6. Misc word (SFPCONFIG dest 8)

| Word | Shape | Provenance | Status |
|---|---|---|---|
| `0x00000330` | minmax | LM:886 | word ESTABLISHED, bit semantics PARTIAL §9(c) |
| `0x00000110` | signbit | LM:855 | idem |
| `0x00000100` | cast-round | LM:869-870 "Fixed FP16B store mode and instruction-count delay semantics" | idem |
| `0x700 \| StoreMod0` (proven instance `0x706`) | select | LM:1572 "Fixed store Mod0"; FM "Misc.StoreMod0 carries the fixed payload store mode" | bits 3:0 = StoreMod0 ESTABLISHED for this shape; bits 10:8 PARTIAL §9(c) |

## 7. Fixed architectural words and cost constants

| Constant | Value | Provenance | Status |
|---|---|---|---|
| CR-mode Dst += 8 step | `SETRWC(0, CR=4, D=8, B=0, A=0, mask=4)` = `0x37120004` | WP1 `rvtt_ttdstface` mnemonic `TTSETRWC 0,4,8,0,0,4` ×2; SETRWC layout BH:224, WH:211; equals the frozen magic word LM:161 (deleted at WP1) bit-for-bit — meaning fully recovered | ESTABLISHED |
| face advance | two such steps | WP1 pattern; LM:146-149 comment "two identical architectural Dst += 8 operations" | ESTABLISHED |
| absorbed increment | `TTINCRWC(0,2,0,0)` → INCRWC word `0x38008000` | LM:1401-1418 (`exact_dst_increment_after`); INCRWC layout BH:89, WH:83 | ESTABLISHED |
| drain slots | 3 | LM:985-990, 1056-1062, 952-957 ("maximum elapsed-instruction delay is three slots"); generic rule stays "greatest remaining programmed delay" (FM legality proof 6) | ESTABLISHED for proven calendars |
| n_templates / n_sequence_slots | 4 / 4 | FM "Its init programs four instruction templates, four delay sequences" (MulInt32 handwritten); frozen pass exercises templates 0-1, sequences 0-2 | ESTABLISHED (counts), untested beyond the exercised subset |
| break-even rows | BH 7 / WH 8 | LM:1120-1123 | reference data ONLY for the WP7 §6 cost regression; the caps field is named `reference_breakeven_rows` and must never be read by the planner (Layer 6 derives it) |
| hidden template write | SFPSWAP template (0x920002cX) writes physical L2 | `rvtt.md` `rvtt_sfploadmacro_swap_int` `(clobber (reg:XTT32SI 82))`; 82 = SFPU_REG_FIRST(80)+2 (riscv.h:390) | ESTABLISHED |

## 8. QSR

Intentionally table-absent: `rvtt_macro_caps_for_cpu (CPU_QSR)` returns
null and the refusal name is `target-macro-encoding-unproven`.
QSR's launch layout is on record (QS:228-233: `seq_id << 22 |
lreg_ind_lo << 20 | instr_mod0 << 16 | sfpu_addr_mode << 13 | done << 11
| dest_reg_addr << 1 | lreg_ind_hi`) but FM ("Architecture boundary",
"QSR remains discovery-only because its sequence selector, split VD, and
`done` encoding have not been proven equivalent to WH/BH") forbids
treating it as a capability.  Do not add a QSR .def without the
equivalence proof.

## 9. Constants whose architectural meaning could NOT be established

These need the independent architectural reference DESIGN.md §11 calls
for.  The tables treat them exactly as stated:

* **(a) BH launch addr-mode shift conflict — RESOLVED 2026-08-17:
  `<< 13` with a 3-bit field (bits 15:13) is PROVEN; the vendored
  `TT_OP_BH_SFPLOADMACRO` `<< 14` was stale and has been corrected.**
  Original finding: the frozen pass emits BH launch words with
  `addr_mode << 13` (LM:1001, 896, 979) and is CRAQ-validated 8/8;
  `TT_OP_BH_SFPLOADMACRO` (sfpu-ops-bh.h) said `<< 14`.
  Evidence chain (three independent sources, all agreeing):
  1. **CRAQ simulator authoritative decode** — `data/bh/tensix_isa.json`
     in every sim worktree (`craq-sim`, `craq-sim-minmax-macro`,
     `craq-sim-sfploadmacro-model`, `craq-sim-ttnnwhere-events`):
     `SFPLOADMACRO { dest_reg_addr: "9:0", sfpu_addr_mode: "15:13",
     instr_mod0: "19:16", lreg_ind: "23:20" }`; WH: `sfpu_addr_mode
     "15:14"`.  The field is semantically consumed:
     `TENSIX_EXECUTE_SFPLOADMACRO` (tensix.cpp) re-dispatches the
     embedded SFPLOAD with the mode re-packed at `<< 13` on BH
     (`<< 14` on WH), driving the ADDR_MOD RWC counters — so the 8/8
     CRAQ passes genuinely discriminate the shift.
  2. **Production TT-Metal LLK** —
     `tt_metal/tt-llk/tt_llk_blackhole/common/inc/ckernel_ops.h`:
     `TT_OP_SFPLOADMACRO ... (sfpu_addr_mode) << 13` with
     `is_valid (sfpu_addr_mode, 3)`; the WH header uses `<< 14` with a
     2-bit validity.  The production header was never stale — only the
     GCC-vendored copy was (a WH-layout copy; 3-bit values at `<< 14`
     would overlap InstrMod0 at bit 16, internally inconsistent).
  3. **Shipped ground-truth binaries** — the 8/8-CRAQ-passing
     `minmax-final-craq-v2` BH ON oracle ELFs contain launch words
     `0x9300E000` / `0x9370C000` (RISC-V stream embeddings
     `0x4C038002` / `0x4DC30002`; tensix word = `ror32 (embedded, 2)`),
     i.e. addr modes **7** and **6** in bits 15:13 with InstrMod0 zero.
     Mode 7 is unrepresentable at `<< 14` without setting bit 16.
     `riscv-tt-elf-objdump` independently decodes them as
     `sfploadmacro 0,L0,0,0,7` / `sfploadmacro 1,L3,0,0,6`.
  Field widths settled: BH addr mode **3 bits at 15:13**; WH **2 bits
  at 15:14**; launch `dest_reg_addr` **10 bits (9:0)** on both (the
  quarantined select path's `<= 0xff` bound is a deliberate protocol
  restriction kept for oracle parity, now documented in LM; the main
  paths' `<= 0x3ff` matches the field).  Actions taken (all
  byte-identical no-ops, verified by corpus A/B + 156-entry oracle
  three-way): `TT_OP_BH_SFPLOADMACRO` corrected to `<< 13` (the macro
  has no in-tree consumers); `select_launch_word` now shifts
  `TARGET_XTT_TENSIX_BH ? 13 : 14` (previously a latent `<< 14`, dead
  because the select protocol passes mode 0); its InstrMod0-overlap
  rationale comment rewritten (the overlap claim was an artifact of the
  stale shift — mode zero remains the proven protocol value); capability
  tables unchanged (already the proven values) with provenance upgraded
  from CONFLICT to RESOLVED; ground-truth decode pins added to
  rvtt-macro-tables-test.cc.  TT-Metal requires no change.
* **(b) Sequence-word bit format.**  Only seven whole words are proven
  (§5).  Suggestive but UNVERIFIED observations, recorded so the
  eventual decode can be cross-checked: leading byte `0x53` appears in
  exactly the three programs whose store retires at delay 3 and `0x13`
  in the store-at-d2 select program; low bytes `0x04`/`0x05` appear in
  exactly the template-0/template-1 Simple-unit single-event positions.
  No encoder is derived from these observations.
* **(c) Misc-word bits above 3:0.**  Bits 3:0 = StoreMod0 is proven only
  for the select shape (LM:1572).  UNVERIFIED hypothesis recorded: bits
  8+k are per-macro delay-mode bits ("cycle-vs-instruction delay mode",
  FM required-representation list) — consistent with 1-macro shapes
  0x1xx, the 2-macro shape 0x3xx, and the 3-macro shape 0x7xx.  Bits
  7:4 (`0x10` signbit, `0x30` minmax, `0x00` cast-round) are
  UNESTABLISHED (candidates: store slot / VD index / store-mode
  variants; no evidence).  Whole words only.
* **(d) Routing selectors 0xC/0xD in template LREG-dest fields.**
  Comment-derived: 0xC ≈ the launching instruction's VD, 0xD ≈ the
  macro transient LReg16.  The cast-round template 0 (`0x900000c0`,
  selector 0xC) is described as writing "into the macro transient
  LReg16 slot" (LM:863-864), which contradicts the plain 0xC≈VD
  reading; the exact read/write-role semantics per instruction position
  are therefore only partially established.  The encoder whitelists
  exactly {0xC, 0xD} and refuses 0x8-0xB/0xE/0xF.
* **(e) SFPSHFT2 `mod1 = 6` in templates.**  Present in both SHFT2
  templates (copy `0x940000d6` and shift-by-−31 `0x94fe10c6`); the
  explicit signbit calendar uses BH shift mode 5 / WH 1 (LM:441).
  Presumed an immediate-operand SHFT2 variant whose canonical immediate
  form aliases its source selector to L1 (FM/LM:357-359 — the reason
  the signbit macro VD must be L1); not confirmed.
* **(f) SFP_STOCH_RND `mod1 = 1`** in `0x8e0000d1`: "FP32-to-BF16
  round" per comment only.
* **(g) Per-event programmed delays** not documented for: minmax macro-0
  swap, macro-1 copy/store split, signbit shift/cast.  Stored as
  `DELAY_UNKNOWN` (255) wildcards in the proven-program keys; WP5's
  derived schedules must not be validated against invented delays.
* **(h) Subunit assignments** beyond the documented "Simple d0, Round
  d1, Store d2" line: SFPSWAP is recorded as a Simple-unit event (its
  MAD relationship is a write-PORT borrow, FM legality proof 2, not
  established as MAD occupancy); the signbit SFPCAST is recorded Simple
  by analogy with the cast-round shape's documented Simple cast.  Both
  are inferences; flagged for the architectural reference.

## 10. What WP7 must do with this file

For the byte-parity gate: every word in §§1-7 marked ESTABLISHED may be
asserted directly; every §9 item must remain either (i) covered by the
whole-word parity assertions against the frozen pass, or (ii) resolved
by the independent architectural reference before the covering encoder
is generalized beyond the proven programs.  The `reference_breakeven_rows`
field is a regression expectation for the Layer-6 model, never an input.
