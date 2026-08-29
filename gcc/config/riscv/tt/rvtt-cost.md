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
;; NOT transfer) -- this stance is unchanged by the laneGU FP16
;; six-entry SELECTION rows (rvtt-lut-tables.cc mod0 2/3/6/7,
;; 2026-08-25): those are capability rows for the instruction-
;; selection pass only; the emitted rvtt_sfplutfp32_6r insn keeps
;; every refusing cost/effect default exactly as the mod0 0/4
;; formations always have; SFPLUT INDIRECT_VD (dynamic write target); the
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
;; PER-EXECUTION CONFIGURATION PRICING (lane IA, pin 35).  The
;; profitability comparison is stated in frontend issue slots PER
;; EXECUTION OF THE CONFIGURATION PROGRAM on both sides, and the cost
;; model splits by PLACEMENT -- lane IA silicon bracketed BOTH
;; directions (laneIA-evidence-20260827, 3-rep cycle-identical):
;;
;;   - a PREHEADER program executes once per loop entry inside the
;;     same pre-steady-state window the once-per-entry drain residual
;;     already prices: the lane EP covered witnesses measured that
;;     whole entry window, three-word program INCLUDED, at ~2
;;     cycles/entry -- so preheader words price at their word count,
;;     plus the residual through a live crossing (the original
;;     pricing, unchanged).  The refuting witnesses for charging more
;;     there: the lcm-fresh row loop (preheader 8x1-row group,
;;     692423 -> 694979 = +0.37% refused) and the relu hand rolled
;;     loop (45744 -> 49330 = +7.8% refused) -- both measured BETTER
;;     fired, so the configuration class occupancy must not be
;;     double-counted against the entry residual;
;;
;;   - a NON-PREHEADER program re-executes on every execution of its
;;     region, whose scalar entry control (call or branch) drains the
;;     frontend the configuration then consumes: each SETC16 word
;;     occupies the configuration issue class, an audited TWO-cycle
;;     resource (rvtt_issue_cfg below; craq-sim
;;     tensix_rtl_issue_class_for_inst models the same), while each
;;     removed TTINCRWC frees a single-cycle slot, and the program
;;     pays the once-per-entry drain residual (min_config_distance)
;;     on every execution.  Charging the residual to a mid-block
;;     program whose block carries prior covering words is
;;     conservative in the refusing direction only.
;;
;;   - groups whose candidates share a rewritten capture payload are
;;     priced as one FAMILY: payload coverage forces all-or-nothing
;;     across the capture's execution sites, so the two real
;;     alternatives are the whole family transformed or kept -- an
;;     orphan split-off group priced alone would poison a paying
;;     sibling stream (the rdiv/sqrt/cbrt hand kernels' 32-launch
;;     streams split 8+24: 32 removed vs two programs' slots pays).
;;
;; Silicon witness for the non-preheader term (binopscalar-fresh,
;; pin 35): the eight-row straight-line callee
;; (8x{SFPLOAD,SFPADDI,SFPSTORE}) re-invoked 512 times per kernel
;; fired under the old 3-words-vs-8-rows admission and measured
;; KERNEL 21929 vs 21164 OFF (+3.61%, TILE_LOOP 168.95 vs 162.95 =
;; ~1.5 cycles per invocation, delta = exactly 3 SETC16 + 8 mod-write
;; retargets - 8 TTINCRWC per call).  Per-execution slot pricing
;; refuses it at removed 8 <= 3*2 + 2 = cost 8 (unprofitable-group),
;; returning the OFF bytes; the measured ~9.5 effective slots bracket
;; the audited 8 from above, so the break-even remains a conservative
;; floor.  Every prior witness verdict is preserved: the
;; trips-amortized preheader winners (unaryshift hand 4x8 rows,
;; threshold/hardshrink 32 removed/entry vs preheader cost 5, and the
;; lcm/relu preheader 8x1 groups above) and the skinny rolled losers
;; (crossing-refused upstream) are unchanged.
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
;; CROSS-CALL ADDR_MOD CONTRACT PRICING (lane IK,
;; -mtt-tensix-optimize-crosscall-addrmod).  A straight-line callee
;; whose groups ALL refuse by the non-preheader per-execution term may
;; have its slot program hoisted, ONCE, into the proven caller's loop
;; entry (gimple-rvtt-crosscall.cc addrmod service: the lane CA
;; init-hoist scan at every lane HC residency-walk level, plus the TU
;; MOP template audit and the Wormhole ADDR_MOD_SET_Base watch row).
;; The hoisted program is PREHEADER-CLASS by construction -- it
;; executes once per caller-loop entry inside the same entry window
;; the drain residual already prices (the lane EP covered-witness
;; measurement above) -- so the callee's groups price at ZERO
;; configuration slots per call and the comparison becomes removed
;; rows against the CALL-BOUNDARY crossing charge alone: the block-
;; final live mod-write's next consumer is the NEXT invocation's first
;; Dst access, reached through frontend-draining scalar return/call
;; control -- the same audited W_drain the loop-backedge term charges.
;; Cover credits only the callee's own frontend words after the final
;; increment (caller-side words credited ZERO -- the refusing
;; direction); rows <= charge refuses by name
;; (mod-write-dominates-crosscall-body).  Soundness of the zero term
;; is the ISA-adjudicated slot-clobber census (tt-isa-documentation:
;; ThreadConfig ADDR_MOD rows are per-thread, writable ONLY by
;; same-thread SETC16 -- WRCFG/CFGSHIFTMASK/RMWCIB functional models
;; each exclude ThreadConfig), instantiated fail-closed over the whole
;; callee, the caller epoch at every lifted level, and the TU template
;; slots; any possible owned-row or watched-row write refuses -- there
;; is no per-call re-establishment to fall back to.  Named refusals:
;; crosscall-addrmod-unproven (callee shape / service),
;; mod-write-dominates-crosscall-body (boundary charge),
;; crosscall-addrmod-owned-row-write, crosscall-addrmod-loop-unproven,
;; crosscall-addrmod-callers-unproven,
;; crosscall-addrmod-preheader-occupied.
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
;; EXEC-WHILE-RECORD FIRST-TRIP PEEL (lane GQ, 2026-08-25;
;; -mtt-tensix-optimize-record-hoist-peel, Init(0), composing on
;; record-hoist mode only).  Rescues exactly the ADMISSION-SIDE
;; DOOMED-HOIST MIRROR refusal above (Dst-store payload, preheader
;; inside an outer loop -- the recip-fresh face-loop shape: 4 in-body
;; exec-record sites per tile, ON-28 dump witness
;; noexec-rerecord-dststore-composition-unaudited).  SOUNDNESS: the
;; mirror's hazard class is keyed to a capture that is STILL NO-EXEC at
;; end of pass (the sweep's own skip: exec-converted captures are the
;; fleet-witnessed class -- minmax/sdpa/where/typecast/lcm ON-set all
;; re-record exec-while-record per trip with sibling launches between
;; re-ingestions; the dst-autoincr group guard's refuted composition is
;; likewise keyed TTREPLAY load=1 exec=0 in
;; noexec_record_composition_p).  The peel therefore never forms the
;; refused shape: the loop's ENTIRE proven first trip (capture flipped
;; to exec-while-record + payload + its sibling launches) moves
;; verbatim to the dedicated preheader, every former in-body record
;; site becomes one playback launch, and the proven-constant counter
;; re-initializes one step later (trips -> trips-1; a single-bb loop is
;; do-while, so proven trips >= 2 is an admission bound, not pricing).
;; The EXECUTED effect stream is unchanged (a launch delivers exactly
;; the recorded words); the delivered stream drops (trips-1) per-trip
;; record passes -- the same quantity the record-hoist measurement
;; pricing above models -- so admission reuses hoist_profitable_p
;; unchanged: the peel's true benefit is bounded below by the modeled
;; no-exec hoist benefit minus nothing (the peeled preheader pass
;; executes trip-1 work that was paid in both worlds, where the modeled
;; no-exec record pass is pure cost), i.e. the reused model is
;; conservative.  The FZ downstream-fallback oracle above is SKIPPED
;; for an admitted peel BY THE GUARD'S OWN KEYING: the mirrored
;; refuted composition exists only for no-exec records, and the group
;; guard itself still audits the final placement at its own pass time
;; (an exec capture cannot flip a group the unhoisted world kept).
;; NO NEW CONSTANTS.  Refusal names (all keep the mirror's refusal and
;; today's bytes): record-hoist-peel-qsr-exec-record-unavailable
;; (cannot exec while capturing on QSR), record-hoist-peel-multibb-loop,
;; record-hoist-peel-trips-unproven (constant trips >= 2 by the
;; provable_constant_trips discipline; runtime-trip admission is
;; deliberately NOT extended here -- the counter rewrite needs the
;; proven chain), record-hoist-peel-body-foreign-insn (full body
;; coverage: every body word is a clone-span member, the counter step,
;; or the final jump -- a word outside the spans would be dropped from
;; the peeled trip), record-hoist-peel-counter-rewrite-unproven (the
;; re-init constant must be single-insn materializable post-reload,
;; the launch-loop unroll's own SMALL_OPERAND/LUI_OPERAND bound).
;; COMPOSITION DOWNSTREAM: the peeled loop body (pure launches + loop
;; control, proven trips-1) is admissible to the existing launch-loop
;; unroll, whose exec-flip increment correctly no-matches (it requires
;; a no-exec capture, operand test XVECEXP 6 == 0) -- the plain unroll
;; path then removes the loop control; both compositions leave the
;; re-ingestion cadence at one exec-record per outer-loop entry, the
;; witnessed class.
;;
;; RECORD-HOIST PLACEMENT LIFT (lane IL, 2026-08-28;
;; -mtt-tensix-optimize-record-hoist-lift, Init(0), composing on
;; record-hoist mode only).  Rescues exactly the DOWNSTREAM-FALLBACK
;; refusal above for STORELESS payloads
;; (record-hoist-downstream-fallback-unprofitable: the lcm-fresh shape
;; -- an invariant computational window re-recorded exec-while-record
;; once per row, whose no-exec hoist into the INNERMOST dedicated
;; preheader would sit within the audited drained-frontend window of
;; the row's own would-be dst-autoincr mod-write loads/store across
;; the backedge).  KEY FACT: the oracle's distance walk runs UPSTREAM
;; of the placement (the hazard is a record INGESTED inside the
;; retirement window following a mod-write) and a path reaching the
;; function entry is proven separated -- so an OUTER dedicated
;; preheader, ultimately the function entry, is outside the refuted
;; composition by the guard's own distance semantics.  That placement
;; is the witnessed init-record discipline (the xielu/gcd/lcm preamble
;; class; the raw gcd init records its round program once per kernel
;; at entry).  The lift walks outward across enclosing loops and
;; commits the UNCHANGED no-exec hoist at the outermost admissible
;; oracle-clean level: each crossed loop must prove replay-preserving
;; under the record-hoist interval walk (an in-loop replay owner,
;; call, asm, or possible FIFO push could re-record the lifted slots
;; between record and a later trip's launch; the walk covers every
;; intermediate block), each candidate placement must be a DEDICATED
;; preheader with no open user recording state, and each is re-audited
;; by the SAME oracle (a still-covered placement walks on).  A failing
;; level STOPS the walk (never refuses; the lane HC residency-walk
;; discipline); no oracle-clean level keeps today's bytes by name
;; (record-hoist-lift-no-admissible-level).  SOUNDNESS is the existing
;; hoisted no-exec capture class at a different placement: dominating
;; and not forward-reachable except through the placement itself (FS
;; rules hold), STORELESS by construction (the Dst-store mirror
;; refuses those payloads before the oracle; sweep rule 1 is keyed to
;; Dst-store payloads and storeless no-exec captures are the
;; silicon-good celu/eqz class), re-ingestion at a still-in-loop
;; placement repeats the SAME fixed-encoding words once per that
;; loop's trip (idempotent; invariance is the record-hoist
;; fixed-encoding admission), and sweep rule 2 re-audits the final
;; placement's mod-write distance with the same predicate at end of
;; pass.  PRICING: hoist_profitable_p unchanged on the immediate loop
;; -- the lifted record is delivered at most as often as the modeled
;; immediate-preheader record, so the modeled benefit is a floor.
;; NO NEW CONSTANTS.
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
;; COUNTED-CAPTURE PEEL (lane IO, 2026-08-29;
;; -mtt-tensix-optimize-counted-capture-peel, Init(0)).  Extends the
;; lane-GQ exec-while-record first-trip peel to the COUNTED-LOOP
;; capture class -- a counted single-block SFPU row loop whose body is
;; one fixed replay-safe run and records nothing per trip.  The plain
;; counted hoist places a NO-EXEC record in the dedicated preheader and
;; pays the payload's FULL re-delivery there,
;;
;;   benefit_plain = trips * (before - after)
;;                   - (deliver_record + RECORD_OVERHEAD)
;;
;; which cannot amortize on delivery-paced row loops whose per-trip
;; relief (before - after) is small: the addrsqrt-fresh production-shape
;; row loop (trips 31, words 24, exec_ilk 28) prices
;; 31*82 - 3375 = -833 and refuses, leaving the row delivered inline
;; from the RISC every trip while the hand kernel's exec-while-record +
;; launches shape runs execution-paced.  The peeled shape never
;; re-delivers the payload: the loop's proven first trip moves verbatim
;; to the dedicated preheader with the capture flipped to
;; exec-while-record (TTREPLAY load=1 exec=1 -- the fleet-witnessed
;; class, never the silicon-refuted no-exec re-record wedge), every
;; remaining trip becomes one playback launch, and the proven-constant
;; counter re-initializes one step later (trips -> trips-1):
;;
;;   benefit_peel = (trips - 1) * (before - after)
;;                  - (RISC_PUSH + RECORD_OVERHEAD)      ; >= MIN_BENEFIT
;;
;; The peel pass pays only the capture word (RISC_PUSH) plus the
;; record-engine overhead beyond the payload delivery the baseline
;; first trip already paid; before/after are the counted-loop capture
;; terms unchanged.  For the addrsqrt shape: 30*82 - 423 = +2037.
;; STREAM IDENTITY is inherited from the GQ peel with a weaker premise
;; (there is no former in-body record site): payload instances
;; 1 + (trips-1) = trips, in trip order, at the same stream positions
;; (the dedicated preheader immediately precedes the loop).  ADMISSION
;; = the counted-loop capture's own gates unchanged (fixed-encoding
;; replay-safe payload, replay-preserving loop, dedicated preheader,
;; slot availability) AND peel_admissible_p unchanged (non-QSR,
;; single-bb body, proven constant trips >= 2, full body coverage,
;; provable single-insn counter re-init).  An admitted PLAIN counted
;; hoist is never converted (the peel is evaluated only on the plain
;; pricing's refusal, so every booked counted-capture fire keeps its
;; bytes).  A reform-mode carried payload refuses by lane IH's
;; post-autoincr-window-carried-peel-launch-arithmetic-unproven (the
;; peel relocates one trip's carried executions across the
;; configuration program's placement; the walk-order proof is not in
;; this increment).  Refusal names (all keep the plain refusal's
;; bytes): counted-capture-peel-trips-unproven,
;; counted-capture-peel-benefit, replay-reissue-latency-unproved, and
;; peel_admissible_p's record-hoist-peel-* names.  NO NEW CONSTANTS.
;;
;; REPLAY WINDOW SIZING UNDER A HOISTED RECORD (lane IM, 2026-08-28;
;; -mtt-tensix-optimize-replay-window-sizing, Init(0)).  pick_replay's
;; saving key, (clones-1) x (length-1), prices IN-BLOCK
;; exec-while-record economics: each replaced clone trades length words
;; for one launch word while the record's own words STAY IN THE BODY,
;; so a shorter window with more instances wins -- lcm-fresh picks the
;; 14-word Stein round-pair at 7 instances (saving 77) over the 28-word
;; round-quad at its only 3 non-overlapping instances (saving 53), and
;; lane IH measured that key RIGHT for in-block windows (freed slots
;; let tail windows form).  A HOISTED record (record-hoist /
;; replay-hoist preheader placement, incl. the placement lift above)
;; voids that model's record term: the record is delivered once per
;; placement, the per-trip cost is the launch words alone, and the
;; widest word-exact window that fits the free slots minimizes it.
;; The hand kernels witness the discipline this flag reconstructs
;; (gcd/lcm: TTI_REPLAY(0,28,0,1) once per kernel entry, then per row
;; 3 x REPLAY(0,28) + 1 x REPLAY(0,13) -- the last launch a PARTIAL
;; playback of the recorded window's 13-word prefix).  Two vocabulary
;; pieces were missing, and both are gated behind this flag:
;;
;;   WIDENING: after the ORIGINAL pick's hoist admission succeeds (and
;;   only then -- in-block picks keep pick_replay's measured-right
;;   key; an admitted first-trip peel keeps its own audited shape),
;;   same-anchor wider candidates that fit the largest free slot span
;;   are re-priced by delivered per-trip issue words over the covered
;;   span: full launches + one partial launch + every word left
;;   inline.  Strict improvement is required; the widened candidate
;;   re-proves the WHOLE hoist admission itself (oracle, pricing,
;;   dedication, lift walk), its clones are structurally re-verified
;;   word-exact and non-overlapping (discovery matches by hash), and
;;   any failing premise keeps the original pick by name:
;;   window-sizing-slot-exhausted (wider candidates exist, free slots
;;   cannot hold the record -- the IH useq eviction class stays
;;   protected), -no-wider-candidate, -no-cheaper-delivery,
;;   -coverage-short, -clone-arithmetic, -hoist-refused,
;;   -reform-composition-unaudited (a widened carried payload would
;;   need the reform launch-arithmetic audit re-derived for the trim).
;;
;;   PARTIAL-LAUNCH TRIM: the trailing run after the last clone that
;;   is a word-exact PREFIX of the recorded window is delivered as one
;;   REPLAY launch whose Count is the run's word count.  ISA basis:
;;   a playback launch emits ReplayBuffer[(Index+i)%32] for i in
;;   [0,Count) -- a pure prefix of the recorded program, independent
;;   of the recorded length (WormholeB0 REPLAY.md functional model;
;;   BH mirrors it per the lane FS silicon persistence model; the hand
;;   kernels' REPLAY(0,13) is the silicon witness).  The prefix walk
;;   mirrors sequence-growth continuity exactly (never crosses a
;;   must_end word, a deleted insn, or the block end; stops one word
;;   short of a full clone), so the launch replaces words that
;;   executed inline with the same delivered words in the same stream
;;   positions: stream identity, like every full clone replacement.
;;   The record dominates the partial launch (preheader placement),
;;   the slots are persistent and disjoint (FS model; the widened
;;   record's full span is marked), and the TEN-2932 window checker
;;   resolves sub-span launches natively.
;;
;; PRICING: no new constants.  The widening comparison is a word count
;; over the covered span (launches are delivered words; the launch
;; boundary crossing cost, ~1.3-1.8 cy each at the lane EE table, is
;; NOT modeled -- fewer launches strictly reduces both terms, so the
;; word-count key is a conservative proxy).  The hoist pricing that
;; licenses the placement is hoist_profitable_p on the widened window
;; itself, unchanged.
;;
;; lcm-fresh (the naming shape): entry record widens 14 -> 28 words
;; (18 free slots held; 28 <= 32), per row 7 x REPLAY(0,14) + 4 inline
;; trim words -> 3 x REPLAY(0,28) + 1 x REPLAY(0,18) -- the hand
;; kernel's exact 4-launch row delivery.
;;
;; LAUNCH-FLATTEN complete-unroll request (lane HH; the GIMPLE-side
;; generalization of the launch-loop unroll above).  A counted innermost
;; DELIVERY loop -- typed replay records/launches, fixed raw .ttinsn
;; words, computed-word volatile delivery stores (the LLK TT_ macro
;; shape; the flatten folds each per-trip recomputed word to the
;; constant the raw-word arm's unroll has always produced), typed SFPU
;; builtins, own scalar control -- pays the same two
;; delivered loop-control words per trip, plus one folded-away branch
;; per per-trip conditional (a direction flip-flop, a record-once
;; guard), all functions of the proven trip number.  The request pass
;; (gimple-rvtt-replay-unroll.cc, -mtt-tensix-optimize-launch-flatten)
;; sets loop->unroll to the proven trip count immediately before the
;; GIMPLE complete unroller: the transformation is the generic,
;; unconditionally-sound complete unroll; the dynamic word stream is
;; unchanged by construction.  Pricing reuses the row-request bounds
;; unchanged -- XTT_REPLAY_LOOP_UNROLL_MIN_WORDS below per trip (fewer
;; delivered words cannot price the removed control words against
;; growth) and XTT_REPLAY_LOOP_UNROLL_MAX_WORDS on the flattened total
;; (the same straight-line size class the factor-8 row group commits
;; to).
;;
;;   XTT_LAUNCH_FLATTEN_FN_BUDGET_WORDS bounds the SUM of flattened
;;   totals (words * trips) across one function's fires.  The per-loop
;;   bound cannot cap a vehicle that instantiates many admissible
;;   delivery loops: the topk_xl K=2048 correctness TU overflowed
;;   TRISC1_CODE by 1836 bytes with per-loop-only budgeting (a loud
;;   link error; the refusal-by-name is the honest form).  1024 = four
;;   per-loop budgets: the smallest power-of-two envelope over every
;;   measured winning fire set (the topk flip needs ~880 estimated
;;   words across phases_steps' five fires; the census overestimates
;;   real growth ~5x -- typed plumbing words fold away -- so the cap
;;   bounds real text growth to roughly 1KB per function).  A size
;;   guard, not a shape key: only proven trips and delivered word
;;   counts participate; loops refuse in program order once the budget
;;   is exhausted (launch-flatten-function-budget).
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
  (XTT_LAUNCH_FLATTEN_FN_BUDGET_WORDS 1024)
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
;; Cross-row pairing (rtl-rvtt-schedule.cc, crossrow_pair_rows, under
;; -mtt-tensix-optimize-crossrow-pairing).  Additive section.
;;
;; PAIR_FACTOR (2) - the pairing doubles exactly two consecutive
;; iterations: two rows are the smallest interleave that fills a
;; distance-1 producer/consumer adjacency and the row seam, the doubled
;; footprint (one extra Dst row at +2 address units) stays inside the
;; separator's own per-iteration frame, and the doubled live set is the
;; only shape the eight-LREG file is known to carry for the target row
;; class (laneGJ AUTOPSY roundingops: rename web L0-L2 -> L4-L6,
;; pressure 7 <= 8).  Wider factors have no audited pressure or Dst
;; footprint story and refuse structurally (capture budget).
;;
;; MIN_ROW_WORDS (4) - mirrors the replay pass's MIN_SEQUENCE (4,
;; rtl-rvtt-replay.cc): a row the capture machinery would not record is
;; not in this mechanism's charter (the pairing exists to improve the
;; EXECUTION of a captured window, never to displace the capture).
;;
;; The capture budget bound reuses XTT_DELIVERY_CAPTURE_SLOTS (32, the
;; replay buffer's entries): a doubled row of 2n words must still fit
;; the buffer or the counted-loop capture downstream stops firing and
;; the pairing would trade record-plus-launch delivery for a rolled
;; issue stream -- the adjudicated profitability defect of the
;; round-cc-modulo prototype (NO-GO 2026-08-25, evidence
;; round-cc-modulo-evidence-20260825/REPORT.md: the committed rolled
;; two-row loop lost the TTREPLAY delivery entirely).  The separator
;; stays explicit per launch (counted_loop_payload's contract) and
;; dst-autoincr may absorb it afterwards exactly as in the single-row
;; stream.
;;
;; Dst disjointness fact: the admitted row separator TTINCRWC
;; (0, 2, 0, 0) advances the Dst RWC by 2 address units = one 32-bit
;; row (SFPLOAD/SFPSTORE unit addressing; tt-isa-documentation
;; TTINCRWC/SFPLOAD address-unit semantics, the same 1-index-=-2-units
;; fact laneFI's walk lift used with TTINCRWC imm <= 7).  Two accesses
;; at constant offsets A and A+2 of one counter frame with A == 0 mod 4
;; therefore touch disjoint unit footprints, and the doubled separator
;; (0, 4, 0, 0) advances exactly the two rows the pair consumed
;; (4 <= the TTINCRWC immediate bound).
;;
;; CC placement: interleaving is licensed by CC-STATE EQUALITY, proven
;; structurally -- flat atoms (CC writer through word-exact all-lanes
;; SFPENCC restore, the effect vocabulary's cc_write_all_lanes fact)
;; stay indivisible in original interior order, so every atom word
;; executes under its own row's lane state; every inter-atom position
;; is the all-lanes state in both the sequential and the paired order
;; (each atom closes with the restore), provided the LOOP-ENTRY ambient
;; is all-lanes -- proven by the backward walk to the nearest reaching
;; CC writer (must be the all-lanes restore) or to the function entry,
;; whose all-lanes ambient is the shipped structured-CC lowering
;; contract (gimple-rvtt-cc.cc: outermost PUSHC removed, every
;; outermost region closed by the exact all-lanes ENCC; laneEL's
;; structured-CC-restore proof).  Rename webs may root only in that
;; ambient state (crossrow-pairing-rename-cc-domain): a fresh
;; lane-predicated definition renamed to a dead LREG would expose stale
;; disabled-lane bits -- the adjudicated wrong-code defect of the
;; round-cc-modulo prototype's rename reuse.
;;
;; These constants describe structural bounds only.  They are
;; deliberately independent of any operation identity, opcode calendar,
;; coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_CROSSROW_PAIR_FACTOR           2)
  (XTT_CROSSROW_MIN_ROW_WORDS         4)
])


;; Rule-B preservation seeds (-mtt-tensix-optimize-crossrow-pairing-seed,
;; the round-cc-modulo DESIGN-V2 Rule-B rename; additive sub-feature,
;; effective only where the pairing above admits the loop).
;;
;; Lane-exactness fact: the seed word is the bare-SET rvtt_sfpassign
;; alternative -- SFPMOV mod-2 -- which writes EVERY lane regardless of
;; the CC state (craq-sim TENSIX_EXECUTE_SFPMOV mod 2 forces the full
;; lane mask; the same audited hidden-state-free fact the shadow-fill
;; crossing rule relies on).  Seeded immediately after the LAST
;; definition of the old register that precedes the root -- in the
;; ambient position before the flat atom's first CC writer when the
;; value reaches the atom entry unwritten, or INSIDE the atom directly
;; after an in-atom producer (the mod-2 copy's lane-immunity is exactly
;; what licenses the interior position; the seed joins the atom's
;; indivisible item so the original words keep their interior order and
;; CC contexts) -- it makes
;; the fresh register F lane-wise equal to the old register R at the
;; atom-interior fresh root: the root then writes the same enabled
;; lanes it originally wrote into R, disabled lanes of F carry exactly
;; the value R's disabled lanes carried (a read-modify-write root's
;; implicit read consumes the lane-equal F), and rewriting every later
;; web member through the web's fresh terminator keeps the equality
;; inductive across later CC domains, so an all-lanes consumer or store
;; observes the identical value.  This is precisely the disabled-lane
;; exposure the plain rename discipline refuses
;; (crossrow-pairing-rename-cc-domain), discharged by paying one word.
;; A web whose ROOT is itself a bare all-lanes copy (a full-lane root:
;; SFPMOV mod-2 writes every lane wherever it sits, so the fresh
;; register carries the complete value from the root on) renames
;; seed-free at zero word cost -- DESIGN-V2 Rule A carried into the
;; atom interior by the same lane-immunity fact.
;;
;; Pricing: NO new constants.  A seed is one issued word charged in the
;; SAME steady-state II model and the SAME capture-budget bound
;; (XTT_DELIVERY_CAPTURE_SLOTS) as every row word -- the modeled II of
;; the seeded candidate already contains the seed's issue slot, so the
;; strict-improvement acceptance (seeded II strictly below the unseeded
;; candidate's, with the non-improving tail of the forward pass rolled
;; back to the last strict checkpoint) is the complete cost comparison:
;; a seed whose slot costs more than the serialization it removes never
;; commits, and delivery stays record-plus-launch because the budget
;; bound keeps the counted-loop capture firing.  Provenance:
;; round-cc-modulo-evidence-20260825/DESIGN-V2.md (Rule B), the laneEL
;; structured-CC restore contract (ambient positions), and the laneGP
;; adjudication that Rule-A pairing alone leaves the atom-rooted webs
;; serialized (laneGP-evidence-20260825/RESULTS.md).
;;
;; Stall-words extension (-mtt-tensix-optimize-crossrow-pairing-stall-
;; words, lane IC).  Additive; NO new constants.  Three coordinated
;; pieces, all priced through the existing models:
;;   1. Vocabulary: a word carrying the architectural next-slot
;;      ACCEPTANCE stall (xtt_next_slot_stall -- the SFPSWAP family)
;;      joins crp_node with its audited biased result latency and an
;;      issue occupancy of TWO slots (the xtt_next_slot_stall consumer
;;      rule above: one extra slot per occurrence), charged identically
;;      in the doubled sequential baseline and every candidate; its
;;      recorded-word count against XTT_DELIVERY_CAPTURE_SLOTS stays
;;      one (the stall is an issue fact, not a stream word).
;;      audited_latency () itself is untouched: fill passes keep
;;      refusing these words (lane BM).
;;   2. Free-LREG priority: the Rule-A cyclic renamer offers the free
;;      registers to the COPY half's webs first (scan order only; web
;;      extents and rewrites stay in stream order).  Breaking row-B
;;      serialization is the pairing's entire benefit; an intra-row
;;      false-recurrence rename that grabs the last free register
;;      leaves the row-B accumulator serialized and the II gate then
;;      refuses the whole transform (the tanh anatomy).
;;   3. Critical-path selection + capture-overflow belt: among READY
;;      items the candidate constructor prefers the longer remaining
;;      critical path (ls_list_order's rule at item granularity) so
;;      the two rows' tails interleave and the SFPMAD->SFPSWAP delay
;;      shadows fill with real words; and the doubled record is
;;      admitted only while row words PLUS the pad sites the nop
;;      inserter still owes fit XTT_DELIVERY_CAPTURE_SLOTS
;;      (crossrow-pairing-capture-overflow refuses) -- at 2n == 32 a
;;      single surviving pad silently trades record-plus-launch for a
;;      rolled issue stream, the adjudicated round-cc-modulo
;;      profitability defect.
;; Soundness of the delay contracts is NOT this extension's burden:
;; the pairing runs before the nop inserter, which re-discharges every
;; STATIC/DYNAMIC delay (including the BH SFPMAD->SFPSWAP scoreboard
;; erratum, xtt_dynamic_bug) over the committed final order; the
;; pad-site probe and the overflow belt only PRICE that discharge.
;; Provenance: laneIC-evidence-20260827 (tanh 2-datum window-density
;; autopsy), SFPSWAP.md acceptance-stall rule, laneHM
;; counted-row-vacated-delay-shadow adjudication (the delay-contract
;; positional-discharge hazard class).


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
;;
;; PRESSURE-PARK extension (lane GV, -mtt-tensix-optimize-pressure-park;
;; the FX PASS-GAP "invariant-loadi rename/pressure admission" class):
;; two widenings of the CC-canonical residency PEEL class, both riding
;; the peel's existing break-even proof (the candidate's issue words
;; join sum_w exactly like pre-CC candidates; no new pricing constants).
;;
;; (1) POST-CC POSITION ADMISSION.  A candidate materialization at or
;; after the body's first CC writer is admitted when every consumer is
;; in the audited lane-predicated set (remat_consumer_audited_p; PHI
;; uses and _lv-tied operands refuse by name).  The parked read carries
;; the constant in EVERY lane -- a strict superset of whatever lane
;; subset the original predicated SFPLOADI wrote -- so all
;; originally-defined lanes are bit-exact and only
;; originally-indeterminate lanes change (the invariant pass's ratified
;; superset-write refinement).  Save = the candidate's issue words per
;; iteration after the first; cost = PUSH per staged word + PUSH per
;; SFPCONFIG, priced inside the peel break-even.
;;
;; (2) LREG TIER on prgm-exhausted.  When the three PRGM destinations
;; are gone, a remaining admitted candidate hoists to the SAME proven
;; programming point as a plain SSA live range (rename-to-free-LREG),
;; budgeted by the function-wide SSA pressure model at this pipeline
;; position (CC machinery already lowered to explicit statements, so
;; the invariant pass's cc_transients blindness does not apply): each
;; hoist charges one register; capacity 8 exceeded refuses by name
;; (lreg-file-exhausted) and changes nothing.  Save = the candidate's
;; issue words per iteration after the first; cost = zero extra words
;; (the materialization moves; no SFPCONFIG).  The peel break-even
;; computed over the full candidate set is therefore conservative for
;; this tier.
;;
;; Named refusals: postcc-phi-use, consumer-lane-discipline-unaudited,
;; lreg-file-exhausted, plus the pre-existing prgm-exhausted and every
;; peel-class refusal unchanged.  Flag off: byte-identical (the scan
;; break and single-tier placement are restored verbatim).
;;
;; STORE-SOURCE TIER (lane HO, -mtt-tensix-optimize-store-source-tier;
;; the HL-F1 encoding-ceiling copy tax, generalizing lane HL's
;; license-gated refusal): SFPSTORE sources L0-L11 only
;; (SFPSTORE_MAX_SRC_LREG), so a PRGM-parked (L12-L14) store-source
;; constant is NOT free at its store consumers -- the register
;; allocator materializes a per-consumer SFPMOV copy out of the
;; constant file, one issued word per row inside a loop.  Placement
;; economics per store-consumed loop-class candidate:
;;
;;   parked (status quo)   1 word/row (the copy) + programming words
;;   LREG-tier hoisted     0 words/row, zero programming words
;;                         (the materialization moves; no SFPCONFIG),
;;                         1 LREG of function-wide pressure
;;   bare refusal          the in-loop materialization stays:
;;                         1 word/row (short imm) or 2 words/row
;;                         (wide constant) -- NEVER chosen
;;
;; So the knob routes the candidate to the LREG tier FIRST and, when
;; the tier refuses (lreg-file-exhausted, or the pressure-park tier is
;; not enabled), falls through to the established park byte-identically
;; -- a strict never-worse ordering (the copy word beats or ties the
;; rematerialization; the hoist beats both).  Math consumers of the
;; same candidate read the hoisted LREG exactly as they read the parked
;; register.  The store-sink license token's own place() refusal (all
;; candidate classes, no park fallback) is kept verbatim: the licensed
;; sink's word accounting requires the value NEVER park.  Named dump
;; line: "store-source-tier (store-source-encoding-ceiling)"; refusal
;; names unchanged (lreg-file-exhausted).  Flag off: byte-identical.

