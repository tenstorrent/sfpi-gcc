;; Tensix issue-cost model.
;; Copyright (C) 2026 Tenstorrent Inc.
;;
;; This file is part of GCC.
;;
;; The five classes deliberately mirror craq-sim's
;; tensix_rtl_issue_class_for_inst: Math, Sfpu, Tdma, Cfg and Sync.  The
;; default reservations model the simulator's top-level issue classes.  CFG
;; execution-resource occupancy will be added with the first compiler pattern
;; for the B0-B8 opcode family; no existing RVTT pattern owns that resource.
;; F1.3 will supply tuned per-CPU values; keep the class vocabulary independent of the legacy
;; `type' attribute because several late RVTT passes use TYPE_TENSIX as an
;; ISA-membership test.

(define_enum "xtt_issue" [
  none
  math
  sfpu
  tdma
  cfg
  sync
])

;; RVTT instructions are SFPU by default.  The legacy TYPE_TENSIX membership
;; test remains untouched, but ttrocc patterns are not Tensix FIFO words and
;; must not enter this model.  Generated zero-length ghosts likewise have no
;; issuer.  Individual real Tensix patterns override this for Math, Tdma,
;; Cfg, and Sync as their opcode ownership becomes known.
(define_enum_attr "xtt_issue" "xtt_issue"
  (cond [(eq_attr "length" "0") (const_string "none")
         (eq_attr "type" "tensix") (const_string "sfpu")]
        (const_string "none")))

;; Replay formation is intentionally stricter than Tensix membership.  A
;; `safe' instruction may be copied into an automatically generated replay
;; recording.  A barrier remains at its source position and terminates any
;; candidate payload.  An owner reserves or plays an explicitly managed replay
;; range.  Opaque asm has no `safe' classification and therefore remains a hard
;; boundary.
(define_enum "xtt_replay" [
  none
  safe
  barrier
  owner
])

(define_enum_attr "xtt_replay" "xtt_replay"
  (const_string "barrier"))

;; ---------------------------------------------------------------------
;; Generated architectural-effect attribute family (macro-planner Layer 1).
;;
;; Every default is the REFUSING value, exactly like xtt_latency_reorder
;; defaulting to barrier: a pattern carries no effect claim until it is
;; audited and annotated.  Consumers reach these only through the
;; rvtt-effects query API (rvtt-effects.h); an unaudited field makes the
;; whole effect set opaque and the consumer must refuse byte-identically.
;; ---------------------------------------------------------------------

;; Execution subunit occupied by the instruction's event.  `none' is both
;; the refusing default and the audited value for pure issue-slot fillers
;; (SFPNOP); the distinction is carried by the other audited fields.
(define_enum "xtt_subunit" [
  none
  simple
  mad
  round
  load
  store
  cfg
  sync
])
(define_enum_attr "xtt_subunit" "xtt_subunit"
  (const_string "none"))

