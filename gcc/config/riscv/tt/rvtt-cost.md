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
;; Deliberately UNAUDITED (refusing): SFPSWAP -- its hazard is the
;; structural next-slot rule (only SFPNOP is accepted in the following
;; cycle, SFPSWAP.md), not a consumable result latency, so it must
;; never become a fill target; SFPSHFT2 -- mod-dependent next-cycle
;; register constraints (SFPSHFT2.md) outside the single-latency
;; vocabulary; SFPGT/SFPLE -- no proven sub-unit placement (absent from
;; the S1 legality table); LUT/LUTFP32 -- mad-unit but no per-mod
;; effect audit yet; everything QSR (simulator returns
;; MissingSpecification for these opcode semantics).
(define_attr "xtt_result_latency" ""
  (const_int 0))

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
;; The profitability model in rtl-rvtt-replay.cc prices one loop entry:
;;
;;   deliver = (1 + length) * RISC_PUSH_X100     ; capture word + payload
;;   execute = length * REPLAY_SLOT_X100
;;   after   = max (RISC_PUSH_X100, execute)     ; one launch push; the
;;                                               ; replay unit reissues
;;                                               ; the payload
;;   surplus = launch_run * (execute - RISC_PUSH_X100)
;;                                               ; execution surplus of the
;;                                               ; body's longest run of
;;                                               ; final-stream-contiguous
;;                                               ; sibling launches of the
;;                                               ; same buffer
;;   hidden  = surplus >= deliver                ; the record pass's
;;                                               ; delivery streams into
;;                                               ; that execution shadow
;;   before  = hidden ? after : deliver          ; in-loop record WITH
;;                                               ; execution: the payload
;;                                               ; does the loop's real
;;                                               ; work while recording,
;;                                               ; overlapped under the
;;                                               ; dominant delivery cost
;;                                               ; (RISC_PUSH >= REPLAY_SLOT)
;;                                               ; -- unless hidden, when
;;                                               ; removing it relieves
;;                                               ; nothing per trip
;;   benefit = trips * (before - after) - deliver ; minus the added
;;                                               ; record-only preheader
;;                                               ; pass: 1 + length words
;;                                               ; delivered, nothing
;;                                               ; executed
;;   hoist iff benefit >= MIN_BENEFIT            ; trips provably constant
;;
;; The context term (launch_run) is computed from the candidate's own
;; statically known structure: the number of sibling occurrences of the
;; capture in the loop body and the delivered words between them, where a
;; typed per-row Dst-counter increment separator is discounted when the
;; Dst auto-increment pass -- which runs after replay formation and
;; absorbs exactly those separators around replay launches -- is enabled.
;; A contiguous run of R launches occupies the issue plane for R * execute
;; centislots while delivering only R * RISC_PUSH_X100 words; once its
;; surplus covers the record pass's delivery, that delivery is hidden
;; under execution and the hoist's true benefit degenerates to -deliver
;; (the preheader record-only pass is pure cost).  A single launch can
;; never hide a record pass (length*100 - 123 < (1+length)*123 for every
;; length), so counted-loop hoists -- one clone per trip, launches always
;; separated across trips by the loop-control delivery -- and every other
;; single-instance shape are arithmetically unaffected, with byte-identical
;; decisions and dump numbers.  The saturation term is part of the modeled
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