;; CROSSLOOP-CC-PEEL (lane HR, -mtt-tensix-optimize-crossloop-cc-peel;
;; the atan2 "crossloop-cc-unproven" peel-composition class): a
;; PROGRAMMING-ONLY lift of the CC-canonical peel class's placement
;; across enclosing loops.  The peel exists only to manufacture an
;; all-lanes programming point INSIDE a loop whose body writes CC --
;; and the placement walk's region scan blanket-refuses those very CC
;; writes at every enclosing level (crossloop-cc-unproven), so a
;; peel-class placement could never lift and the peel-plus-programming
;; re-executed on EVERY enclosing iteration (atan2: 27 peeled body
;; words + 2x2 programming words per face entry for constants that
;; cannot change).  Under the cc-immaterial region discipline
;; (structured typed CC atoms admitted -- their whole effect is the
;; lane-enable state plus an SSA definition, and the parked constant
;; register is out of any CC write's reach; delivered words, replay,
;; MOP census, and explicit LREG writes keep their established
;; refusals) plus a no-CC-write-reaches proof at the lifted preheader
;; (the plain loop class's own fn-entry-all-lanes ambient; the point
;; executes once, ahead of every crossed iteration), the loop's
;; candidates place as plain loop-class programming at the outermost
;; proven entry and the peel is never created:
;;
;;   flag off (peel)     per enclosing entry: body_w peeled words +
;;                       (sum_w + nprog) programming words, priced by
;;                       the residency-peel break-even
;;   lifted              once per kernel: (sum_w + nprog) programming
;;                       words; zero peeled words; in-loop candidates
;;                       read the parked register on every iteration
;;
;; Strictly cheaper than the peel wherever both fire; additionally
;; fires where the peel break-even refused (the lift pays W+1 words
;; once against W per iteration -- the plain class's model), gated by
;; the plain class's trip policy (a proven single trip refuses:
;; trip-count-single-trip; runtime trips admit, worst case one extra
;; pushed word).  Lane discipline is the peel class's own: the lifted
;; staged load writes EVERY lane under the proven ambient (superset of
;; any consumer mask; post-CC candidates additionally passed the
;; pressure-park consumer audit at collection).  The transform inserts,
;; deletes, and reorders NO CC-writing statement relative to the
;; original program (the peel it forgoes was this pass's own duplicate)
;; -- no exec-state shape can form that the source did not already
;; contain (the ES/FJ hazard discipline).  Named refusals:
;; crossloop-cc-atom-unproven (a CC writer off the typed whitelist),
;; crossloop-cc-peel-entrycc-unproven (a CC write reaches the lifted
;; preheader), trip-count-single-trip, plus every walk-stop name
;; (crossloop-word/replay/stmt/config-word/mop-slot-unproven,
;; crossloop-speculation-unproven, crossloop-preheader-unproven) and
;; every peel-class refusal unchanged.  Flag off: byte-identical (the
;; peel path is restored verbatim).
;;
;; ---------------------------------------------------------------------
;; CROSSCALL CONFIG-PREFIX + RESIDENCY (lane HC,
;; -mtt-tensix-optimize-crosscall-config-prefix; the geluappx
;; "table-prefix/crosscall-residency" residual class): two widenings of
;; the cross-call coefficient contract, no new pricing constants -- the
;; contract's own amortization argument (per-call prefix words become
;; per-loop-entry words) simply reaches more words and better entries.
;;
;; (1) CONFIG-PREFIX PAIR.  A callee prefix pair (all-constant
;; immediate materialization whose single consumer is sfpwriteconfig_v
;; to a programmable-constant register 11..14) joins the contract:
;; re-materialized once per proven caller loop entry AHEAD of the
;; contract loads (SFPCONFIG's source operand is md-pinned to L0 -- the
;; callee's own prefix order), deleted from the callee.  Save = 2 words
;; per call (the gelu licensed body's per-tile vConstFloatPrgm0
;; programming); cost = the same 2 words once per loop entry.  The
;; caller proofs widen to the programmed register (mask extension +
;; config_strict: delivered SFPCONFIG-class words refuse; MOP template
;; slots must be config-word-free) and the callee pressure proof uses
;; the invariant pass's ratified creg-read exemption (LReg[8..14] reads
;; never occupy an allocatable LREG).
;;
;; (2) PLACEMENT RESIDENCY WALK.  The committed contract entry lifts
;; across ENCLOSING caller loops level-by-level while each level's body
;; passes the SAME caller-epoch scan and vector-liveness proof (a
;; failing level stops the walk -- never a refusal).  geluappx: tile
;; loop -> 8-tile batch loop -> kernel entry = the hand init
;; discipline (program once per kernel).
;;
;; Named refusals/disqualifications: crosscall-config-dest-unproven,
;; crosscall-config-writer-unproven, crosscall-config-shape-unproven,
;; mop-template-config-word-unproven, the widened
;; crosscall-caller-foreign-contract/config-word-unproven; flag off:
;; byte-identical (discovery never runs; the historical
;; vector-outside-loop refusal and single-level placement restored
;; verbatim).

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