;; Issue slots until the result is architecturally readable, stored with a
;; +1 bias because generated attributes cannot be negative: 0 means
;; unaudited (refusing), N+1 means latency N.  The mad family's stored 2
;; (latency 1) restates the established one-cycle dynamic result-delay
;; contract (xtt_delay `dynamic'); real per-CPU values are future work
;; (F1.3).  Decoded only by rvtt_insn_effects.
;;
;; D3 latency audit (2026-08-17, WH/BH; QSR carries no entries and the
;; interlock scheduler refuses the target).  Every entry is a table fact
;; with provenance; anything not listed keeps the refusing default 0.
;; Sources:
;;   [ISA]   the per-instruction architectural references (SFP*.md):
;;           the required-NOP contracts ("software must ensure that on
;;           the next cycle ... does not read what this wrote") name
;;           exactly the mad family; the audited latency-0 pages carry
;;           no next-cycle constraint.
;;   [SIM]   craq-sim TENSIX_EXECUTE_* executors (state read/written,
;;           lane predication, CC effects; TT_VERSION <= 1 = WH/BH).
;;   [HAND]  the silicon-proven hand exp kernel
;;           (ckernel_sfpu_exp.h _sfpu_exp_21f_bf16_tti_): its
;;           back-to-back dependent adjacencies are latency-0 witness
;;           points, and its one deliberate filler placement ("slots
;;           into the SFPMAD's 2-cycle latency window") restates the
;;           mad fact.
;;   [CAL]   the frozen, silicon-passing macro calendars (R1 derivation
;;           facts, rvtt-macro-tables.cc subunit_result_latency):
;;           Simple/Round chains step one slot per dependence, the MAD
;;           store distance is two slots.
;;
;;   class (patterns)                 latency  provenance
;;   mad family (mul/add/mad/muli/
;;     addi/mul24)                       1     [ISA] next-cycle rule;
;;                                             [CAL] MulInt32 store
;;                                             delay 2; pre-existing
;;                                             entry, unchanged
;;   load (sfpload_lv_int)               0     [HAND] SFPLOAD->SFPMAD
;;                                             back-to-back; [ISA]
;;                                             SFPLOAD.md's 3-insn rule
;;                                             is the cross-unit Dst
;;                                             race, not a result delay
;;   loadi (sfploadi_lv_int,
;;     mod0 0-8,10)                       0     [HAND] SFPLOADI->SFPSWAP
;;                                             back-to-back; [SIM] lane
;;                                             write only; [ISA] no
;;                                             next-cycle rule
;;   simple unit (mov/exexp/exman/
;;     abs/lz, setexp/setman/setsgn
;;     register forms, shft variable
;;     mod 0/2 and immediate forms,
;;     and/or/xor, iadd_v audited
;;     mods, cast audited mods, the
;;     bare and lane-predicated
;;     copies)                           0     [HAND] exexp->exman->
;;                                             shft->exman->cast->mad
;;                                             and and->setexp->rnd
;;                                             back-to-back; [CAL]
;;                                             signbit shift->cast->
;;                                             store steps one slot;
;;                                             [SIM] per-mod effect
;;                                             audits in rvtt.md
;;   round unit (stochrnd i/v)           0     [CAL] cast-round rnd->
;;                                             store one slot; [HAND]
;;                                             SFPSTOCHRND->SFPSTORE
;;                                             back-to-back
;;   SFPNOP                              0     pure issue-slot filler
;;                                             (pre-existing entry)
;;
;;   swap (sfpswap int/cst/indexed)     0     [ISA] SFPSWAP.md: the
;;                                             next-cycle rule is an
;;                                             ACCEPTANCE stall (only
;;                                             SFPNOP accepted; hardware
;;                                             stalls otherwise), with
;;                                             no further result-read
;;                                             constraint; [SIM]
;;                                             TENSIX_EXECUTE_SFPSWAP
;;                                             atomic register update;
;;                                             [HAND] the silicon-proven
;;                                             reduce_custom bitonic
;;                                             calendar issues CHAINED
;;                                             dependent SFPSWAPs
;;                                             back-to-back (each reads
;;                                             the previous swap's
;;                                             result).  The acceptance
;;                                             stall itself is the
;;                                             separate structural fact
;;                                             xtt_next_slot_stall
;;                                             below; a latency entry
;;                                             alone does NOT make swap
;;                                             a fill target (the
;;                                             interlock scheduler
;;                                             refuses any
;;                                             next-slot-stall insn).
;;   compare set-cc (sfpgt/sfple,
;;     BH mod1 == 1 arm,
;;     rvtt_sfpgt_cc/rvtt_sfple_cc)      -     [ISA] SFPGT.md/SFPLE.md:
;;                                             SET_CC writes only
;;                                             LaneFlags under
;;                                             LaneEnabled -- no LREG
;;                                             result, so no result
;;                                             latency entry; ordering
;;                                             is carried by the
;;                                             "readwrite" CC effect
;;                                             (the SFPSETCC
;;                                             convention); [SIM]
;;                                             TENSIX_EXECUTE_SFPGT/LE
;;                                             mod1==1 arm lane-writes
;;                                             only the cc mask; [HAND]
;;                                             rounding_ops floor/ceil
;;                                             fixups + softmax_k issue
;;                                             SFPGT mod1==1 with the
;;                                             dependent predicated
;;                                             consumer in the next
;;                                             Simple slot.
;;
;;   compare set-dest (sfpgt/sfple,
;;     BH mod1 == 8 arm only)            0     [ISA] SFPGT.md/SFPLE.md
;;                                             carry no next-cycle
;;                                             result-read rule (the
;;                                             audited latency-0 page
;;                                             convention above); [SIM]
;;                                             TENSIX_EXECUTE_SFPGT/LE
;;                                             lane-write the tied
;;                                             destination immediately;
;;                                             [HAND] the hand exp
;;                                             kernel's SFPGT feeds its
;;                                             SFPAND through the same
;;                                             one-slot Simple stepping
;;                                             the mod-8 effect audit
;;                                             on the pattern already
;;                                             cites.  Every other mod
;;                                             keeps the refusing
;;                                             default.
;;
;;   iadd immediate (sfpiadd_i_lv_int,
;;     constant-imm alternatives,
;;     mods 1/5/9)                       0     [ISA] SFPIADD.md: simple
;;                                             sub-unit, no next-cycle
;;                                             rule; the ARG_IMM arm
;;                                             reads only LReg[VC];
;;                                             [SIM] craq tensix.cpp
;;                                             TENSIX_EXECUTE_SFPIADD
;;                                             :8894 (envelope
;;                                             mod1 <= 10, (mod1&3)<=2;
;;                                             lane-predicated write,
;;                                             CC written unless
;;                                             (mod1&12)==4); [CAL]
;;                                             Simple chains step one
;;                                             slot.  D3-follow-up
;;                                             audit (2026-08-19, lane
;;                                             CF): this row unblocks
;;                                             the reissue pricing of
;;                                             the fresh-body ceil and
;;                                             rewritten-lcm loops,
;;                                             whose first unaudited
;;                                             payload producer was
;;                                             this pattern (NOT
;;                                             SFPSWAP, which the swap
;;                                             row above already
;;                                             covers).  The
;;                                             register-immediate
;;                                             (runtime-synth)
;;                                             alternatives keep the
;;                                             refusing default: their
;;                                             instruction-buffer push
;;                                             is outside this audit.
;;
;;   divp2 (sfpdivp2_lv_int,
;;     constant-imm alternatives,
;;     mods 0/1)                        0     [ISA] SFPDIVP2.md: simple
;;                                             sub-unit, no next-cycle
;;                                             rule; reads only
;;                                             LReg[VC], writes VD
;;                                             lane-predicated, no
;;                                             lane-flag effect; [SIM]
;;                                             craq tensix.cpp
;;                                             TENSIX_EXECUTE_SFPDIVP2
;;                                             (envelope mod1 <= 1);
;;                                             [CAL] Simple chains step
;;                                             one slot.  D3-follow-up
;;                                             audit (2026-08-19, lane
;;                                             CF): unblocks the
;;                                             reissue pricing of the
;;                                             fresh-body rsqrt loop
;;                                             (25 words after
;;                                             residency), whose first
;;                                             unaudited producer was
;;                                             this pattern.
;;
;;   store (rvtt_sfpstore_int)           0     [ISA] SFPSTORE.md (BH+WH):
;;                                             Dst write only, no LREG
;;                                             result, no next-cycle rule
;;                                             (the audited latency-0
;;                                             page convention); the BH
;;                                             SFPMAD.md undetected-
;;                                             consumer bug list does not
;;                                             name SFPSTORE, so the
;;                                             mad->store read IS
;;                                             scoreboard-covered; [SIM]
;;                                             craq tensix.cpp
;;                                             TENSIX_EXECUTE_SFPSTORE
;;                                             commits Dst at issue
;;                                             (reorder-equivalence run
;;                                             archived, laneDL-evidence-
;;                                             20260820); [HAND] the
;;                                             silicon-proven hand exp
;;                                             kernel stores back-to-back
;;                                             with dependent neighbours.
;;                                             D3-follow-up audit
;;                                             (2026-08-20, lane DL):
;;                                             admits the store class
;;                                             into capture-rotation's
;;                                             plain-reorder filler pool
;;                                             (the pool's Dst-crossing
;;                                             proof is separate,
;;                                             rtl-rvtt-schedule.cc); the
;;                                             replay reissue recurrence
;;                                             is unchanged (stores write
;;                                             no LREG and never mark a
;;                                             register unproved).
;;
;;   row step (rvtt_ttincrwc)            0     [ISA] WH INCRWC.md: pure
;;                                             RWC counter update, no
;;                                             LREG result, no next-cycle
;;                                             rule; the BH tree carries
;;                                             NO INCRWC page (doc gap,
;;                                             recorded); [SIM] craq
;;                                             tensix.cpp
;;                                             TENSIX_EXECUTE_INCRWC
;;                                             applies counter deltas at
;;                                             issue (reorder-equivalence
;;                                             run archived, laneDL-
;;                                             evidence-20260820); [HAND]
;;                                             every silicon-proven
;;                                             counted production row
;;                                             issues TTINCRWC->SFPLOAD
;;                                             back-to-back at the row
;;                                             boundary.  D3-follow-up
;;                                             audit (2026-08-20, lane
;;                                             DL): admits the typed
;;                                             row-step word into
;;                                             capture-rotation's
;;                                             plain-reorder filler pool
;;                                             under its RWC-crossing
;;                                             proof and the replay-
;;                                             formation deferral gate
;;                                             (a mid-row TTINCRWC makes
;;                                             a loop ineligible to
;;                                             counted_loop_payload, so
;;                                             the mover defers by name
;;                                             while replay-hoist is
;;                                             enabled).
;;
;;   lut (rvtt_sfplut, mod0 0/4)         1     [ISA] SFPLUT.md (BH+WH,
;;                                             identical functional
;;                                             models): reads LReg[0..2]
;;                                             + LReg[3] (tied dest),
;;                                             lane-predicated write, no
;;                                             CC write, no config/RWC/
;;                                             Dst access; MAD sub-unit;
;;                                             scheduling "as per
;;                                             SFPMAD" -> the mad
;;                                             family's one-slot result
;;                                             delay; [SIM] craq
;;                                             tensix.cpp
;;                                             TENSIX_EXECUTE_SFPLUT
;;                                             matches the model
;;                                             (extract archived,
;;                                             laneDL-evidence-
;;                                             20260820); [HAND] the
;;                                             production sigmoid_appx
;;                                             LUT kernel issues SFPLUT
;;                                             in the mad-family
;;                                             calendar shape.
;;                                             D3-follow-up audit
;;                                             (2026-08-20, lane DL):
;;                                             un-opaques the LUT rows
;;                                             for the replay reissue
;;                                             recurrence (LUT loops
;;                                             previously refused
;;                                             effect-opaque) and the
;;                                             interlock window.
;;                                             INDIRECT_VD (mod0 & 8)
;;                                             keeps every refusing
;;                                             default: the write
;;                                             target is dynamic
;;                                             (LReg[7]-indexed) and
;;                                             position masks cannot
;;                                             express it.
;;
;;   planner-emitted SFPLOADMACRO
;;     (rvtt_sfploadmacro_int /          issue-plane record (lane CK):
;;      rvtt_sfploadmacro_hidden_int,    the launch pattern itself stays
;;      planner emission records only)   attribute-UNAUDITED (its effects
;;                                             are descriptor-dependent and
;;                                             inexpressible here), but the
;;                                             macro planner records the
;;                                             effect interface of each
;;                                             launch IT emits, derived
;;                                             from the descriptor it just
;;                                             synthesized (rvtt-effects.h
;;                                             contract).  [ISA]
;;                                             SFPLOADMACRO.md: every
;;                                             sub-unit event executes at
;;                                             issue + 1 + delay; no
;;                                             issue-cycle register rule,
;;                                             so launch issue is never
;;                                             operand-gated and reads
;;                                             nothing at the issue plane;
;;                                             [SIM] craq-sim f80a8d64
;;                                             sfploadmacro_events.h:
;;                                             events enqueue
;;                                             unconditionally at issue,
;;                                             "there is no FIFO between
;;                                             launches"; [HAND] the
;;                                             production Where kernel and
;;                                             the handwritten typecast
;;                                             issue launches
;;                                             back-to-back.  The record's
;;                                             result latency is the
;;                                             launch's SETTLE distance
;;                                             (greatest SequenceBits
;;                                             delay + the sub-unit's
;;                                             audited writeback latency
;;                                             above -- the drain
;;                                             derivation's own math), so
;;                                             a foreign consumer of a
;;                                             launch-written LREG waits
;;                                             for the macro to settle.
;;                                             Full-lane writes by the
;;                                             record contract (records
;;                                             refuse CC-writing
;;                                             calendars), so the reissue
;;                                             pricing drops the
;;                                             write-side dependence edge
;;                                             for record-carried
;;                                             launches.  USER-written
;;                                             launches (raw `.ttinsn'
;;                                             words) never acquire
;;                                             records and keep every
;;                                             refusing default.
;;
;; Deliberately UNAUDITED (refusing): SFPSHFT2 -- mod-dependent
;; next-cycle register constraints (SFPSHFT2.md) outside the
;; single-latency vocabulary; LUTFP32 -- mad-unit, but its
;; Mod1/Mod1Mirror scheduling split (SFPLUTFP32.md: the stalling logic
;; keys on Mod1Mirror, not Mod1) and its per-mode register envelopes
;; (3-entry/6-entry/FP32 tables read different LReg sets) need their
;; own per-mod audit before any entry lands (SFPLUT's audit above does
;; NOT transfer); SFPLUT INDIRECT_VD (dynamic write target); the
;; auto-incrementing load/store address modes (positional Dst/RWC
;; state, WP6 capability-table territory); everything QSR (simulator
;; returns MissingSpecification for these opcode semantics).
;;
;; Deliberately UNAUDITED (refusing): the X6 FPU face-transpose family
;; (lane FV 2026-08-22): TTMOVD2B / TTMOVB2A / TTMOVB2D / TTMOVA2D /
;; TTTRNSPSRCB / TTSTALLWAIT / TTRMWCIB0..3.  Matrix-Unit (FPU)
;; instructions moving Dst rows through the SrcA/SrcB banks under
;; ALU-format state, plus the backend-config byte RMW and the wait-gate
;; stall that choreograph them.  Their Dst addressing is positional
;; (RWC + DEST_REGW_BASE + addr-mod), their data path is format-state-
;; dependent (MOVD2B.md/MOVA2D.md conversion arms), their wait-gate
;; behavior is bank-ownership-dependent, and TTSTALLWAIT's stall extent
;; is condition-dependent -- every one of those is outside today's
;; effect vocabulary, so the whole family carries NO effect attributes:
;; rvtt_insn_effects () resolves it opaque and every optimization layer
;; refuses around it byte-identically (scheduler barrier, dst-autoincr
;; AIC_FOREIGN, replay barrier via xtt_replay=barrier, reassoc
;; reassoc-fpu-choreography-boundary, crosscall
;; drain-init-ownership-unproven).  Operand envelopes are the
;; architectural encoding-field widths only (rvtt-insn.def {CU, width}
;; = ckernel_ops.h TT_*_VALID); semantic legality (format protocol,
;; bank grants, TEN-4245's TF32+UseDst32bLo UB edge) is owned by the
;; sfpi_crosslane.h X6 surface contract and its sim gate, not by
;; per-operand compiler checks.
;; Mod-write backedge-crossing price (lane EB, DX finding F2 / CK-P3).
;; The Dst auto-increment pass (rtl-rvtt-dst-autoincr.cc) turns the
;; audited-latency-0 TTINCRWC row step above into a positional-state
;; side effect of the terminator access -- an auto-incrementing access
;; mode, i.e. exactly the class this table DELIBERATELY REFUSES a
;; latency row for.  The audit split that follows from the table:
;;
;;   - consumers inside a continuous Tensix word stream are covered by
;;     [HAND] witnesses (production unrolled and replay-windowed kernels
;;     issue live-modifier stores back to back with dependent accesses;
;;     the pass's own silicon winners -- cast, minmax, the unrolled
;;     unaryshift hand kernel at 12.955 -- have this shape);
;;
;;   - a consumer reached ACROSS A LOOP BACKEDGE has no witness: the
;;     scalar loop control drains the frontend and the next iteration's
;;     first Dst access issues onto an empty pipe a few slots after the
;;     mod-write, inside the unaudited retirement window.  Two pin-14
;;     whole-ELF silicon witnesses measure that crossing regressive on
;;     one-row rolled loops (absint32 hand 16.950 -> 18.853, +11.2%;
;;     unaryshift-fresh semantic 16.962 -> 19.631, +15.7%; the entire
;;     math.elf delta is the transform: three preheader SETC16, the
;;     store mode 7 -> 6, the TTINCRWC deleted), about two to three
;;     issue slots per iteration -- while the eight-row-per-iteration
;;     rolled hand kernel with the same live crossing measures a
;;     -23.6% WIN (laneDX-evidence-20260820/EVIDENCE.md).
;;
;; AUDITED CONSTANT: W_drain = 7 -- the drained-frontend retirement
;; window of a backedge-crossing mod-write, in frontend issue-slot
;; words (lane EP finding F1,
;; laneEP-evidence-20260821/EP-FINDINGS.md).  Consecutive
;; backedge-crossing mod-writes serialize at the window: the covering
;; distance per crossing is the WHOLE iteration's issue-slot word
;; count, scalar words included (they occupy the same frontend issue
;; slots that elapse while the mod-write retires), Tensix words at
;; their audited slot counts, launch words at the one-word
;; conservative floor.  Per-crossing stall = max(0, W_drain -
;; iteration_slots).
;;
;; Five-witness derivation (whole-ELF silicon, the entire math.elf
;; delta is the transform in every case):
;;
;;   UNCOVERED (skinny) regime -- 5-slot iterations (3 Tensix +
;;   2 scalar), measured stall per crossing:
;;     absint32 hand         16.950 -> 18.853  = 1.38 c/crossing
;;     unaryshift-fresh sem  16.962 -> 19.631  = 1.57 c/crossing
;;     bitwisenot hand       16.950 -> 18.853  = 1.38 c/crossing
;;   => W_drain - 5 ~= 1.4..1.6, W_drain ~= 6.4..6.6.
;;
;;   COVERED (fat) regime -- 10/12-slot iterations, measured stall per
;;   crossing (from the ~2-cycle/tile residual the pin-15 fired forms
;;   pay over 32 crossings):
;;     threshold-fresh sem   0.064 c/crossing
;;     hardshrink-fresh sem  0.061 c/crossing
;;   => W_drain <= 10.  (Refusing these shapes at pin 16 under the
;;   old 2-slot-guard walk -- which ignored the iteration's own body
;;   words -- reinstated 29 slots/tile and measured +26.95/+27.06
;;   booked: the EP-F1 counterexample that forced this audit.)
;;
;; The audited value takes the CONSERVATIVE 7 (fit ~6.5): it preserves
;; every witness verdict on both sides, including the eight-row rolled
;; hand winner at 12.955 (-23.6%, ~26-slot iterations).  The Wormhole
;; capability entry carries the same value as the same-frontend-class
;; conservative adoption (no WH silicon witness; a larger W only
;; widens refusal).  The once-per-loop-entry drain residual -- the
;; ~2 cycles/tile TOTAL the covered witnesses still measure, the first
;; crossing's cost before the pipeline reaches steady state -- is
;; charged on the configuration-cost side at the audited
;; min_config_distance (2), never per iteration.
;;
;; An audited issue-time RWC writer (surviving explicit TTINCRWC,
;; typed face advance) between the last terminator and the backedge
;; re-anchors the crossing and clears the charge.  Groups whose
;; per-iteration rows cannot pay refuse by name
;; (mod-write-dominates-rolled-body).  The charge is a model-derived
;; bracket of an unaudited quantity, not a tuned constant: the same
;; W_drain separates the measured one-row 5-slot losers (charge 2 >=
;; 1 row: refuse) from the measured 10-slot one-row winner class
;; (threshold: covered, fire) and the eight-row winner (covered,
;; fire) with no trip-count or body-length threshold anywhere.
;;
;; Calibration cross-check (lane EE whole-row closure model,
;; laneEE-evidence-20260821/LOSER-ANATOMY.md -- reproduces all 14
;; anatomized measured cells within ~3%): the measured
;; per-TTREPLAY-launch boundary cost of ~1.3-1.8 cycles on
;; serial-chain windows means the launch word the covering walk
;; credits as ONE slot in fact separates producer from consumer by
;; MORE than a slot -- the credit is a conservative floor, never an
;; overcount.  EE also supplies the third skinny witness (bitwisenot
;; hand-ON, above); this pricing term restores that row's honest hand
;; baseline.  (EE's launch-vs-straight-push arbitration -- pricing a
;; whole replay-window formation against rolled push delivery with
;; the same boundary term -- is replay-formation territory, recorded
;; there as the named follow-up.)
;;
;; AUDITED COMPOSITION FACT: no-exec record composition (lane ES,
;; laneES-evidence-20260821).  Composing the store-side mod-write with
;; a replay capture recorded WITHOUT execution (TTREPLAY load=1
;; exec=0) in the same function is silicon-refuted.  The device 2x2 on
;; the lcm-fresh Int32/dest-acc kernel (identical TU, pin-17 compiler
;; ae7342e4fda3, one flag toggled per cell, solo flocked runs, .text
;; recorded per cell):
;;
;;   autoincr fired, no-exec record absent   (08d62bac...)  PASS
;;   no-exec record fired, autoincr refused  (fd8c5ac4...)  PASS
;;   BOTH fired                              (1c0bdce0...)  TENSIX
;;     TIMED OUT (Math/Unpacker/Packer), device wedged until tt-smi
;;     reset -- reproduced twice on a proven-healthy device (sweep
;;     09:29:45 + lane ES controlled solo re-run), and the wedge
;;     poisoned every subsequent device job until reset (the pin-17
;;     divint32floor/log corr "hangs" were this collateral).
;;
;; The byte deltas between the hang binary and each passing neighbor
;; are exactly one transform each, so the hazard is the COMPOSITION:
;; a no-exec recording window swallows the following Tensix words
;; while the mod-write's positional-state retirement (W_drain above)
;; can still be in flight from the previous iteration's terminator,
;; and the composed state wedges the Vector Unit pipeline (the
;; math thread's STALLWAIT C11 drain condition never satisfies, so
;; SEMPOST to the packer never issues).  Exec-while-record captures
;; and launches carry fleet-wide silicon witnesses (minmax, sdpa,
;; where, typecast, lcm ON-set) and stay admitted.  BlackholeA0 has NO
;; REPLAY functional model in tt-isa-documentation (doc gap, filed
;; ES-F1), so no finer-grained fact can be audited yet; until one is,
;; rtl-rvtt-dst-autoincr.cc refuses per group by name
;; (mod-write-noexec-record-composition-unaudited).
;;
;; The witnessed safe/unsafe boundary is DISTANCE, not presence: the
;; hang witness's no-exec record sits in the inner loop's dedicated
;; preheader INSIDE the face loop, re-executing FIVE issue-slot words
;; after the previous face group's final mod-write store -- inside the
;; audited drained-frontend retirement window (W_drain = 7 above).
;; Every passing composition separates the record from the stores by
;; at least that window or is unreachable from them: the per-tile LLK
;; wrapper records (celu/eqz-class ON-set rows, 24 corpus rows) sit
;; behind the chunk-boundary synchronization's dozens of issue words;
;; xielu-fresh's preamble record and gcd/lcm's run_kernel init records
;; are not reachable from any group store at all (all device PASS).
;; The guard therefore prices the SAME audited quantity as the
;; crossing charge: the minimum issue-slot word distance over CFG
;; paths from the group's block to the capture, at the audited
;; W_drain.  Unreachable or covered admits (bytes preserved on every
;; witnessed-good row); nearer refuses by name.
;;
;; DELIVERY BOUNDARY OF THE DISTANCE PROXY (lane FE finding F1,
;; laneFE-evidence-20260822; guard extension lane FJ).  The
;; frontend-word distance is an audited retirement proxy only at
;; ISSUE PARITY: an explicit row's mod-write store is itself a
;; frontend word, so N subsequent frontend words bound its retirement
;; from below.  Every witness behind the W_drain fit is an
;; explicit-row shape, and the witnessed-good celu/eqz-class
;; compositions (24 ON-set corpus rows, silicon-good across many
;; pins) are explicit mod-write rows with an in-loop no-exec wrapper
;; record behind >= W_drain words.  A REPLAY-DELIVERED row (launch or
;; executing capture whose payload carries the mod-write terminator)
;; breaks the premise: the launch issues one frontend word while the
;; expander delivers the payload asynchronously, so no frontend word
;; count after the launch bounds the store's retirement.  The
;; refuting silicon witness: sparse_k_filter Int32/dest-acc sem-ON
;; (pin 19) -- a 32-launch group whose own no-exec record (TTREPLAY
;; 0,11,0,1) re-ingests the mod-write payload one block earlier in
;; the tile loop, admitted covered at 20+ frontend words -- wedges
;; Tensix (TENSIX TIMED OUT, Math/Unpacker/Packer) at RUNTIME trip
;; count 32 and passes at trip 8 on BYTE-IDENTICAL code (runtime
;; TILE_CNT axis, 2/2 device reproductions, flush-verified healthy
;; device, default-flags t32 re-verified PASS), while the pinned sim
;; passes both legs (frontend/RWC retirement timing unmodeled).
;; Admission at any static distance therefore decays to runtime
;; semaphore pacing the model cannot see.  Until BlackholeA0 REPLAY
;; documentation exists (ES-F1 doc gap still open), groups with any
;; replay-delivered row refuse every reachable (or same-block)
;; no-exec capture at ANY distance, by the same name; the W_drain
;; window rule remains in force for issue-parity (explicit-row)
;; groups, whose witnesses carry it.
;;
;; PLACEMENT SIDE OF THE SAME FACT (lane FL, FH-1).  The dst-autoincr
;; guard above prices the composition only against that pass's OWN
;; mod-write groups; the replay former PLACES no-exec captures and
;; must audit the identical fact against the stream's audited
;; mod-write classes -- the typed SETC16 address-modifier programming
;; word and typed Dst accesses through non-no-increment address
;; modifiers (the mod0-6 class).  rtl-rvtt-replay.cc's fail-closed
;; end-of-transform sweep un-hoists, by name
;; (noexec-record-modwrite-window-unaudited), any still-no-exec
;; formed capture whose recording window can open within W_drain
;; issue words of such a word on any CFG path (minimum-distance walk,
;; the exported rvtt_modwrite_drained_frontend_window constant --
;; both consumers price the SAME audited quantity); launches become
;; inline payload copies (the identity the capture was formed from).
;; Issue-time RWC writers (TTINCRWC, the SET/FACE separator class,
;; raw pure-Dst/RWC words) are outside the class -- the crossing
;; model records them as re-anchoring issue-time words, and the
;; celu/eqz-class wrapper-record adjacency behind raw STALLWAIT-class
;; words is silicon-witnessed good across many pins.  Undecodable
;; words earn zero cover in the walk (they never manufacture
;; separation) but are not themselves hazards -- the same audit
;; boundary the group guard's own function-scan has.
;;
;; REPLAY-STATE PERSISTENCE MODEL (lane FS, FP-3; the BH REPLAY doc gap,
;; laneFS-evidence-20260822).  The reach walks above (dst-autoincr group
;; guard, W_drain placement sweep) are BOTH intra-function.  The FP delta
;; audit (FP-3) witnessed shapes that ADMIT under both walks yet reassemble
;; the wedge trio ACROSS the analysis boundary.  Controlled Blackhole
;; silicon experiments (p150, dual flocks, flush-verified; vehicle =
;; datacopy-acquired 32-bit DEST with packer readback; sentinel FP32
;; 0x42F70000 written by a recorded SFPSTORE the launch delivers) settle the
;; hardware model the doc gap left open:
;;
;;   EXP-2  A record in one function body (TT_REPLAY load=1 exec=0 + payload)
;;          and a launch (load=0) in a SIBLING function body, one kernel
;;          launch, deliver the recorded store (sentinel present).  The
;;          per-thread Replay Expander buffer persists across function-call /
;;          basic-block boundaries within a launch -- the pfj1 shape is real.
;;   EXP-1  A kernel A that ONLY records (never launches) then exits, followed
;;          by a SEPARATE kernel B (distinct ELF, TRISC soft-reset + ELF
;;          reload + brisc device_setup() between) that ONLY launches, with no
;;          record of its own, DELIVERS kernel A's recorded store.  The buffer
;;          persists across the standard kernel-invocation (soft-reset)
;;          dispatch boundary.  device_setup() issues no REPLAY and neither
;;          re-arms nor clears the buffer.
;;   BASE   Launch-only as the first REPLAY-issuing kernel after a FULL board
;;          reset (flush.sh / tt-smi -r) WEDGES (TENSIX TIMED OUT): a full
;;          reset clears the buffer to zero words whose replay is not an inert
;;          no-op.  So only a full board reset clears replay state; the
;;          soft-reset boundary does not.
;;
;; CONSEQUENCE: a compiler-FORMED still-no-exec capture with a Dst-store
;; payload that is not consumed by a launch its record dominates is a latent
;; cross-path / cross-invocation deliverer of the silicon-refuted composition
;; -- the intra-function walks cannot see the launch.  rtl-rvtt-replay.cc's
;; end-of-transform sweep therefore adds a third fail-closed rule
;; (noexec-record-dststore-nondominating-launch-persist-unaudited): a formed
;; still-no-exec Dst-store capture whose record does not dominate EVERY launch
;; of its span (or has no in-function launch at all) is un-hoisted by the same
;; identity-restoring action (launches -> inline payload copies, record +
;; shadow deleted).  The dominating-preamble class (xielu/gcd/lcm init records
;; dominating their in-loop launches, all device PASS) executes the record
;; before every launch in the same invocation and is preserved; the witnessed
;; exec-while-record conversions (exec=1) are outside the still-no-exec filter.
;; User-authored records remain the user's own contract (the sweep touches
;; only pass-formed captures); the same-function admission of user records is
;; the group guard's territory above, filed as the remaining FP-3 semantics
;; item.
(define_attr "xtt_result_latency" ""
  (const_int 0))

