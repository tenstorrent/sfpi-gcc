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
;; single-latency vocabulary; LUT/LUTFP32 -- mad-unit but no per-mod
;; effect audit yet; everything QSR (simulator returns
;; MissingSpecification for these opcode semantics).
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