;; ---------------------------------------------------------------------
;; LOAD-CARRIER unlock (lane IF,
;; -mtt-tensix-optimize-dst-autoincr-load-carrier).  Additive section;
;; no new pricing constant -- one audited COUNTING fact and one
;; replay-soundness model note.
;;
;; COUNTING FACT (the whole knob): a canonical single-constant
;; `.ttinsn' asm word (the TTI_ macro shape, audited extraction
;; rvtt_raw_ttinsn_word) is by construction exactly one 32-bit Tensix
;; word in the issue stream, so it occupies exactly one replay-buffer
;; slot during a recording ([ISA] WormholeB0 REPLAY.md functional
;; model: the Load loop stores every incoming instruction, one slot
;; per word, with no opcode filtering; BlackholeA0 carries no REPLAY
;; functional model -- doc gap already adjudicated by the lane FS
;; silicon persistence experiments -- and the pinned craq sim ingests
;; recorded words identically) and exactly one frontend issue slot
;; ([SIM] every slot-word walk in the sim's issue model counts words,
;; not classifications).  The pin-38 dst-autoincr walks counted raw
;; words as ZERO slots, so an LLK envelope recording whose shadow is
;; raw words overran its block and the scan refused the whole function
;; ("replay capture crosses block") -- adjudicated as THE blocker of
;; the load-carrier class (lane IE useq probe; the identical
;; load-terminated rows fire in a record-free function at pin 38, so
;; no admission gap exists in the row machinery itself).  The knob
;; makes the count exact in occupies_replay_slot_p and every walk
;; built on it (shadow folding, consume prefixes, iteration cover,
;; no-exec-composition distances); classification is UNTOUCHED: raw
;; words keep AIC_FOREIGN (or the audited pure-RWC decode), never
;; become rewritable payload members, gap-legal items, or
;; configuration-window-legal items, so every ownership wall stands.
;;
;; REPLAY-SOUNDNESS MODEL for carried walks (the lane IF adjudication,
;; ISA-doc-derived; the walk-must-restart question answered):
;;
;;   [ISA] REPLAY (WormholeB0 REPLAY.md): a launch re-emits the STORED
;;   WORDS into the same downstream pipeline; a replayed SFPLOAD /
;;   SFPSTORE / INCRWC executes architecturally identically to the
;;   inline word.  RWC effects (INCRWC.md, RWCs.md ApplyPartialAddrMod)
;;   are PER-EXECUTION CUMULATIVE counter adds on RWCs[CurrentThread];
;;   the ADDR_MOD increment values live in ThreadConfig (SETC16state),
;;   which no launch resets.  There is NO per-launch reset of either --
;;   and none is needed: the walk is execution-count-linear.
;;
;;   THEREFORE the only skew mechanism is a mismatch between the
;;   number of EXECUTIONS of a carried access and the number of
;;   removed explicit increments.  The pass's existing payload-coverage
;;   rule ("payload execution site without matching increment") IS the
;;   fail-closed skew guard: it forces every execution site (launches
;;   + executing captures) of a rewritten payload to be a transformed
;;   row, i.e. executions == removed increments, per capture, exactly.
;;   Launch count per se is irrelevant; an uncovered launch site
;;   refuses by that name.  Cross-invocation record persistence (lane
;;   FS FP-3) is already handled by the no-exec-composition dominance
;;   clause; it concerns record ARMING, not walk arithmetic.
;;
;;   [HAND] the production sdpa_reduce_row kernel records an SFPLOAD
;;   with a live Dst increment on its ADDR_MOD as the last recorded
;;   word and launches the window repeatedly per tile -- the
;;   silicon-proven precedent that a carried access inside a replayed
;;   payload walks exactly once per execution.
;; ---------------------------------------------------------------------

;; ---------------------------------------------------------------------
;; POST-AUTOINCR WINDOW RE-FORMATION (lane IH,
;; -mtt-tensix-optimize-post-autoincr-window).  Additive section; no new
;; pricing constant -- one ordering fact, one soundness theorem, and the
;; carried-payload launch-arithmetic discipline.
;;
;; ORDERING FACT (the whole knob): the replay former runs BEFORE the Dst
;; auto-increment fold (rvtt-passes.def anchor order), and the explicit
;; per-row TTINCRWC separators are window-EXCLUDED barrier words
;; (xtt_replay barrier), so a carried row body is word-uniform -- and
;; therefore capturable -- only AFTER the fold, at a program point the
;; formation never sees.  Capturing the increments instead is the
;; silicon-refuted direction (lane IE uniform-block twins: every
;; captured pre-fold form measured +17-39 cy/tile WORSE than the
;; straight-push lift -- raw sync words excluded from windows, boundary
;; and epilogue cost; the FI envelope law).  The knob DEFERS the
;; formation wholesale: pass_rvtt_replay gates itself off and the same
;; transform runs once, between pass_rvtt_dst_autoincr and
;; pass_rvtt_mop_form (which by its own contract must see the final
;; launch stream).  Deferral rather than a second run: a pre-fold run
;; consumes replay-buffer slots on the small pre-fold-visible windows
;; and starves the fold's larger windows (measured on the lane IE useq
;; vehicle: tail-shuffle windows worth <= 11 delivered words claimed
;; slots [0,14) and left 2 free slots against a 45-word carried-body
;; candidate); the single post-fold allocation prices every candidate
;; against the one buffer.  Deferral loses no opportunity: the fold
;; only removes barrier words and retargets modifier operands of the
;; rows those barriers separated, and no pre-fold-capturable run
;; contains such a row, so every pre-fold-capturable run is
;; post-fold-capturable verbatim.
;;
;; STREAM-IDENTITY THEOREM (the re-formation's soundness): restricted to
;; the word-exact replacement paths, formation is an INSERTION-ONLY
;; transformation of the delivered instruction stream --
;;
;;   in-block: one exec-while-record word inserted before the first
;;   clone (its payload executes in place while recording); each other
;;   clone replaced by one launch AT ITS POSITION whose expansion
;;   re-emits exactly the clone's words ([ISA] WormholeB0 REPLAY.md:
;;   the expander pushes stored words into the same pipeline; [SIM]
;;   pinned-sim replay_expander, same tensix_push_inst_fifo);
;;
;;   hoisted: the no-exec record's preheader payload is INGESTED, never
;;   executed (Load=1/Exec=0 swallowed words, the lane FR delivery
;;   model), and every clone becomes one launch in place;
;;
;; so the delivered word sequence equals the folded sequence with words
;; only INSERTED (capture/launch words, ingested preheader words).
;; Consequences, each an obligation discharged by the theorem:
;;   - carried walk arithmetic (per-execution-cumulative RWC/ADDR_MOD,
;;     the LOAD-CARRIER model above) is preserved verbatim: each carried
;;     access executes exactly once per replaced site, in stream order;
;;   - every positionally discharged delay-shadow contract (lane HM) is
;;     preserved: word gaps only GROW under insertion.  The one
;;     word-MUTATING phase (counted-row canonicalization) runs exactly
;;     once, in the deferred invocation, with all its own audits
;;     (lockstep, occupancy, crf_shadow_contract_ok delay re-verify,
;;     final lockstep) over the folded stream; its exclusion vocabulary
;;     refuses every Dst/RWC-effecting word, so no carried access is
;;     ever moved, and its register-map rewrites never touch a modifier
;;     operand;
;;   - the fold's ownership walls stand: formation edits no payload
;;     word, no configuration word, and no modifier operand.
;;
;; CARRIED-PAYLOAD LAUNCH-ARITHMETIC DISCIPLINE (fail-closed, each by
;; name): the fold's payload-coverage rule (executions == removed
;; increments) extends to the re-formed window's launch arithmetic --
;; the window's delivered payload executions must equal the replaced
;; row sites.  The word-exact paths preserve the equality by
;; construction (one delivery per replaced clone);
;; reform_carried_launch_arithmetic_ok re-verifies its premises (clone
;; non-overlap, word-exactness against the recorded clone) structurally
;; over the final clone list and refuses
;; post-autoincr-window-launch-arithmetic-skew.  The two
;; stream-RESTRUCTURING mechanisms whose delivered words are NOT the
;; replaced site's words refuse carried members by name:
;;   - post-autoincr-window-carried-isomorphic-conversion-unproven (the
;;     isomorphic-run launch conversion delivers the recorded words
;;     under a register-rename proof; renamed delivery of a
;;     positional-state access is unaudited in this increment);
;;   - post-autoincr-window-carried-peel-launch-arithmetic-unproven
;;     (the exec-while-record first-trip peel RELOCATES one trip's
;;     carried executions into the preheader, across the owned
;;     configuration program's placement point; the walk-order proof
;;     for the relocation is not in this increment -- the dststore
;;     mirror refusal then stands and the candidate falls back to
;;     in-block formation).
;;
;; INHERITED BELTS (run again, over this run's own formed-capture
;; list): the raw-REPLAY census, the recording-epoch scoping, slot-span
;; subtraction (this run's records take only slots no prior owner --
;; user, LLK envelope, or first formation -- declared, so no re-record
;; path into foreign slots exists), and the FS/FJ/FL un-hoist sweep
;; rules 1-3 (in-loop Dst-store re-record; mod-write W_drain window --
;; carried accesses are audited mod-write hazard words for that rule;
;; non-dominating persistent Dst-store record).  The ES/FJ no-exec
;; shapes therefore cannot form here any more than in the first
;; formation: formation itself never emits a launch a record does not
;; dominate (in-block launches follow their capture in the same block;
;; hoisted records dominate their loop), and the sweep un-hoists every
;; residual hazardous composition by its established names.
;;
;; PRICING: none new.  The second run consumes the same audited issue
;; model; profitability of hoists and conversions re-prices with the
;; first run's own terms over the folded stream.
;; ---------------------------------------------------------------------
;; ---------------------------------------------------------------------
;; PRESSURE-PARK PRE-PEEL PLACEMENT (lane IN, 2026-08-28; refinement of
;; the park LREG tier under -mtt-tensix-optimize-park-ordering; no new
;; flag, no new constants).  The park-ordering deferral hands a
;; CC-restore loop's in-region constants to the const-residency walk;
;; the walk's park LREG tier then placed the hoisted materialization at
;; the POST-peel programming point while the peel had already
;; duplicated the in-body load -- the parked constant was materialized
;; TWICE per loop entry (peel inline copy + park hoist), a per-entry
;; word tax the early invariant hoist never paid (it materialized
;; BEFORE the peel copied the body).  Witness: softplus PRODUCTION body
;; at ON-36, hand cell 138163 -> 139060 (+0.65% KERNEL, 8 duplicated
;; words per face-loop entry; lane HN's named hand-arm residual).
;;
;; The refinement places the park-tier materialization at the HEAD of
;; the peel block and erases the peel's duplicate (uses redirected to
;; the parked definition), gated by a PRE-PEEL AMBIENT ALL-LANES proof:
;; every backwards CFG path from the peel block's entry reaches the
;; function entry (all-lanes ambient; calls stay CC-transparent, the
;; plain loop class's established model) or an all-lanes-SFPENCC-
;; terminated block (the canonical tail itself is the kill) before any
;; escaping CC-affecting statement (typed sets_cc, SFPPUSHC/SFPPOPC;
;; calls and raw asm stay CC-transparent because the transform is
;; gated on the TU raw-boundary audit -- in any TU it runs in, every
;; raw word decodes through the audited non-CC table and every store
;; is proven unable to alias an instruction FIFO).
;; With the ambient all-lanes, the placement writes EVERY lane: the
;; identical superset-write refinement the post-peel point stands on,
;; now also covering the peeled iteration's own audited consumers.  An
;; unproven ambient refuses by name (park-prepeel-ambient-unproven) and
;; keeps the post-peel placement byte-identically; without
;; park-ordering the tier is byte-identical to its established form.
;;
;; PRICING: none new.  The tier's budget charge (one LREG per commit
;; against the function-wide pressure model) is unchanged; the erased
;; duplicate only removes issue words from the loop-entry path.
;; ---------------------------------------------------------------------
;; ---------------------------------------------------------------------
;; ENTRY-AMBIENT ENABLE DERIVATION + IMMEDIATE-DELTA ROWS (lane IS,
;; 2026-08-29; the owner-ratified F1 honest fix: the semantic sources'
;; empty sfppushc(0)/sfppopc(0) marker pairs are DELETED tree-wide and
;; the compiler derives what the marker used to signal).
;;
;; The marker pair lowered (pass_rvtt_cc outermost transform) to a
;; single all-lanes SFPENCC whose presence did two load-bearing jobs the
;; source was never entitled to do:
;;   (a) it was the planner's ambient all-lanes enable
;;       (rows[0].enable / preheader trailing enable), and
;;   (b) as a per-iteration CC write it BLOCKED the dst-iteration
;;       fusion, keeping rows separator-carried and therefore
;;       planner-isomorphic.
;;
;; (a) ENTRY-AMBIENT DERIVATION (rtl-rvtt-macro-planner.cc
;; entry_ambient_all_lanes_p): when a needs-all-lanes region has no
;; typed enable, no proven trailing enable, and no WP10 in-row restore,
;; a kill-aware backwards CFG walk from the configuration placement
;; point proves the fn-entry ambient all-lanes state (the established
;; structured-CC lowering contract; kills = the word-exact all-lanes
;; SFPENCC; calls/asm/opaque/unrecognized = DIRTY, fail-closed).  On
;; success the formation SYNTHESIZES the canonical all-lanes enable
;; (rvtt_sfpencc_all_lanes) at the prefix head -- re-writing the state
;; the walk just proved, a machine-state no-op.  PRICING: the enable was
;; ALWAYS counted in config_prefix_cost (the "+1 all-lanes enable"
;; term); the synthesized word is that word.  The explicit-side price
;; HONESTLY DROPS by the deleted marker's 1 word/row (rows[0].enable
;; gone), so straight-line shapes that only amortized their prefix
;; against marker padding now refuse unprofitable -- the honest verdict.
;; Refusal: all-lanes-proof-missing (ambient-entry-unproven).  The
;; crosscall init hoist v1 admits the ambient-synthesized enable (it is
;; the same canonical word the hoist's caller side already synthesizes);
;; the WP10 in-row materialization stays refused cross-call.
;;
;; (b) IMMEDIATE-DELTA ROWS (rvtt-macro-region.cc / rvtt-macro-sched.cc
;; / formation): fusion-shaped rows equal to rows[0] up to ONE common
;; typed Dst-address immediate delta are admitted when the region-level
;; absolute progression is uniform (row k's accumulated separator
;; advance + its immediate delta == k*S, total separator advance ==
;; rows*S) -- the SAME +S-per-row progression the separator-carried
;; shape expresses.  Formation MANDATES the absorbed-stride calendar
;; (imm-stride-not-absorbed refusal otherwise: the rows' immediates
;; cannot replay verbatim), dry-runs the address rewrite on every Dst
;; access of every offset row (imm-stride-rewrite-unproven), and
;; emission normalizes explicit copies back to rows[0]'s base while the
;; absorbed auto-increment supplies the advance.  The emitted calendar
;; is word-identical to the unfused shape's, and the final counter state
;; matches by the total-advance proof.  PRICING: unchanged formulas; the
;; per-run/per-trip explicit comparison keeps the frozen rows[0]
;; extrapolation (refusal-biased for this shape: it undercounts the
;; explicit side's separators).
;; ---------------------------------------------------------------------
;; ---------------------------------------------------------------------
;; INIT-HOIST-AWARE RUN PRICING -- CALLER-LOOP PREFIX AMORTIZATION
;; (lane IU, 2026-08-29; the laneIS-named successor for the minmax
;; class).
;;
;; THE GAP.  The macro planner's straight-line run gate froze the
;; conservative-per-run discipline:
;;     config_prefix + rows*ii + drain  <  rows * explicit_row_words
;; charging the FULL configuration prefix (all-lanes enable + owned
;; SETC16 program + descriptor-word materializations, in issue words)
;; to EVERY run -- priced BEFORE the crosscall init hoist was known,
;; although the hoist (lane CA, D2) is decided in the same formation
;; and, at stage 2, removes the prefix from the callee ENTIRELY: the
;; init contract words execute once per proven caller-loop entry.  The
;; post-F1 minmax shape (rows=32 runs=4, fused imm-stride rows,
;; explicit row = 4 words after the deleted marker's padding) refused
;; `unprofitable' at that arithmetic while the formed+hoisted form
;; measures 17451 cy vs the refusal's replay-delivered 25000 cy on
;; silicon (laneIS RESULTS.md) -- a pure pricing artifact.
;;
;; THE DERIVATION.  Let E = caller-loop entry executions and B = in-loop
;; call executions (the caller's profile counts, B >= E > 0, the SAME
;; unreduced-fraction discipline as loop_trip_weight/WP8 one call level
;; up).  Under the proven stage-2 contract the formed shape's issue
;; words per caller-loop entry are config_prefix once plus B *
;; (rows*ii + drain) per call; the explicit alternative pays B * rows *
;; explicit_row_words with NO prefix anywhere.  Cross-multiplied
;; (no rounding):
;;     config_prefix * E + (rows*ii + drain) * B
;;         <  rows * explicit_row_words * B.
;; Refusal-biased in every term: each run of a multi-run region charges
;; the FULL amortized prefix again (n runs charge it n times); the
;; weight comes from the committed hoist's own proven caller loop (the
;; profile fraction, exact for constant-bound loops); stage 1 (enable +
;; SETC16 retained per call) and an unusable profile weight keep the
;; frozen per-run pricing UNCHANGED, as does init-hoist-off.  The
;; PROOF-ONLY pre-run of the identical hoist proof chain
;; (rvtt_crosscall_init_hoist commit=false) runs ahead of the gate;
;; the caller-side insertion still commits LAST among the refusal
;; points, and a formation whose pricing consumed the amortization
;; refuses fail-closed (init-hoist-commit-diverged) if the committing
;; call could ever diverge -- it cannot on the refusal-free path, the
;; proof chain is deterministic over an unchanged function.
;;
;; The WP13/IMS arbitration composes identically: under the proven
;; stage-2 contract the formed side's prefix push words
;; (XTT_REPLAY_COST_RISC_PUSH_X100 centislots each) weigh by E while
;; both sides' per-call words weigh by B; the replay alternative
;; carries no prefix, so its lower-bound bias is preserved.  The lane
;; IA per-execution config pricing is untouched: it prices the
;; dst-autoincr SETC16 slot programs a NON-hoisted callee re-emits per
;; call, and its own crosscall relief is the lane IK ADDR_MOD service
;; -- the two contracts price disjoint words.
;; ---------------------------------------------------------------------