;; Architectural next-slot ACCEPTANCE stall (SFPSWAP.md: "on the next
;; cycle, the only instruction that the Vector Unit can accept is
;; SFPNOP; hardware will automatically stall the thread" otherwise).
;; A structural per-instruction fact, distinct from result latency: the
;; result is consumable by the very next ACCEPTED instruction, but that
;; instruction issues one slot late.  Consumers: reissue pricing charges
;; one extra slot per occurrence; the interlock scheduler REFUSES any
;; next-slot-stall instruction as a fill participant (preserving its
;; pre-audit behavior exactly).  Default no; set only under an audited
;; per-instruction provenance block.
(define_attr "xtt_next_slot_stall" "no,yes"
  (const_string "no"))

;; LREG writeback-port occupancy.  Replaces the SFPSWAP-borrows-MAD and
;; Simple/Round shared-port folklore previously embedded in pass code.
(define_enum "xtt_lreg_write_port" [
  none
  own
  shared_simple_round
  borrows_mad
])
(define_enum_attr "xtt_lreg_write_port" "xtt_lreg_write_port"
  (const_string "none"))

;; Bitmask of operand positions that may read (resp. write) an LREG,
;; stored with a +1 bias because generated attributes cannot be negative:
;; 0 means unaudited (refusing), M+1 means position mask M.  Positions are
;; resolved to hard-register masks post-RA by rvtt_insn_effects, which is
;; the only decoder of the bias.
(define_attr "xtt_lreg_read_ops" ""
  (const_int 0))
(define_attr "xtt_lreg_write_ops" ""
  (const_int 0))

;; Whether LREG16 (the macro side-load target) is a legal destination.
(define_enum "xtt_lreg16_dest" [
  no
  yes
])
(define_enum_attr "xtt_lreg16_dest" "xtt_lreg16_dest"
  (const_string "no"))

;; Architectural CC (lane-state) effect.  `unknown' is the refusing
;; default; `read' covers ordinary lane-predicated value operations.
;; Mod-dependent CC writes are refined through the CC(mask) data in
;; rvtt-insn.def, never widened here.
(define_enum "xtt_cc_effect" [
  unknown
  none
  read
  write
  readwrite
])
(define_enum_attr "xtt_cc_effect" "xtt_cc_effect"
  (const_string "unknown"))

;; SFPU configuration effect.  `dest' marks a config write whose
;; destination register is the operand at xtt_config_dest_op.
(define_enum "xtt_config_effect" [
  unknown
  none
  dest
  read
])
(define_enum_attr "xtt_config_effect" "xtt_config_effect"
  (const_string "unknown"))
;; Operand position of the config destination, +1 biased (0 = unset).
(define_attr "xtt_config_dest_op" ""
  (const_int 0))

;; Dst/RWC counter effect class.  `inc'/`set'/`face' are typed counter
;; operations whose deltas live in typed operands; `addr_mode' marks a
;; load/store whose RWC effect is decided by its address-mode operand and
;; is resolved per-operand by rvtt_insn_effects.  `unknown' refuses.
(define_enum "xtt_rwc_effect" [
  unknown
  none
  inc
  set
  face
  addr_mode
])
(define_enum_attr "xtt_rwc_effect" "xtt_rwc_effect"
  (const_string "unknown"))

;; Fast gate: whether a macro template encoder exists for this insn's
;; unspec.  The authoritative answer is the Layer-4 capability table; this
;; attribute only lets region discovery skip hopeless candidates early.
(define_enum "xtt_macro_encodable" [
  no
  yes
])
(define_enum_attr "xtt_macro_encodable" "xtt_macro_encodable"
  (const_string "no"))

;; Replay-hoist profitability constants.
;;
;; Units: hundredths of one Tensix instruction issue slot (centislots).
;; The x100 scaling exists only to express the measured RISC-delivery
;; premium as an integer; the issue reservations below stay in whole
;; slots.
;;
;; Two per-slot rates describe replay delivery economics:
;;
;;   RISC_PUSH_X100 (123)   - one instruction word pushed by the RISC
;;                            core through the Tensix instruction FIFO.
;;                            The 1.23x premium over a replayed slot is
;;                            the silicon-refit delivery-cost model
;;                            (re-fit from measured actuals; the older
;;                            2.2:1 ratio over-predicted
;;                            launch-conversion gains ~7x).
;;   REPLAY_SLOT_X100 (100) - one slot reissued by the Tensix replay
;;                            expander with no RISC involvement.
;;
;; The profitability model in rtl-rvtt-replay.cc prices one loop entry
;; (2026-08-19 re-record re-derivation; the audited-interlock reissue
;; terms are Lane BP's 14-shape recalibration):
;;
;;   deliver_body   = words * RISC_PUSH_X100     ; per-trip delivered words
;;   deliver_record = (1 + words) * RISC_PUSH_X100 ; capture word + payload
;;   exec    = exec_interlocked_slots * REPLAY_SLOT_X100
;;                                               ; dependence-tracked with
;;                                               ; the audited
;;                                               ; xtt_result_latency and
;;                                               ; xtt_next_slot_stall
;;                                               ; facts; an unaudited
;;                                               ; consumed producer makes
;;                                               ; the payload unpriceable
;;                                               ; (named refusal
;;                                               ; replay-reissue-latency-
;;                                               ; unproved)
;;   after   = max (RISC_PUSH_X100, exec + TURNAROUND_X100)
;;                                               ; one launch push; the
;;                                               ; replay unit reissues
;;                                               ; the payload
;;
;;   counted-loop capture (the body records nothing per trip):
;;     before = max (deliver_body, exec)
;;     record = deliver_record + RECORD_OVERHEAD_X100
;;
;;   re-record body, EXECUTION-bound (exec >= deliver_record):
;;     before = exec + RECORD_OVERHEAD_X100      ; the in-loop record pass
;;                                               ; executes the payload at
;;                                               ; its interlocked pace and
;;                                               ; exposes the record
;;                                               ; engine's per-pass
;;                                               ; overhead on the critical
;;                                               ; path
;;     record = RECORD_OVERHEAD_X100             ; the hoisted preheader
;;                                               ; pass's delivery hides
;;                                               ; behind the loop's own
;;                                               ; execution backlog
;;                                               ; (delivery is CONCURRENT
;;                                               ; with playback execution;
;;                                               ; witnesses below)
;;
;;   re-record body, DELIVERY-bound (exec < deliver_record):
;;     before = deliver_record                   ; in-loop record WITH
;;                                               ; execution: the payload
;;                                               ; does the loop's real
;;                                               ; work while recording,
;;                                               ; execution AND the record
;;                                               ; overhead absorbed in the
;;                                               ; per-word delivery slack
;;                                               ; (RISC_PUSH >= REPLAY_SLOT)
;;                                               ; -- unless hidden (below)
;;     record = deliver_record + RECORD_OVERHEAD_X100
;;     surplus = launch_run * (exec - RISC_PUSH_X100)
;;     hidden  = surplus >= deliver_record       ; the record pass's
;;                                               ; delivery streams into
;;                                               ; the contiguous sibling
;;                                               ; launch run's execution
;;                                               ; shadow: before = after
;;
;;   benefit = trips * (before - after) - record
;;   hoist iff benefit >= MIN_BENEFIT            ; trips provably constant
;;
;; The context term (launch_run) is computed from the candidate's own
;; statically known structure: the number of sibling occurrences of the
;; capture in the loop body and the delivered words between them, where a
;; typed per-row Dst-counter increment separator is discounted when the
;; Dst auto-increment pass -- which runs after replay formation and
;; absorbs exactly those separators around replay launches -- is enabled.
;; A contiguous run of R launches occupies the issue plane for R * exec
;; centislots while delivering only R * RISC_PUSH_X100 words; once its
;; surplus covers the record pass's delivery, that delivery is hidden
;; under execution and the hoist's true benefit degenerates to -record
;; (the preheader record-only pass is pure cost).  The term applies to
;; DELIVERY-bound re-record bodies only: a single launch of a
;; delivery-bound payload can never hide its record pass (exec <
;; deliver_record in that branch), so counted-loop hoists -- one clone
;; per trip, launches always separated across trips by the loop-control
;; delivery -- and every other single-instance shape are arithmetically
;; unaffected.  An EXECUTION-bound re-record pass is never hidden this
;; way: its cost is its own interlocked execution plus the exposed
;; record-engine overhead, which no sibling surplus can absorb (the
;; Reduce-class A/B below measured the in-loop record pass exposed
;; inside a fully execution-backlogged, 8-contiguous-sibling body).
;; The saturation term is part of the modeled
;; benefit, not of the threshold: -mtt-tensix-replay-hoist-min-benefit=
;; cannot force a hoist whose record delivery is hidden.
;;
;; The superseded model charged the removed in-loop recording at full
;; payload length, as if recording were pure overhead.  It is not: a
;; record-with-execution pass performs the loop's real work while it
;; records, so hoisting it away saves only the RISC-delivery premium,
;; while the hoisted record-only pass is bought at full delivery price
;; with no work done.  That accounting rewarded LONG captures at LOW
;; trip counts -- exactly the class silicon measures as regressing --
;; and starved short-payload shapes.
;;
;; Calibration (Blackhole same-source silicon A/Bs, identical
;; source/input/golden, only compiler flags differing, three fresh
;; processes per selector; the 2026-08-16 p150-class re-measurement
;; reproduced the archived p100a absolutes for the Reduce-class rows,
;; 855.5 and 839.0 cycles/body exactly; the unary-max/min point is the
;; 2026-08-16 nightly flip root-cause, MATH_ISOLATE TILE_LOOP,
;; deterministic on both eras):
;;
;;   shape (trips, length)      old benefit  new benefit  silicon
;;   counted loop (8, 24)           148         2325      -9.83%  WIN
;;   preheader capture (4, 8) x2     16          121      855.5 -> 834
;;                                                        cyc/body WIN
;;   repeated-seq (4, 17)            34         -158      +1.81%  LOSS
;;   repeated-seq (4, 31)            62         -592      +2.30%  LOSS
;;   exec-saturated (4, 4) x8 sib     8         -615      +2.06%  LOSS
;;                                               (245 without the
;;                                                context term)
;;
;; (The two losing shapes modeled as 3 trips / benefits 16 and 30 on the
;; measurement-era stack; both trip readings are negative under the new
;; model, (3,17) = -672 and (3,31) = -1428, so the fit is insensitive to
;; that ambiguity.)
;;
;; The superseded model ordered the (4,8) silicon WINNER (16) strictly
;; below both silicon LOSERS (34, 62): no threshold could separate them,
;; which is how the old default-64 gate refused the measured
;; 21.5-cycle/body Reduce-class win.  The new model is sign-correct on
;; all four measured points using only the pre-existing 1.23 delivery
;; ratio -- no constant here is fitted to a shape identity.
;;
;; Execution-saturation context term, derivation from the measured
;; points.  The unary-max/min flip (root-caused 2026-08-16) showed the
;; delivery-only `before = deliver' is FALSE for a loop body whose
;; sibling launches of the same buffer are contiguous in the final
;; stream: (trips 4, length 4, 8 siblings/trip) modeled +245 and
;; measured +2.06% LOSS (+3.93 cyc/tile against a modeled -2.45), while
;; the measured +121 Reduce-SDPA winner (trips 4, length 8, 8
;; siblings/trip) has every launch separated by a surviving typed Dst
;; increment and realizes most of its modeled delivery relief.  No
;; MIN_BENEFIT can order 245 (loss) above 121 (win): the model needed a
;; context term, not a threshold move.  Aggregate plane occupancy cannot
;; be that term -- BOTH bodies are execution-heavy in total (32 exec
;; slots vs <= 12 delivered words/trip; 64 vs ~27) -- the measured
;; discriminator is CONTIGUITY of the launch run.  Arithmetic on all
;; five calibration shapes (PUSH = 123, SLOT = 100 centislots):
;;
;;   shape                 deliver  execute  after  run  surplus   hidden  benefit           decision
;;   SDPA-exp (8,24) n=1     3075     2400    2400   1  1x2277=2277 <3075  8*(3075-2400)-3075 = +2325  FIRE  (unchanged)
;;   ReduceSDPA (4,8) n=8    1107      800     800   1  1x 677= 677 <1107  4*(1107- 800)-1107 =  +121  FIRE  (unchanged)
;;   Log (4,17) n=8          2214     1700    1700   1* 1x1577=1577 <2214  4*(2214-1700)-2214 =  -158  refuse (unchanged)
;;   Log1p (4,31) n=8        3936     3100    3100   1* 1x2977=2977 <3936  4*(3936-3100)-3936 =  -592  refuse (unchanged)
;;   unarymaxmin (4,4) n=8    615      400     400   8  8x 277=2216 >=615  4*(   0     )- 615 =  -615  refuse (NEW)
;;
;; A drain-inclusive caller can request the stricter completion contract with
;; -mtt-tensix-replay-hoist-completion-guard.  In the ordinary replay-hoist
;; model, it changes execution-bound re-record loops only: it does not assume
;; that the hoisted preheader record delivery hides behind the loop's execution
;; backlog, so `record' is charged as DELIVER_RECORD + RECORD_OVERHEAD instead
;; of RECORD_OVERHEAD.  The counted-loop and delivery-bound branches of that
;; model already charge the complete record and are unchanged.  This is
;; deliberately opt-in: it is a completion-accurate scope guard, not a
;; replacement for the silicon-calibrated body throughput model, and contains
;; no operation or kernel identity test.  Guarded record-hoist deliberately
;; changes from its delivery-only model to the shared binding-resource model.
;; The semantic definition is completion accuracy, not a heuristic demand for
;; a conservative verdict.  Nevertheless the legal replay domain makes its
;; execution-bound verdict provably monotone relative to the measurement
;; model: MIN_SEQUENCE requires at least four delivered words, and the exact
;; benefit difference derived below is then strictly negative.
;; Both replay-hoist entry modes honor that contract.  The dedicated
;; -mtt-tensix-optimize-replay-record-hoist issue-side model normally cancels
;; payload execution between the two worlds.  Under the completion guard it
;; instead retains the audited interlocked-execution term and enters this
;; shared binding-resource model: delivery-bound records charge the complete
;; record exactly once, while execution-bound records additionally lose the
;; body-throughput model's hidden-delivery credit.  An unaudited payload
;; refuses by replay-reissue-latency-unproved because its binding resource is
;; not known.  No delivery word is double charged.
;;
;;   (* Log/Log1p at their measurement flags: the Dst auto-increment
;;   pass is disabled there, so their typed increment separators
;;   survive and run = 1; they refuse on the delivery benefit exactly
;;   as before, message and number byte-identical.  Under a flag set
;;   that enables the auto-increment absorption their runs become 8 and
;;   the printed benefit is -deliver instead -- still a refusal, still
;;   byte-identical object code.)
;;
;;   unarymaxmin at its measurement flags (full ON set): the Dst
;;   auto-increment pass absorbs the per-row typed increment between
;;   the 8 sibling launches (rvtt-passes.def ordering contract), the
;;   final-stream run is 8, surplus 8*(400-123) = 2216 >= deliver 615,
;;   the record delivery is hidden, before = after, benefit = -615 <
;;   60: refuse, byte-identical to the pre-recalibration refusal (the
;;   silicon-measured winning form).  ReduceSDPA at its measurement
;;   flags disables the auto-increment pass, its increments survive,
;;   run = 1, surplus 677 < 1107: not hidden, benefit +121 exactly as
;;   before.  Counted-loop shapes pass run = 1 by construction (one
;;   clone; trips separated by loop-control delivery), and run = 1 can
;;   never hide any record pass (length*100 - 123 < (1+length)*123 for
;;   all lengths), so SDPA-exp and the whole n=1 class are
;;   arithmetically untouched.
;;
;; Predicted-band consistency: the unarymaxmin root-cause bracketed the
;; measured +3.93 cyc/tile loss by [zero residual saving = +6.15 ...
;; model = -2.45]; the context term prices exactly the zero-residual
;; end (before = after), the conservative bound of the measured band,
;; using only the two pre-existing rates -- no newly fitted constant.
;;
;; RE-RECORD RE-DERIVATION (2026-08-19; the tables above are the
;; delivery-only-era history and their run/benefit columns describe
;; that era's arithmetic).  The 2026-08-18 interlock recalibration's
;; first re-record spelling -- before = max(deliver_record +
;; RECORD_OVERHEAD, exec), record = deliver_record + RECORD_OVERHEAD,
;; saturation term applied to every re-record body -- inverted BOTH
;; re-record silicon anchors at the installed pin:
;;
;;   - Reduce-class (trips 4, words 8, exec_ilk 12 with the audited
;;     SFPSWAP acceptance stalls, deliver_record 1107, two hoist
;;     sites): priced -859 (its measurement flags, launch run 1) and
;;     -1407 (full ON set, run 8, saturation-clamped) -- refusing the
;;     measured 855.5 -> 832.75 = 21.5+ cyc/body silicon WIN
;;     (gatefix-evidence-20260816 step-1 A/B, three fresh processes
;;     per selector).
;;   - Log-class (trips 4, words 17, exec_ilk 17, deliver_record
;;     2214): priced +462 and FIRED at its measurement flags (run 1)
;;     -- firing a measured +1.81% silicon LOSS the delivery-era
;;     model refused at -158.
;;
;; The corrected split prices which resource the in-loop
;; record-with-execution pass is bound by:
;;
;;   EXECUTION-bound (exec >= deliver_record): word delivery is
;;   concurrent with execution (max, not sum -- the sigmoidappx
;;   pure-delivery control, 64 delivered words removed = +0.006 units
;;   noise, and the exp pre-Z -> Z increment, -33.0 measured vs -32
;;   modeled execution-side; see the MOP section below), so the
;;   record pass costs its interlocked execution plus the exposed
;;   record-engine overhead: before = exec + RECORD_OVERHEAD.  The
;;   hoisted preheader record-only pass's delivery hides behind the
;;   execution backlog such a loop necessarily accumulates (its
;;   launches deliver one word each and execute exec >= deliver_record
;;   apiece); its exposed cost is charged at the full engine overhead:
;;   record = RECORD_OVERHEAD.  Anchor arithmetic, Reduce-class:
;;   before = 1200 + 300 = 1500, after = 1200 + 70 = 1270, benefit =
;;   4*(1500-1270) - 300 = +620 >= 60, FIRE -- against silicon net
;;   +1075 cs/site (855.5 -> 832.75 over two 4-trip sites) and
;;   measured per-trip record exposure 2275/8 ~= 284 cs vs the modeled
;;   RECORD_OVERHEAD - TURNAROUND = 230 cs: the model under-claims
;;   both, the safe direction.  Two independent witnesses back the
;;   hidden preheader delivery: the Reduce-class net leaves ~61 cs per
;;   preheader pass unaccounted (2*record_true ~= 2275 - 8*284), and
;;   the counted-loop clamp row measured +0.58 cyc/tile amortized
;;   record delivery (laneBP-evidence-20260818 §4) against a 12-word
;;   deliver_record of 1476 cs -- both ~ 4-25x smaller than the
;;   RECORD_OVERHEAD = 300 charged.
;;
;;   DELIVERY-bound (exec < deliver_record): the delivery-only-era
;;   calibration is restored exactly (before = deliver_record; the
;;   record pass's execution and engine overhead fit inside the
;;   per-word delivery slack), with the interlock era's TURNAROUND in
;;   `after' and RECORD_OVERHEAD in `record' pushing every refusal
;;   further negative -- byte-identical refusals.  Anchors: Log
;;   (4,17): 4*(2214 - 1770) - 2514 = -738, refuse (silicon +1.81%
;;   LOSS; delivery-era -158); Log1p (4,31): 4*(3936 - 3170) - 4236 =
;;   -1172, refuse (silicon +2.30% LOSS); unary-max/min (4,4, run 8,
;;   full ON): saturation-clamped, benefit = -record = -915, refuse
;;   (silicon +2.06%/+3.93 cyc/tile LOSS) -- today that payload's
;;   cst-LREG SFPSWAP is effect-unaudited and the shape refuses
;;   upstream as replay-reissue-latency-unproved; the clamp arithmetic
;;   documents the class for the day the audit lands, at which point
;;   the unary-max/min silicon point must re-validate this branch.
;;
;;   The counted-loop branch is untouched by the re-derivation: the
;;   14-shape validation matrix (laneBP-evidence-20260818) and the
;;   five-loser refusals calibrate it as of the interlock
;;   recalibration.
;;
;;   No-silicon bands of the re-derivation, pre-registered: (a)
;;   execution-bound re-record fires at any provable trips >= 2
;;   (benefit = trips*230 - 300); the only silicon point is trips 4
;;   (Reduce-class, two sites) -- a trips-2 or trips-3 fire has no
;;   silicon yet.  (b) The full-ON Reduce-class form (Dst
;;   auto-increment absorbs the launch separators, run 8) fires by the
;;   same execution-bound arithmetic; its silicon record is the run-1
;;   measurement-flag form -- the run-8 fire is pre-registered for
;;   nightly full2x2 adjudication (expected <= the 855.5 unhoisted
;;   bound, target ~832.75).  (c) The execution-bound boundary exec ==
;;   deliver_record sits in the exec-bound branch; shapes within one
;;   stall slot of the boundary flip branches on a single audit fact
;;   change and have no silicon.
;;
;; RECORD-HOIST MEASUREMENT PRICING (-mtt-tensix-optimize-replay-record-
;; hoist, default off; re-record bodies only, the counted-loop branch is
;; untouched).  DX-F3 (laneDX-evidence-20260820, lcm decomposition)
;; measured the in-loop `ttreplay 0,len,1,1' re-delivering len+1 words
;; per row on a shape the saturation term prices as fully hidden, and
;; attributed ~2-3 words/row of the row's residual loss to that
;; re-delivery by word accounting.  Under this flag a re-record body
;; whose capture window is proven iteration-invariant (every payload
;; word fixed-encoding: hard LREGs and constants only -- a run-time-
;; composed word refuses record-hoist-variant-encoding) prices
;; issue-side only:
;;
;;   benefit = trips * (deliver_body - TURNAROUND) - (deliver_record + RECORD_OVERHEAD)
;;
;; The per-trip TURNAROUND term charges the launch boundary the hoist
;; ADDS (the first clone converts from inline delivery to one more
;; playback launch).  Lane EE's boundary calibration measured 1.3-1.8
;; cycles per launch boundary on serial-chain windows (independent fits
;; on the ceil/log/rsqrt anatomy, laneEE-evidence-20260821) -- above the
;; 0.7-slot table constant; the ~60-110 cs/trip under-charge is absorbed
;; by the MIN_BENEFIT margin and is on the fire side, which is
;; acceptable for a measurement flag and recorded here for the
;; promotion review.
;;
;; Derivation: the executed word stream of the hoisted and unhoisted
;; worlds is identical (each launch expands to exactly the recorded
;; words at the same stream positions the in-body clones held; the
;; hoisted no-exec record executes nothing), so every execution-side
;; term cancels in the difference and the modeled delta is pure
;; delivery: `words' pushed words saved per trip, bought once at the
;; preheader record's full delivery plus engine overhead.  For proven
;; trips >= 2 the hoisted world delivers strictly fewer words on every
;; execution (monotone in the delivery model).  HONEST OPPOSITION: the
;; Log-class silicon anchors above ((4,17) +1.81%, (4,31) +2.30%) are
;; re-record shapes whose hoist FIRED under the delivery-only era model
;; and MEASURED AS LOSSES -- evidence that on those rows the per-trip
;; record delivery was in fact hidden behind execution (the physics the
;; saturation/MAX model encodes).  lcm's shape differs in trips (8 vs
;; 4), sibling launch count, and round-chain seriality, and has NO
;; hoisted silicon point in either direction.  This flag is therefore a
;; measurement class in the -mtt-tensix-mop-form-force pattern: every
;; structural, invariance, slot-liveness, and audited-latency proof
;; still gates admission and refuses by name (record-hoist-loop-shape /
;; -loop-opaque / -no-dedicated-preheader / -preheader-recording-open /
;; -trip-count-unproven / -variant-encoding /
;; replay-reissue-latency-unproved / record-hoist-benefit); only the
;; profitability verdict changes, and promotion into any default or ON
;; set requires this class's own silicon A/B.
;;
;; MIN_BENEFIT = 60 centislots (0.6 slot per loop entry): every measured
;; losing shape models negative (max -158), so any non-negative
;; threshold refuses the entire measured losing class with the whole
;; band as buffer; 60 is approximately half the smallest measured
;; winning benefit (121), keeping ~2x headroom under the nearest winner
;; while refusing modeled-marginal shapes, because refusal is
;; byte-identical code and costs nothing.  The dynamic-pipeline costs
;; Blackhole adds around a hoisted record-only capture are not itemized
;; by this static model; the threshold margin is the only buffer
;; standing in for them.  No-silicon band: the acceptance region
;; [60, 121) of modeled benefit -- including the test-pinned (4,9)=90
;; fire -- has zero silicon points; a silicon A/B remains the promotion
;; backstop for any shape class landing there.  The
;; -mtt-tensix-replay-hoist-min-benefit= option (same centislot units)
;; overrides this table value for experimentation.
;;
;; COMPLETION-GUARDED MEASUREMENT PRICING.  With both record-hoist and the
;; completion guard enabled, the reissue audit is retained and the candidate
;; uses the ordinary shared binding-resource arithmetic above, including the
;; full preheader record delivery exactly once.  For an execution-bound
;; re-record with W delivered words and T proven trips, both models charge the
;; same one-time record cost 123*(W+1)+300.  Their per-trip terms are:
;;
;;   unguarded measurement model: 123*W - 70
;;   completion shared model:     (EXEC+300) - (EXEC+70) = 230
;;   guarded benefit - unguarded benefit
;;     = T * (230 - (123*W - 70))
;;     = T * (300 - 123*W)
;;     <= -192*T, because the replay former's MIN_SEQUENCE makes W >= 4.
;;
;; The generic legal four-word, four-trip next-slot-stall witness on both BH
;; and WH pins the boundary (PUSH=123, SLOT=100, TURNAROUND=70,
;; RECORD_OVERHEAD=300):
;;
;;   words=4, exec_ilk=8 slots, deliver_body=492, deliver_record=615
;;   record=615+300=915
;;   unguarded: 4*(492-70)-915 = 773 >= 60              FIRE
;;   guarded: before=800+300=1100, after=800+70=870,
;;            4*(1100-870)-915 = 5 < 60                refuse
;;   difference: 5-773 = -768 = 4*(300-123*4)
;;
;; No identity, opcode, payload-length special case, or target-specific
;; threshold selects the result; BH and WH share the audited rates and
;; resource effects.  A two-word arithmetic reversal exists outside this
;; domain, but is not a compiler candidate because it is below MIN_SEQUENCE.
;;
;; RECORD-HOIST RUNTIME-TRIP ADMISSION (lane FW, 2026-08-22; rides the
;; same measurement flag without the completion guard).  The blaze
;; sdpa_reduce_row loss class -- the
;; only corpus kernel whose loss WIDENS with tile count (+0.6% t8 ->
;; +1.6% t32, laneFE scaling table) -- is a per-tile re-record inside a
;; RUNTIME-counted tile loop (TILE_CNT from RuntimeParams), where
;; provable_constant_trips can never resolve trips.  Two facts replace
;; the proven trip count:
;;
;;   (1) STRUCTURAL trips >= 1.  The hoisted record lands in the
;;       DEDICATED preheader of a SINGLE-BLOCK loop: the preheader's
;;       single successor is the loop body itself, so executing the
;;       record implies at least one body execution; a zero-trip entry
;;       branches around the preheader and never pays the record.
;;   (2) MONOTONE delivery delta.  benefit(t) = t*per_trip - record_once
;;       with per_trip = deliver_body - TURNAROUND: each realized trip
;;       saves the same delivered words.  Admission requires
;;       benefit(2) = 2*per_trip - record_once >= MIN_BENEFIT (the same
;;       audited margin proven trip counts must clear) and per_trip > 0;
;;       the worst realized outcome is then the single-trip exposure
;;       record_once - per_trip = PUSH + RECORD_OVERHEAD + TURNAROUND
;;       (~493 cs = about one record delivery, once per kernel entry),
;;       and every trip from 2 on wins.  Shapes whose 2-trip benefit
;;       cannot clear the margin refuse by name
;;       (record-hoist-runtime-trips-break-even): on the sdpa_reduce
;;       shape the 10-word window admits (2-trip benefit 667) and the
;;       4-word window refuses (-71) -- the refusal keeps its in-body
;;       exec-record byte-identically.
;;
;; The completion-guarded path does not reuse this exception.  It routes to
;; the shared binding-resource model, for which the structural trips>=1 fact
;; and the delivery-only two-trip proof above do not establish the selected
;; completion arithmetic.  A runtime/unknown count therefore remains
;; byte-identically unhoisted and refuses by the truthful named reason
;; record-hoist-completion-runtime-trips-unproven.  Its printed zero-trip
;; shared-model benefit is diagnostic only, never an admission proof.
;;
;; LOOP REPLAY-PRESERVATION AUDIT (same lane; rvtt-macro-epoch.cc
;; rvtt_macro_epoch_loop_replay_preserved_p).  Production tile loops
;; always carry raw LLK sync words and computed instruction-FIFO pushes,
;; so the narrow loop_preserves_replay_p scan (call/asm/typed-owner)
;; refuses every real tile loop (record-hoist-loop-opaque).  Under the
;; flag the refused loop is re-audited word by word against the
;; replay-BUFFER owner vocabulary, reusing the configuration-epoch
;; interval resolver: raw `.ttinsn' constants and volatile-stored words
;; must resolve to intervals whose opcode byte provably is not REPLAY
;; (TT_OP 0x04 -- the same audited decode as rvtt-raw-boundary.cc);
;; stores provably outside the FIFO aperture (named data objects other
;; than __instrn_buffer, stack, non-aperture constant MMIO) are inert
;; regardless of value; this pass's OWN playback launches are admitted
;; (their recorded content is the pass's audited payload -- the
;; multi-record calendar), while user-authored launches refuse (their
;; recorded slot content is unknowable and, by the lane FS persistence
;; model, may predate the kernel).  MOP dispatches admit only under the
;; MopCfg template census: the MOP Expander may legally emit REPLAY
;; words (ISA MOPExpander.md -- its performance section even recommends
;; it), so every MopCfg[0..8] word (the consumption set of BOTH
;; templates, same ISA source) must be a function-programmed constant
;; whose opcode byte is not REPLAY, all nine slots covered by stores
;; dominating the loop header (a caller-armed slot is unknowable) and
;; no call in the function; the audited scalar store-load roundtrip asm
;; idioms are admitted against the census because the MopCfg aperture
;; is WRITE-ONLY (reading it is UndefinedBehavior, same ISA source), so
;; a defined execution's roundtrip is never a MopCfg access.
;;
;; REISSUE-LATENCY GATE DISCHARGE (ordinary, unguarded record-hoist mode
;; only).  The
;; replay-reissue-latency-unproved gate serves the DEFAULT model's
;; exec-side estimate; in record-hoist mode execution cancels between
;; the worlds (word-identical streams, above), and the reissue
;; SOUNDNESS half is carried structurally: every window has >= 2
;; clones, so the identical word stream is already playback-delivered
;; at expander pace in the unhoisted world (the always-on former's
;; silicon-witnessed formation); converting the first clone from
;; exec-while-record delivery to one more playback of that same stream
;; adds no exposure an audited latency could bound.  The gate stays for
;; the default hoist model, which consumes the estimate, and on
;; unproven targets (QSR): the discharge cites the silicon-witnessed
;; playback class, which only BH/WH carry.  The completion guard also keeps
;; this gate enabled: its shared binding-resource arithmetic consumes the
;; audited interlocked execution estimate.  Audited BH/WH payloads proceed to
;; that model; an effect-opaque producer refuses by
;; replay-reissue-latency-unproved.  Thus the guard neither borrows the
;; delivery-only discharge nor silently prices an unknown execution resource.
;;
;; ADMISSION-SIDE DOOMED-HOIST MIRROR: a Dst-store payload whose record
;; would land in a preheader that itself sits inside a natural loop is
;; exactly the shape the fail-closed re-record sweep un-hoists (lane FJ,
;; noexec-rerecord-dststore-composition-unaudited) -- and the un-hoist
;; inlines every launch as a payload copy, a strict delivery
;; pessimization against never hoisting.  The record-hoist refuses that
;; shape at admission by the sweep's own name; the dominating loop-free
;; preheader Dst-store class stays admitted (the sweep's rule 3 keeps
;; it -- the witnessed init-record class).
;;
;; RECORD-HOIST x MOD-WRITE COMPOSITION (downstream-fallback pricing,
;; lane FZ, 2026-08-23; record-hoist mode only, gated on the
;; dst-autoincr pass being enabled).  The measurement pricing above is
;; LICENSED by the streams-identical premise: both worlds execute the
;; same words, so the modeled delta is pure delivery.  That premise has
;; a composition hole: a no-exec record hoisted to within the audited
;; drained-frontend window (W_drain above) of a row the dst-autoincr
;; pass would otherwise transform into a mod-write forces that pass's
;; group guard (the AUDITED COMPOSITION FACT above -- fail-closed and
;; CORRECT, the lane ES hang class) to refuse the group, so the hoisted
;; world executes the explicit-increment fallback (no-increment store +
;; TTINCRWC per row, no slot program) while the unhoisted world executes
;; the mod-write form.  Different EXECUTED streams: the execution-side
;; terms no longer cancel, and their difference -- mod-write economics
;; (config words, crossing charges, entry residual, the W_drain-fit
;; dynamic behavior) -- is priced by dst-autoincr's model in a currency
;; the delivery-only benefit above deliberately excludes.  The composed
;; delta is therefore UNPRICEABLE here, and its one silicon point is
;; NET NEGATIVE: lcm-fresh at ON-28 (record-hoist fires trips 8 words
;; 14 modeled benefit 11071; dst-autoincr falls back, sfpstore mod 6->7
;; + 1 TTINCRWC/row) measured 681.86 vs 675.85 cyc/tile against the
;; unhoisted+mod-write ON-25 world = +6.0 cyc/tile, kernel-causal
;; -0.37% -> +0.52% (headline-laneFY-plain-20260823c, device-golden
;; corr PASS; laneFZ-evidence-20260823) -- the modeled 11071-centislot
;; delivery saving was execution-hidden while the forfeited mod-write
;; fire cost real cycles.  RULE: a re-record hoist whose planned no-exec
;; capture placement would induce the fallback refuses by name
;; (record-hoist-downstream-fallback-unprofitable) and keeps today's
;; bytes.  The oracle
;; (rvtt_dst_autoincr_hoist_capture_composition_p, single source in
;; rtl-rvtt-dst-autoincr.cc next to the guard it mirrors) prices the
;; SAME audited quantity with the SAME semantics as the group guard:
;; W_drain window, candidate-block tail credited zero, intermediate
;; blocks at full frontend issue-word cover, capture-block prefix before
;; the insertion point; would-be candidates are find_candidates' own
;; EXPLICIT row shape by forward folded scan (capture shadows folded;
;; typed pure-Dst increment whose preceding non-neutral item is a
;; retargetable no-increment explicit Dst access).  Replay-row leads
;; are deliberately NOT mirrored: their candidacy needs the vetted
;; payload terminator (whole-function launch resolution), and their
;; formed groups refuse ANY same-function no-exec capture under the
;; lane FS persistence clause regardless of distance -- a coarse
;; replay-lead arm measurably over-refused (the sfpu_reduce_sdpa pack
;; TUs: launch-led increments find_candidates rejects as "no owned
;; terminator access" would have forfeited two real corpus hoists).
;; NO NEW CONSTANTS: W_drain and the word accounting are the existing
;; audited entries above.  Consistency: for every admitted hoist the guard later
;; sees the capture at >= W_drain (or unreachable), so an admitted hoist
;; can never flip a group the unhoisted world would have kept -- the
;; ON-28 corpus point of this fact is that the refusal's only fire is
;; the lcm TU (bytes revert to the reviewed ON-25 stream) with the other
;; 44 record-hoist fires byte-identical.  SCOPE BOUND (documented, zero
;; corpus instances): a replay-row-lead candidate BEYOND the window is
;; not mirrored (the guard still refuses its group soundly; the composed
;; bytes then price honestly worse) -- widening needs its own witness.
;; The counted-loop hoist branch is deliberately untouched: it exists in
;; both flag states (pin-11 class, reviewed bytes), so pricing it here
;; would churn proven streams outside this flag's measurement charter.
;;
;; FAIL-CLOSED COMPANION in the narrow scan (both flag states): a
;; volatile store whose ADDRESS is not provably outside the instruction
;; FIFO could push ANY word -- including a REPLAY record -- so
;; loop_preserves_replay_p now refuses it (volatile_store_maybe_fifo_p)
;; instead of silently ignoring it; the flag path re-audits by VALUE as
;; above (a store of a provably non-REPLAY word is benign wherever it
;; lands).  This closes a fail-open the pointer-parameter separator of
;; the original fire twin demonstrated.
;;
;; Launch-loop unroll (the post-hoist delivery companion).  A counted
;; loop whose body has been reduced to pure replay delivery -- playback
;; launches plus typed Dst steps -- still pays two delivered scalar
;; words per trip for its own control (counter step + branch) and
;; separates consecutive launches by exactly those words.  With a
;; provable trip count the loop replicates textually; the benefit,
;; trips * 2 * RISC_PUSH_X100 delivered words removed, is positive for
;; every trips >= 2, so the only gate besides the structural shape
;; proof is straight-line code size:
;;
;;   XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS bounds the total delivered
;;   words of the unrolled run (trips * per-trip delivered words).
;;   128 words = 512 bytes of straight-line launches, twice the largest
;;   currently measured winning shape (the 32-trip {launch, Dst-step}
;;   row loop = 64 words); larger runs refuse byte-identically until a
;;   silicon A/B promotes them.  A size guard, not a shape key: it
;;   depends only on the proven trip count and delivered word count.
;;
;; ORDERING RULE for the execution-saturation context term.  The
;; saturation term's LAUNCH_RUN input measures contiguous sibling
;; launches present in the loop body INDEPENDENTLY of the hoist under
;; evaluation -- context that exists in both the hoisted and unhoisted
;; worlds.  A contiguous run manufactured AFTER the decision by a
;; delivery optimization (this launch-loop unroll: counted-loop trips
;; whose per-trip launch was priced as run = 1 become one back-to-back
;; run) exists only in the hoisted world and must NOT re-price the
;; hoist: in the unhoisted counterfactual the body still re-records per
;; trip and contains no launch run at all, so there is no execution
;; shadow there for the record delivery to hide in.  Re-evaluating the
;; term against the post-unroll stream would misapply `hidden' physics:
;; the preheader record-only pass runs BEFORE the run it feeds, not
;; beside it, and on the issue plane the unrolled run's execution
;; surplus (trips * length * SLOT vs trips * PUSH delivered) shadows
;; the FOLLOWING iteration's preheader deliveries -- increasing the
;; realized hoist benefit, never decreasing it.  Decision order is
;; therefore: hoist (priced on the pre-unroll body), then unroll; the
;; unroll never feeds back.
;;
;; These constants describe replay-unit delivery economics only.  They
;; are deliberately independent of any operation identity, opcode
;; calendar, coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_REPLAY_COST_RISC_PUSH_X100   123)
  (XTT_REPLAY_COST_REPLAY_SLOT_X100 100)
  (XTT_REPLAY_HOIST_MIN_BENEFIT      60)
  (XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS 128)
  ;; Interlock-aware reissue pricing (2026-08-18 recalibration; Lane BP
  ;; diagnosis of the five causal ON regressions -- clamp/hardtanh/
  ;; softsign/hardmish/tanhderivative-lut -- laneBP-evidence-20260818/
  ;; DIAGNOSIS-AND-FIX-SPEC-laneBP.md, validation-matrix over 14
  ;; independent silicon eltwise counted-loop shapes at pin 10):
  ;;
  ;;   TURNAROUND_X100 (70)  - per-launch replay reissue turnaround.
  ;;     Provenance: residual analysis across the 14 shapes (consistent
  ;;     0.7 +/- 0.2 cyc/launch over payload lengths 4-27); the same
  ;;     class of silicon-calibrated machine constant as PUSH=123
  ;;     (itself a re-fit measured actual).
  ;;   RECORD_OVERHEAD_X100 (300) - per-record-pass engine overhead
  ;;     beyond word delivery.  Provenance: the Reduce-SDPA hoist A/B
  ;;     (silicon 21.5+ cyc/body over two 4-trip sites; measured
  ;;     per-trip record exposure ~284 cs vs the modeled
  ;;     RECORD_OVERHEAD - TURNAROUND = 230); keeps that silicon
  ;;     winner firing (+620 per site under the 2026-08-19 re-record
  ;;     derivation above) while making the five counted-loop
  ;;     refusals MORE negative (the safe direction).
  (XTT_REPLAY_COST_TURNAROUND_X100      70)
  (XTT_REPLAY_COST_RECORD_OVERHEAD_X100 300)
  ;; Replay-window loop-unroll request (-mtt-tensix-optimize-replay-
  ;; loop-unroll, lane CV 2026-08-20).  A counted single-block SFPU row
  ;; loop delivers its whole row every trip; the same row written by a
  ;; production author under `#pragma GCC unroll 8' is captured by the
  ;; always-on replay former as one execute-while-record pass plus
  ;; (factor - 1) one-word launches per group.  The pass grants the
  ;; compiler that same request from typed loop-shape facts alone.
  ;;
  ;;   LOOP_UNROLL_FACTOR (8) - the group size requested.  Provenance:
  ;;     the five measured pin-12 hand/semantic pairs whose hand arms
  ;;     are exactly this factor (sqrt/cbrt/hardsigmoid/hardshrink/
  ;;     softsign: every production eltwise row kernel in the corpus
  ;;     carries `#pragma GCC unroll 8'); the per-group record
  ;;     amortization curve (W+1+(k-1))/k flattens past k~8 while the
  ;;     re-record delivery per group stays W+1, so larger factors buy
  ;;     little and cost code size.  A cost-table machine constant of
  ;;     the same class as PUSH/TURNAROUND: no operation identity,
  ;;     opcode calendar, coefficient value, or instruction-word
  ;;     fingerprint participates.
  ;;   LOOP_UNROLL_MIN_WORDS (4) - the replay former's MIN_SEQUENCE:
  ;;     smaller rows cannot form a window, so unrolling them is pure
  ;;     code growth.
  ;;   LOOP_UNROLL_MAX_WORDS (256) - code-size bound on factor times
  ;;     the estimated row words (the launch-unroll analog of
  ;;     XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS).
  (XTT_REPLAY_LOOP_UNROLL_FACTOR      8)
  (XTT_REPLAY_LOOP_UNROLL_MIN_WORDS   4)
  (XTT_REPLAY_LOOP_UNROLL_MAX_WORDS 256)
  ;; Round-chain interleave request (-mtt-tensix-optimize-round-
  ;; interleave, lane EI 2026-08-21).  A counted round loop whose
  ;; iterations are independent by dataflow (every loop-carried value's
  ;; recurrence is at most a single word-delivering update) is asked to
  ;; unroll by TWO so the post-RA cyclic list scheduler can interleave
  ;; the two copies' chains -- the dual-Horner shape, one iteration
  ;; ahead.
  ;;
  ;;   ROUND_INTERLEAVE_FACTOR (2) - two-ahead is the minimal depth
  ;;     that creates cross-copy slack, and each further copy multiplies
  ;;     the live-range bound toward the hard 8-LREG file (the same
  ;;     reason modulo variable expansion is refused outright on this
  ;;     target); deeper factors are not priced and not requested.
  ;;   ROUND_INTERLEAVE_MIN_WORDS (2) - the doubled body must present
  ;;     at least the scheduler's three-node interleave minimum
  ;;     (2 * 2 = 4 > 3); a one-word body has nothing to interleave.
  ;;   ROUND_INTERLEAVE_MAX_WORDS (128) - code-size bound on the
  ;;     DOUBLED body (2 * estimated row words), half the
  ;;     LOOP_UNROLL_MAX_WORDS budget class since the factor is fixed
  ;;     at two.
  (XTT_ROUND_INTERLEAVE_FACTOR      2)
  (XTT_ROUND_INTERLEAVE_MIN_WORDS   2)
  (XTT_ROUND_INTERLEAVE_MAX_WORDS 128)
])

;; ---------------------------------------------------------------------
;; Delivery-shape arbitration (-mtt-tensix-optimize-delivery-shape,
;; lane EG 2026-08-21).  Additive section; every model above is
;; unchanged and its constants are reused read-only.
;;
;; One solver, one model: per proven-trip counted single-block SFPU row
;; loop, gimple-rvtt-delivery-shape.cc enumerates the whole discrete
;; shape lattice {unroll factor U} x {payload rows R}, PREDICTS the
;; shape the downstream machinery materializes for each U by mirroring
;; the replay former's grouping and the replay-hoist gate's model above
;; (with the DOWNSTREAM constants -- prediction, never re-pricing),
;; prices each predicted shape with the MEASURED delivery table below,
;; and requests the exact argmin through the same loop->unroll
;; annotation as the fixed-factor request pass (which never overrides
;; an existing annotation; an affirmative rolled selection annotates
;; factor 1 to own the slot).  Exact solver: rvtt_bnb_delivery_shape
;; (rvtt-bnb.cc), deterministic branch-and-bound with an admissible
;; prune over the (tiny) lattice.
;;
;; MEASURED DELIVERY TABLE.  Provenance: lane EE loser-anatomy closure
;; (laneEE-evidence-20260821/LOSER-ANATOMY.md) -- fourteen silicon rows
;; (pin-15 headline + pin-13 storm cells) reproduced within ~3% from
;; instruction-class censuses of the timed ELFs:
;;
;;   WORD_X100 (100)        - one delivered word (RISC-pushed SFPU word
;;     or scalar control word) on an issue-bound leg measures ~1.0
;;     cycle (threshold sem 355 w -> 350.8 c; hardshrink sem 365 ->
;;     357.8; log hand 576 -> 577.9; ceil hand 512 -> 477.9).  The
;;     ceil-hand point shows scalar pairs partially dual-issue folding
;;     (<= 7% closure slack, the documented residual of this table);
;;     the model prices all words at 1.0 and carries the slack as
;;     model error, never as a fitted constant.  Note this table
;;     deliberately does NOT reuse the RISC_PUSH_X100 = 123 premium:
;;     that constant was re-fit on launch-conversion shapes and is the
;;     downstream hoist gate's own term (mirrored for prediction);
;;     EE's issue-bound closure on these row classes measures 1.0.
;;   BOUNDARY_{LB,UB}_X100 (130, 180) - measured per-launch boundary
;;     cost on serial-chain replay windows, where the payload's
;;     dependence chain leaves no across-launch overlap and every
;;     reissue boundary is exposed (EE rows 3/11/13: ceil 31 launches
;;     ~= 28-43 c; log 31 launches + record ~= +49 c; rsqrt 30
;;     launches ~= +45 c).  Carried as an INTERVAL: every candidate is
;;     priced at both ends and a non-rolled request must clear the
;;     benefit threshold at both -- no averaging, no per-shape fit.
;;     The relation to TURNAROUND_X100 (70): that constant averages
;;     over the 14-shape recalibration set where boundaries partially
;;     overlap; the EE rows isolate the exposed serial-chain case.
;;   record pass = (1 + payload_slots) delivered words (EE machine-
;;     model fact 1: an n-slot record costs n+1 issue words), priced
;;     at WORD_X100 and ADDITIVE to the leg's execution: the ceil
;;     closure books exec 448 + record 15 + boundaries -- record
;;     delivery measures exposed, "issue words that never retire as
;;     work" (EE row 3), so the measured table charges it in full.
;;     Loop-control delivery on a replay leg measures HIDDEN under the
;;     execution backlog (none of the EE replay closures carries a
;;     control term), while an issue-bound leg pays its control words
;;     at WORD_X100.
;;   execution = payload slots at 1.0 cycle each, plus one slot per
;;     audited next-slot acceptance stall (SFPSWAP family).  The
;;     mad-family latency-1 stalls measure as ABSORBED on every
;;     chain-heavy anatomy row (EE: ceil/sqrt/rsqrt/lcm exec ==
;;     slots), so no per-member stall is charged; a row member with
;;     no audited latency fact at all makes the term unpriceable and
;;     the loop refuses by name (delivery-shape-exec-term-unaudited).
;;
;; GIMPLE LATENCY MIRROR page audits (2026-08-21, lane EG; the audited
;; latency-0 page convention of the D3 audit above, applied at the
;; pre-expansion census where the RTL attributes are not yet
;; readable):
;;   CC family (SFPSETCC/SFPENCC/SFPCOMPC/SFPPUSHC/SFPPOPC): no
;;     next-cycle rule on either architecture's page
;;     (tt-isa-documentation BlackholeA0 + WormholeB0
;;     TensixCoprocessor); lane-flag state consumed by the next
;;     instruction by construction of every silicon-passing kernel.
;;   SFPARECIP (BH-only): "simple sub-unit", functional model writes
;;     LReg[VD] at issue, no next-cycle rule (SFPARECIP.md); the
;;     silicon-measured fresh_recip_hwseed bodies (lane DJ, addcdiv
;;     -25.3%) issue SFPARECIP -> SFPMAD back-to-back.
;;   SFPSETEXP/SFPSETMAN/SFPSETSGN immediate arms: the pages carry no
;;     next-cycle rule on either architecture and the immediate arms
;;     share the register forms' functional model (the D3 simple-unit
;;     row listed only the register forms as an audit-scope choice,
;;     not a hardware distinction); latency 0 in the gimple mirror.
;;   Structured float compares (sfpxfcmps/sfpxfcmpv): the lowered
;;     compare-vs-operand executes as a mad-family member in the final
;;     stream (lane EE anatomy; the recorded pin-13 hoist refusal
;;     arithmetic on the hardshrink body requires exec_ilk = words + 1)
;;     -- charged one slot in the downstream-mirror exec, absorbed in
;;     the measured exec like the rest of the mad family.  Integer
;;     compares lower to the audited iadd class, latency 0.
;;   Class-level envelope caveat: per-mod refinements (shft variable
;;     mods, iadd mods, cast mods) are enforced by the RTL consumers;
;;     a mis-refined mod in this mirror can only shift the modeled
;;     delta, never semantics (the transform is an unroll request;
;;     bit-exactness is CRAQ-gated independently of every cost term).
;;
;; MODEL SEAMS (stubbed to current-model values, documented):
;;   - the dst-autoincr crossing price (lane EB's DX-F2 term, corrected
;;     to the W_drain covering walk above by lane EQ / EP-F1, LANDED at
;;     pins 16/17) is NOT yet joined into this mirror: the solver models
;;     no autoincr setup cost (value 0) and consumes only the enable bit
;;     for the mirror's saturation run term.  Joining the W_drain term
;;     into this mirror is the named follow-up (FH audit FHI-1/FHS-11).
;;   - lane EC's record-hoist scope widening (DX-F3) LANDED at pin 16;
;;     its wider hoist scope is NOT YET JOINED into this mirror -- the
;;     downstream mirror still models only the pre-EC hoist branches.
;;     The join is the named follow-up (same owner as the term above).
;;   - rolled-override seam: where the modeled winner is the explicit
;;     ROLLED shape but the downstream hoist's own gate is predicted
;;     to window the loop anyway (the ceil-fresh class: straight-push
;;     478 c measured vs hoisted window 521 c), this pass has no
;;     channel to suppress that pass; the disagreement is dumped by
;;     name (delivery-shape-downstream-override-required) as the
;;     wiring seam for the pricing-consumer follow-up.
;;
;; CAPTURE_SLOTS (32) - the replay buffer's 32 entries (the same bound
;; the S+L <= 32 MOP/replay co-ownership invariant divides,
;; rvtt-mop-tables.h); no payload may exceed it.
;;
;; MIN_BENEFIT (60) mirrors the replay-hoist threshold's rationale
;; verbatim: refusal (staying rolled) is byte-identical code and costs
;; nothing, and no silicon point yet anchors this solver's own
;; acceptance region -- the interval discipline (boundary ends) is the
;; buffer.  -mtt-tensix-delivery-shape-min-benefit= (same centislot
;; units) overrides it for experimentation and A/B legs.
;;
;; These constants describe measured delivery economics only.  They
;; are deliberately independent of any operation identity, opcode
;; calendar, coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_DELIVERY_WORD_X100           100)
  (XTT_DELIVERY_BOUNDARY_LB_X100    130)
  (XTT_DELIVERY_BOUNDARY_UB_X100    180)
  (XTT_DELIVERY_CAPTURE_SLOTS        32)
  (XTT_DELIVERY_SHAPE_MIN_BENEFIT    60)
])


;; ---------------------------------------------------------------------
;; MOP loop-delivery formation (rvtt_mop_form).  Additive section; the
;; replay-hoist model above is unchanged and its constants are reused.
;;
;; The MOP expander is a third delivery tier under the replay buffer:
;;
;;   RISC push      one delivered word per instruction  (PUSH = 123)
;;   REPLAY launch  one delivered word per PAYLOAD      (payload slots
;;                                                       reissued at
;;                                                       SLOT = 100)
;;   MOP loop       one delivered word per LOOP: the template's launch
;;                  word is reissued loop_count+1 times by the frontend
;;                  with no RISC involvement per iteration.
;;
;; Pricing follows the corrected concurrent-delivery accounting (the
;; exp-parity re-basing, NOTES-exp-parity-laneR2.md): during replay
;; playback, RISC delivery is CONCURRENT with execution -- per-row time
;; is max(exec, 1.23 x delivered words), NOT their sum -- proven by the
;; sigmoidappx pure-delivery control (64 delivered loop-control words
;; removed, measured +0.006 units = noise) and the exp pre-Z -> Z
;; increment (-33.0 RAW measured against -32 modeled from the execution
;; side alone).  Under that accounting a replay-launch row prices
;;
;;   before_row = max ((len+k) * SLOT, d * PUSH) ; d delivered words/row:
;;                                             ; 1+k for a straight-line
;;                                             ; run of [launch, k typed
;;                                             ; SETRWC step words], 3
;;                                             ; for a counted launch
;;                                             ; loop (launch + 2
;;                                             ; control); k step words
;;                                             ; ride in the template's
;;                                             ; flags&2 slots and
;;                                             ; execute either way
;;   after_row  = (len+k) * SLOT               ; MOP delivers nothing
;;
;; so MOP formation relieves ONLY delivery-bound rows:
;;
;;   benefit = N * max (0, d * PUSH - (len+k) * SLOT) - config_words * PUSH
;;
;; where the configuration block is serial delivery bought at full
;; price (it precedes the loop it feeds; there is no execution shadow
;; for it to hide in -- same ordering physics as the hoist's
;; record-only preheader pass):
;;
;;   config_words = mop_sync (lui+sw, the production reprogramming
;;                  guard) + config-base lui + one MMIO store per
;;                  programmed template register (flags, the launch
;;                  word, and any flags&2 step/NOP slots; li 1..2 per
;;                  distinct nonzero value, zero from x0) + MOP_CFG +
;;                  MOP = 8..9 words for the bare launch class, up to
;;                  ~16 with three step slots; computed exactly per
;;                  candidate.
;;
;; Consequences the model asserts (falsifiable, no silicon yet for this
;; tier):
;;   - execution-bound rows (len >= 2 in a straight-line run) model
;;     <= 0 and refuse byte-identically: their launch pushes were
;;     already free under the corrected accounting.  This is exactly
;;     the MOP-tier correction the TOP3-3 design doc names as the
;;     falsification arm of its 1.23:1 additive prediction; a silicon
;;     A/B on the minmax gate adjudicates between them (the testing-only
;;     -mtt-tensix-mop-form-force flag exists to build that leg, since
;;     no non-negative threshold admits a negative modeled benefit).
;;   - the winning class is delivery-bound rows: single-slot payload
;;     runs (len 1: 23 centislots/row, fires at N >= ~51) and counted
;;     launch loops whose control words dominate the row (d = 3:
;;     len 1 fires at N >= ~5, len 2 at N >= ~7, len 3 at N >= ~17).
;;
;; MIN_BENEFIT mirrors the replay-hoist threshold (60 centislots =
;; 0.6 slot per formation): refusal is byte-identical code and costs
;; nothing, and no silicon point yet anchors this tier's acceptance
;; region.  -mtt-tensix-mop-form-min-benefit= (same centislot units)
;; overrides it for experimentation and for building silicon A/B legs.
;;
;; Outward ownership (2026-08-17 silicon adjudication follow-up): the
;; formed template lives in thread-shared registers that survive the
;; function's return, so formation additionally requires the outward
;; ownership proof (rtl-rvtt-mop-form.cc; refusal
;; mop-caller-template-live-unproven).  The measured consequence that
;; forced this: the minmax perf harness -- caller programs a type-1
;; template once, then per tile launches it and calls the formed
;; callee -- hangs the Tensix deterministically (the caller's next
;; launch expands the callee's template).  A save/restore epilogue is
;; NOT a priceable alternative tier: the MOP config registers are
;; write-only from the RISC (rvtt-mop-tables.h readback fact), so the
;; snapshot cannot be constructed at any price.  Were the registers
;; readable, the epilogue would add ~9 reads + ~11 delivered rewrite
;; words of serial delivery per formed region -- more than the entire
;; config block the model already charges -- which alone would erase
;; every delivery-bound win currently priced above threshold; the
;; refusal therefore costs the model nothing it was winning.  The
;; profitability constants above are unchanged: ownership is a
;; structural proof, not a priced term.
;;
;; These constants describe MOP delivery economics only.  They are
;; deliberately independent of any operation identity, opcode calendar,
;; coefficient value, or instruction-word fingerprint.  MOP capability
;; facts (encodings, register file, S+L <= 32 replay co-ownership) live
;; in rvtt-mop-tables.h with per-fact provenance.
(define_constants [
  (XTT_MOP_FORM_MIN_BENEFIT 60)
])

(define_automaton "rvtt_tensix")
(define_cpu_unit "rvtt_math,rvtt_sfpu,rvtt_tdma,rvtt_cfg,rvtt_sync"
  "rvtt_tensix")

;; These are top-level issue slots, one per class and cycle.  They do not
;; claim to model cross-TRISC wait gates or bank-valid dependencies.
(define_insn_reservation "rvtt_issue_math" 1
  (eq_attr "xtt_issue" "math") "rvtt_math")
(define_insn_reservation "rvtt_issue_sfpu" 1
  (eq_attr "xtt_issue" "sfpu") "rvtt_sfpu")
(define_insn_reservation "rvtt_issue_tdma" 1
  (eq_attr "xtt_issue" "tdma") "rvtt_tdma")
(define_insn_reservation "rvtt_issue_cfg" 2
  (eq_attr "xtt_issue" "cfg") "rvtt_cfg")
(define_insn_reservation "rvtt_issue_sync" 1
  (eq_attr "xtt_issue" "sync") "rvtt_sync")

;; -----------------------------------------------------------------------
;; Constant residency and rematerialization pricing (lane BS,
;; gimple-rvtt-prgm-const.cc const-residency / const-remat phases).
;;
;; Both mechanisms move SFPLOADI materializations, so they price in the
;; same delivered-word units as the replay model above (RISC_PUSH_X100).
;;
;;   materialize (fp32)   = 2 pushed words  (SFPLOADI lo + hi pair; the
;;                          16-bit-encodable forms are 1 word -- the
;;                          conservative bound prices 2)
;;   PRGM programming     = 3 pushed words once (2 staging SFPLOADI +
;;                          1 SFPCONFIG; the staging register model is
;;                          NOTES-exp-parity-laneR2.md D1)
;;   PRGM read-back       = 0 allocatable-LREG pressure and 0 delivered
;;                          words (constant-register operand folded into
;;                          the consumer by the unspec propagation)
;;
;; Residency of an IN-LOOP invariant materialization therefore pays for
;; itself when 2 * trips >= 3, i.e. at two proven trips -- the LOOP
;; class requires the structural first-iteration exit-test proof of a
;; second trip and refuses otherwise (trip-count-unproven).  An
;; OUT-OF-LOOP materialization executes once either way, so parking it
;; buys nothing in delivered words; its value is one freed LREG, and the
;; PRESSURE class fires only while the liveness model exceeds the
;; eight-LREG file.  Rematerialization re-issues the materialization per
;; consumer: 2 pushed words per rematerialized use (~2.46
;; slot-equivalents outside launch runs at RISC_PUSH_X100; free inside a
;; run per the concurrent-delivery correction recorded above), which is
;; why the allocator ranks residency (free reads) above remat and remat
;; fires only under residual over-pressure -- where the alternative is
;; not slower code but NO code (the spill diagnosis error).
;;
;; Residency-peel extension (lane CF, CC-canonical loop bodies): when
;; the loop body carries a lowered v_if region ending in the all-lanes
;; SFPENCC, the programming point is created by peeling iteration one
;; onto the entry edge (the lane-state proof: the programming executes
;; after the peeled copy's own all-lanes SFPENCC; iterations 2..N begin
;; in that same state).  The peel changes iteration one's delivery
;; class -- its body words are RISC-pushed instead of replayed -- so the
;; model charges, in the same centislot units:
;;
;;   save  = SLOT * candidate_words        per iteration after the first
;;   cost  = PUSH * (candidate_words + n_SFPCONFIG)          ; programming
;;         + (PUSH - SLOT) * body_words                      ; peel class
;;                                                           ; change
;;   fire  when (trips - 1) * save >= cost, with trips proven by bounded
;;   forward evaluation of the loop's own scalar control (never profile
;;   data); the refusal is peel-trip-count-unproven.
;;
;; body_words is a delivered-word proxy over the gimple body (one word
;; per typed RVTT call or audited raw word, the immediate-encoding word
;; count for materializations, zero for scalar statements -- scalar
;; control is RISC-side and concurrent per the delivery accounting
;; above).  Overestimating body_words only raises the required trip
;; proof; it never admits an unpriced fire.
;;
;; MAD-PAIR extension (lane GA, FX-F1): a constant the invariant pass
;; has already HOISTED out of the loop executes once either way, so
;; the LOOP-class per-iteration saving is zero -- but when the hoisted
;; constant is the shortened SFPLOADI FLOATB form feeding one half of
;; a single-use mul+add pair inside the loop, the downstream muli/addi
;; immediate folds (which run "in preference to mul,add->mad" on the
;; local-pressure rationale) consume it and the mad rule can no longer
;; fuse: the loop body pays MUL+ADDI (2 words) instead of MAD (1 word)
;; every iteration.  Re-claiming exactly those fold-vulnerable
;; constants into PRGM registers removes the folds' SFPLOADI match and
;; re-offers the pair to the unchanged mad combine:
;;
;;   cost  = PUSH * 1 per claim once   (the SFPCONFIG; the staging
;;                                      materialization replaces the
;;                                      hoisted one word for word)
;;   save  = PUSH-class 1 word per iteration (MAD for MUL+ADDI)
;;
;; A proven single trip is a wash and refuses (trip-count-single-trip);
;; runtime trips admit under the W2 policy (the in-place programming
;; point is the hoist's own execution point -- never speculated).
;; Placement is pair-atomic (all-or-none per pair): a half-claimed pair
;; pays its programming word while the surviving immediate fold still
;; blocks the mad rule -- a pure loss (madpair-prgm-exhausted).
;; Non-vulnerable sfpxloadi chain operands are never claimed: the folds
;; cannot match them and the mad rule fuses them from plain LREGs.

;; ---------------------------------------------------------------------
;; Dual-bank pinned-chain binding caps (lane FU, rtl-rvtt-lp-alloc.cc
;; layer 3).  These are STRUCTURAL bounds, not pricing constants: they
;; cap the shapes and the search the binding layer will model, and an
;; exceeded cap is a named refusal that keeps today's allocation
;; byte-identically (fail-closed to today's behavior, which includes
;; today's lreg-pressure-exceeded error where LRA cannot repair).
;;
;;   LPA_PIN_MAX_OPS = 24        largest pinning pattern operand count.
;;                               Evidence: rvtt_sfptransp8_int has 16
;;                               operands (8 exact-pinned outputs + 8
;;                               matching inputs), the largest exact-
;;                               register pattern in rvtt.md; 24 leaves
;;                               headroom for a wider future quartet
;;                               family without admitting unbounded
;;                               shapes (refusal:
;;                               dualbank-pin-shape-unmodeled).
;;
;;   LPA_PIN_MAX_ALTS = 16       largest alternative count on a pinning
;;                               pattern.  Evidence: the relational
;;                               rvtt_sfpswap_indexed_int carries 12
;;                               alternatives (the twelve legal ordered
;;                               value pairs under companion == value +
;;                               4); every other pinning pattern is
;;                               single-alternative (same refusal).
;;
;;   LPA_PIN_SEARCH_BUDGET = 4096  DFS alternative applications before
;;                               the search refuses
;;                               (dualbank-search-budget-exceeded).
;;                               Evidence: the eager forced-color
;;                               consistency pruning collapses anchored
;;                               chains to near-linear search -- the
;;                               lane-EX generic_moe_gate_topk top16
;;                               kernel, the largest known pinned-chain
;;                               body (1744 webs, 447 pin sites, 186
;;                               relational), solves with budget 1336;
;;                               the distilled dg fire tests use < 32.
;;                               4096 covers a 3x growth in relational
;;                               sites at the measured consumption rate
;;                               while bounding the worst (infeasible-
;;                               instance exhaustion) case.
;;
;; WINDOW-PAIRING INTER-ROW DRAIN MODEL (lane FT; consumed by
;; rvtt_macro_interrow_drain_tuned in rtl-rvtt-schedule.cc under
;; -mtt-tensix-optimize-window-pairing).  The lane-EV inter-row
;; obligation (P0 adjudication 2026-08-21) placed the FULL derived drain
;; between consecutive rows whenever any launch is a fixed-VD VALUE
;; carrier -- a register-blind shape rule.  The tuner derives the
;; minimal spacing from the SAME architectural facts as the boundary and
;; backedge drain proofs (L1-L3/E1-E4/H1-H2, the retire-before-issue
;; transactional model whose provenance is the derived-calendar table's:
;; ISA spec + CRAQ generic executor + hand MulInt32), made exact by
;; per-event footprints.  Audited facts specific to this model:
;;
;;   F1  Hosted-event operand overrides.  For Simple/MAD/Round events
;;       the launch VD joins the reads (Insn.VB or Insn.VC := VD) and
;;       the result register is the launch VD or LReg[16] per the
;;       SequenceBits VD16 bit; the scheduled store's value register is
;;       LReg[16] (0x40), the template's own VD (0x80), or the launch VD.
;;       [ISA] BlackholeA0 SFPLOADMACRO.md functional model (the
;;       override block); identical on WH for the fields used.
;;   F2  Dst physical-row footprint.  An access at constant address A
;;       touches lane rows [A & ~3, (A & ~3) + 3]; the 32-bit format
;;       class (mod0 3/4/7/9/12) maps lane row r to physical rows
;;       adj32(r) and adj32(r)+8 with adj32(r) = ((r & 0x1F8) << 1)
;;       | (r & 0x207); config-resolved classes (mod0 0) take the union
;;       of both layouts.  Same audit as
;;       gimple-rvtt-transp-involution.cc access_rows and the
;;       rtl-rvtt-lp-alloc.cc dst32b aliasing window.  [ISA] SFPLOAD.md
;;       "Row = (Addr & ~3) + (Lane / 8)"; Dst.md Dst32b/Dst16b storage
;;       model.
;;   F3  Column parity.  Both functional models compute
;;       Column = (Lane & 7) * 2, then += 1 when
;;       ((Addr & 2) || DEST_{RD,WR}_COL_EXCHANGE) -- so two accesses
;;       whose addresses differ in bit 1 touch DISJOINT DstBits columns
;;       (the column index is preserved by both the 16-bit and the
;;       32-bit view) PROVIDED the column-exchange LaneConfig bits hold
;;       their architectural default.  [ISA] SFPLOAD.md and SFPSTORE.md
;;       lane loops; Dst.md view-to-storage mapping.  The default-state
;;       proof discipline and the ambient platform contract are the
;;       DSATUR spill machinery's (rtl-rvtt-lp-alloc.cc,
;;       lreg-spill-laneconfig-unproven): any function-local writer that
;;       could reach SFPCONFIG destination 15 refuses the clause.
;;   F4  Mod0 10 (INT32_ALL) refuses the typed-address model: it adds
;;       the Sp counter into the address AND decrements Sp, breaking the
;;       shared-RWC-base distance arithmetic.  [ISA] SFPLOAD.md
;;       MOD0_FMT_INT32_ALL arm.  Same exclusion as the spill machinery.
;;   F5  Row-to-row distance = the schedule's absorbed typed stride,
;;       valid because the absorbing access holds the row's LAST issue
;;       slot (checked; the compact-absorber invariant) and
;;       SFPLOAD/SFPSTORE resolve their own address BEFORE
;;       ApplyPartialAddrMod runs.  [ISA] SFPLOAD.md functional-model
;;       order.  Pending stores latch their Dst row at launch (L1), so
;;       follower counter advances never move them.
;;   F6  Same-cycle staged events on DISTINCT sub-units with disjoint
;;       data are admitted: the Vector Unit executes one instruction per
;;       sub-unit column per cycle by design ([ISA] SFPLOADMACRO.md,
;;       "up to five instructions per cycle"), and the in-row scheduler
;;       already relies on exactly this (core_check_subunit_occupancy
;;       admits same-slot different-sub-unit events).  Same-cycle SAME
;;       sub-unit, and any data-overlapping pair whose order the cycle
;;       arithmetic does not preserve, refuse -- the
;;       cc-restore-store-race failure mode stays refused through the CC
;;       clause (a pending store reads the live lane mask; any follower
;;       CC write inside the horizon conflicts).
;;
;;   F5' Stride-phase rebase (lane GJ; under
;;       -mtt-tensix-optimize-window-pairing-stride).  F5's compact-
;;       absorber invariant (the advancing address mode rides the row's
;;       LAST issued word) is one sufficient condition for the uniform
;;       "row r+j sits exactly j strides away" arithmetic, not a
;;       hardware requirement.  When the single absorbing advance rides
;;       an EARLIER issued word (position p), the row's Dst accesses
;;       split into two phases by the POSITION of their carrying word:
;;       accesses carried at or before p resolved at the row-entry
;;       counter value (phase 0), accesses carried after p resolved one
;;       stride later (phase 1).  This is exact because every access's
;;       Dst address latches at its carrying word: the absorbing word's
;;       own load resolves before ApplyPartialAddrMod runs ([ISA]
;;       SFPLOAD.md functional-model order, = F5), and every
;;       SFPLOADMACRO-hosted event's Dst row is computed AT LAUNCH and
;;       never re-resolved ([ISA] BlackholeA0 SFPLOADMACRO.md StoreSubUnit
;;       extras: "the computation in SFPSTORE will resolve to whatever
;;       was computed in SFPLOADMACRO, regardless of whether SFPLOADMACRO
;;       (or any other intermediate instruction) advanced any RWCs";
;;       [SIM] craq-sim 9f324140 src/tensix.cpp macro_dst_row latched
;;       from dst_rwc before the SFPLOAD dispatch applies the modifier,
;;       = the L1 fact the boundary proofs already consume).  Rebasing
;;       every footprint by phase*stride (pending events: phase*stride;
;;       follower row j: (j+phase)*stride) restores the uniform distance
;;       arithmetic: same-index events across rows keep relative shift
;;       j*stride, cross-index pairs gain exactly the phase delta their
;;       carrying positions imply.  With the absorber on the last word
;;       every phase is 0 and the arithmetic is F5's verbatim (the
;;       admission is flag-gated; flag-off keeps the compact-form
;;       refusal byte-identically).  An event with no provable carrying
;;       word refuses window-pairing-stride-unproven (fail closed).  No
;;       new constant: the phase is 0 or 1 by position comparison.
;;
;; No constant is introduced: every distance derives from the schedule's
;; transcribed delays cross-checked against the descriptor's own
;; SequenceBits (two derivations of one calendar; mismatch refuses
;; window-pairing-delay-unproven).  Frozen whole-word programs leave
;; DELAY_UNKNOWN in the schedule and therefore refuse by name -- the
;; signbit family keeps its proven rolled calendar byte-identically.
