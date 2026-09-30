;; Tensix issue-cost model.
;; Copyright (C) 2026 Tenstorrent Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.
;;
;; The five issue classes mirror the Tensix top-level issue classes: Math,
;; Sfpu, Tdma, Cfg and Sync.  The reservations below model one issue slot
;; per class.  CFG execution-resource occupancy is not modeled, since no
;; RVTT pattern uses the B0-B8 opcode family.  The class vocabulary is
;; kept independent of the generic `type' attribute because several late
;; RVTT passes use TYPE_TENSIX as an ISA-membership test.

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

;; PRNG state is architectural state separate from LREG/CC/config/RWC.
;; Every executed SFPSTOCHRND advances it even in deterministic modes.
;; An unaudited instruction remains effect-opaque; a typed advancing word
;; must preserve its issue order relative to every other PRNG consumer.
(define_enum "xtt_prng_effect" [
  none
  advance
])
(define_enum_attr "xtt_prng_effect" "xtt_prng_effect"
  (const_string "none"))

;; ---------------------------------------------------------------------
;; Architectural-effect attributes.
;;
;; Every default is the refusing value, as xtt_latency_reorder defaults to
;; barrier: a pattern makes no effect claim until it is annotated.
;; Consumers reach these only through the query API in rvtt-effects.h; an
;; unannotated field makes the whole effect set opaque, and the consumer
;; must then leave the code unchanged.
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
;; unannotated (refusing), N+1 means latency N.  Decoded only by
;; rvtt_insn_effects.  The values hold for Wormhole and Blackhole; Quasar
;; patterns carry no entries.  Anything not listed keeps the default 0.
;;
;; An ISA page that requires software to keep the next instruction from
;; reading the result gives latency 1; a page without such a rule gives
;; latency 0.  Each entry also agrees with the ISA functional model and
;; with back-to-back dependent issue in hardware-validated kernels.
;;
;;   class (patterns)                                    latency
;;   mad family (mul/add/mad/muli/addi/mul24)               1
;;   lut (rvtt_sfplut, mod0 0/4; MAD sub-unit,              1
;;     scheduled as SFPMAD)
;;   load (sfpload_lv_int)                                  0
;;   loadi (sfploadi_lv_int, mod0 0-8 and 10)               0
;;   simple unit (mov/exexp/exman/abs/lz, register forms    0
;;     of setexp/setman/setsgn, shft variable mod 0/2 and
;;     immediate forms, and/or/xor, the annotated iadd_v
;;     and cast mods, plain and lane-predicated copies)
;;   iadd immediate (sfpiadd_i_lv_int constant              0
;;     alternatives, mods 1/5/9)
;;   divp2 (sfpdivp2_lv_int constant alternatives,          0
;;     mods 0/1)
;;   round unit (stochrnd i/v)                              0
;;   swap (sfpswap int/cst/indexed)                         0
;;   compare set-dest (sfpgt/sfple, BH mod1 == 8 only)      0
;;   store (rvtt_sfpstore_int; Dst write only)              0
;;   row step (rvtt_ttincrwc; RWC update only)              0
;;   SFPNOP                                                 0
;;
;; SFPLOAD's three-instruction rule (SFPLOAD.md) is a cross-unit Dst race,
;; not a result delay.  SFPSWAP's next-cycle rule is an acceptance stall,
;; recorded separately as xtt_next_slot_stall; a latency entry alone does
;; not make SFPSWAP a latency filler.  The compare set-CC forms (sfpgt/
;; sfple BH mod1 == 1, rvtt_sfpgt_cc, rvtt_sfple_cc) write only the lane
;; flags and have no result latency; their ordering is carried by the
;; `readwrite' CC effect.  The register-immediate (runtime-synthesized)
;; alternatives of iadd and divp2 keep the default, as does SFPLUT with
;; INDIRECT_VD (mod0 & 8), whose LReg[7]-indexed write target an operand
;; position mask cannot express.  A store writes no LREG, so it never
;; makes a register unproved in the replay reissue model; moving a store
;; across Dst accesses is proved separately (rtl-rvtt-schedule.cc).
;;
;; SFPLOADMACRO: the launch patterns (rvtt_sfploadmacro_int,
;; rvtt_sfploadmacro_hidden_int) stay unannotated because their effects
;; depend on the descriptor.  Instead the macro planner records the effect
;; interface of each launch it emits, derived from the descriptor it
;; synthesized (see rvtt-effects.h).  Every sub-unit event executes at
;; issue + 1 + delay and events enqueue unconditionally at issue
;; (SFPLOADMACRO.md), so launch issue is never operand-gated.  The recorded
;; result latency is the launch's settle distance (the greatest
;; SequenceBits delay plus the sub-unit's writeback latency above), so a
;; foreign consumer of a launch-written LREG waits for the macro to
;; settle.  Recorded launches write full lanes (CC-writing calendars are
;; not recorded), so the reissue model drops their write-side dependence
;; edge.  User-written launches (raw `.ttinsn' words) have no record and
;; keep every refusing default.
;;
;; Deliberately left unannotated:
;;   - SFPSHFT2: mod-dependent next-cycle register constraints.
;;   - SFPLUTFP32: its stalling logic keys on Mod1Mirror rather than Mod1,
;;     and its 3-entry, 6-entry and FP32 modes read different LREG sets;
;;     the SFPLUT entry does not transfer.  The FP16 six-entry selection
;;     rows (mod0 2/3/6/7) are instruction-selection capability only;
;;     rvtt_sfplutfp32_6r keeps every refusing default.
;;   - Auto-incrementing load/store address modes (positional Dst/RWC
;;     state).
;;   - Quasar opcodes (no specification is available).
;;   - The FPU face-transpose family (TTMOVD2B, TTMOVB2A, TTMOVB2D,
;;     TTMOVA2D, TTTRNSPSRCB, TTSTALLWAIT, TTRMWCIB0..3).  Their Dst
;;     addressing is positional (RWC + DEST_REGW_BASE + address mode),
;;     their data path depends on ALU format state, their wait gates depend
;;     on bank ownership and TTSTALLWAIT's stall extent on its condition.
;;     rvtt_insn_effects reports them opaque, so every consumer treats them
;;     as barriers.  Their operand checks are the encoding-field widths in
;;     rvtt-insn.def only; semantic legality (format protocol, bank grants)
;;     is the contract of the SFPI face-transpose interface.
;;
;; Mod-write crossing window.  A Dst access whose address mode carries a
;; live increment (a "mod-write") in place of an explicit TTINCRWC row
;; step turns the counter update into positional state that the frontend
;; retires.  A dependent access in the same continuous Tensix word stream
;; sees it in time.  An access reached across a loop backedge or a call
;; boundary issues after scalar control has drained the frontend, and may
;; fall inside the retirement window.  That window, W_drain, is 7 frontend
;; issue-slot words (XTT_MODWRITE_DRAINED_FRONTEND_WINDOW, a conservative
;; bound on a hardware fit of about 6.5 on Blackhole; Wormhole has the same
;; frontend and uses the same value).  Each crossing stalls
;; max (0, W_drain - iteration_slots), where iteration_slots counts every
;; issue-slot word of the iteration, scalar words included, Tensix words
;; at their slot counts and replay launches as one word.  An issue-time RWC
;; writer (an explicit TTINCRWC or typed face advance) between the last
;; mod-write and the backedge re-anchors the counter and clears the charge;
;; a group whose per-iteration rows cannot pay the charge refuses
;; (mod-write-dominates-rolled-body).
;;
;; Configuration programs are priced in frontend issue slots per execution
;; of the program.  A program in a loop preheader runs once per loop entry
;; and costs its word count.  A program elsewhere runs on every execution
;; of its region, after scalar control has drained the frontend: each
;; SETC16 occupies the two-cycle configuration issue class
;; (XTT_CONFIG_ISSUE_SLOTS) and the program also pays the drain residual
;; XTT_AUTOINCR_MIN_CONFIG_DISTANCE, against one slot saved per removed
;; TTINCRWC.  Groups that share one rewritten replay payload are priced
;; together, since payload coverage forces all of them to be transformed
;; or none.  A program hoisted out of a callee into its caller's loop entry
;; is preheader-class; the callee then pays only the call-boundary
;; crossing charge.  That is sound because ThreadConfig ADDR_MOD rows are
;; per thread and writable only by same-thread SETC16.
;;
;; No-exec record composition.  A replay capture recorded without
;; execution (TTREPLAY load=1 exec=0) whose recording window opens while a
;; mod-write may still be retiring hangs the Tensix: the recording
;; swallows the following words and the Vector Unit pipeline never
;; drains.  A capture separated from every mod-write by at least W_drain
;; issue words on every CFG path, or unreachable from it, is safe.
;; Frontend distance only bounds the retirement of an explicit row, whose
;; mod-write is itself a frontend word; a replay-delivered row's payload
;; is delivered asynchronously, so a group with any replay-delivered row
;; must refuse every reachable no-exec capture regardless of distance.
;; Captures that execute while recording (exec=1) are not affected.  The
;; window is exported as rvtt_modwrite_drained_frontend_window so that
;; every consumer prices the same quantity; issue-time RWC writers are not
;; mod-writes, and undecodable words earn no distance.
;;
;; Replay-state persistence.  The per-thread Replay Expander buffer
;; persists across function and basic-block boundaries within a kernel
;; launch and across the soft-reset boundary between kernel launches; only
;; a full device reset clears it, and launching from a cleared buffer hangs.
;; A record and its launches may therefore sit in different functions or
;; even different kernels.  A compiler-formed no-exec capture with a
;; Dst-store payload must therefore dominate every launch of its slot
;; span; otherwise its launches are replaced by inline payload copies.
;; Records written by the user remain the user's contract.
(define_attr "xtt_result_latency" ""
  (const_int 0))

;; Architectural next-slot acceptance stall (SFPSWAP.md: "on the next
;; cycle, the only instruction that the Vector Unit can accept is SFPNOP;
;; hardware will automatically stall the thread" otherwise).  This is a
;; structural fact distinct from result latency: the result is consumable
;; by the next accepted instruction, but that instruction issues one slot
;; late.  The reissue cost model charges one extra slot per occurrence;
;; schedulers that fill latency slots never use such an instruction as a
;; filler.  Default no.
(define_attr "xtt_next_slot_stall" "no,yes"
  (const_string "no"))

;; LREG writeback-port occupancy: the instruction's own port, the port
;; shared by the Simple and Round sub-units, or the MAD port (SFPSWAP).
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
;; unspec.  The authoritative answer is the macro capability table
;; (rvtt-macro-tables.cc); this attribute only lets region discovery skip
;; hopeless candidates early.
(define_enum "xtt_macro_encodable" [
  no
  yes
])
(define_enum_attr "xtt_macro_encodable" "xtt_macro_encodable"
  (const_string "no"))

;; Lane-local value-operation class.  `yes' marks a typed SFPU LREG value
;; operation with no Dst-memory, RWC-counter or configuration effect,
;; keyed per insn code and independent of the mod field.  Decoded only by
;; rvtt_lane_local_effects (rvtt-effects.h); it does not affect
;; rvtt_insn_effects or the rest of the effect family.  Default no
;; (refusing).
(define_enum "xtt_lane_local" [
  no
  yes
])
(define_enum_attr "xtt_lane_local" "xtt_lane_local"
  (const_string "no"))

;; Whether the lane-local operation's mod field can architecturally set
;; CC (the SFPEXEXP/SFPLZ/SFPIADD families), recorded conservatively as a
;; CC write regardless of the mod value.  Meaningful only under
;; xtt_lane_local yes (rvtt_lane_local_effects asserts that it is set
;; there); `unknown' is the refusing default.
(define_enum "xtt_cc_write" [
  unknown
  no
  yes
])
(define_enum_attr "xtt_cc_write" "xtt_cc_write"
  (const_string "unknown"))

;; Lane-gated consumer class.  `yes' marks an instruction whose LREG/Dst
;; writes are lane-gated and whose dataflow is lane-local (no value moves
;; between lanes): a reload feeding only such an insn is complete under
;; narrowed CC, and an RMW def by such an insn under narrowed CC stores
;; back only its enabled lanes.  Cross-lane ops
;; (SFPSWAP/SFPTRANSP/SFPSHFT2/SELECT/CONCAT), plain all-lanes copies,
;; SrcS stores, loadmacro forms and the zero-length LREG markers keep the
;; refusing default `no'.  The CC-gated predicated-assign copy is
;; recognized structurally by its UNSPECV_SFPASSIGN shape at the
;; consumer.  Decoded only by rvtt_lane_gated_consumer_p
;; (rvtt-effects.h).
(define_enum "xtt_lane_gated" [
  no
  yes
])
(define_enum_attr "xtt_lane_gated" "xtt_lane_gated"
  (const_string "no"))

;; Replay-hoist profitability constants.
;;
;; Units: hundredths of one Tensix instruction issue slot (centislots).
;; The x100 scaling expresses the RISC-delivery premium as an integer; the
;; issue reservations below stay in whole slots.
;;
;;   RISC_PUSH_X100 (123)   - one instruction word pushed by the RISC
;;                            core through the Tensix instruction FIFO, a
;;                            hardware-fitted 1.23x premium over a
;;                            replayed slot.
;;   REPLAY_SLOT_X100 (100) - one slot reissued by the Tensix replay
;;                            expander with no RISC involvement.
;;
;; The model prices one loop entry.  WORDS is the payload length and EXEC
;; its interlocked execution, EXEC_SLOTS * REPLAY_SLOT_X100, computed by
;; dependence tracking with xtt_result_latency and xtt_next_slot_stall.  A
;; consumed producer without a latency entry makes the payload
;; unpriceable (replay-reissue-latency-unproved).
;;
;;   deliver_body   = WORDS * RISC_PUSH          ; per-trip delivery
;;   deliver_record = (1 + WORDS) * RISC_PUSH    ; capture word + payload
;;   after          = max (RISC_PUSH, EXEC + TURNAROUND)
;;                                               ; one launch push; the
;;                                               ; expander reissues the
;;                                               ; payload
;;
;;   counted-loop capture (the body records nothing per trip):
;;     before = max (deliver_body, EXEC)
;;     record = deliver_record + RECORD_OVERHEAD
;;
;;   re-record body, execution-bound (EXEC >= deliver_record):
;;     before = EXEC + RECORD_OVERHEAD
;;     record = RECORD_OVERHEAD
;;
;;   re-record body, delivery-bound (EXEC < deliver_record):
;;     before  = deliver_record
;;     record  = deliver_record + RECORD_OVERHEAD
;;     surplus = launch_run * (EXEC - RISC_PUSH)
;;     hidden  = surplus >= deliver_record       ; then before = after
;;
;;   benefit = trips * (before - after) - record
;;   hoist iff benefit >= MIN_BENEFIT            ; trips provably constant
;;
;; RISC delivery is concurrent with execution, so a pass costs whichever
;; is larger, not their sum.  An execution-bound in-loop record pass costs
;; its interlocked execution plus the record engine's overhead, and the
;; hoisted record-only pass's delivery hides behind the execution backlog
;; such a loop accumulates.  A delivery-bound in-loop record pass does the
;; loop's real work inside its per-word delivery slack, so hoisting it
;; saves only the delivery premium while the hoisted record-only pass is
;; bought at full delivery price.
;;
;; launch_run is the number of contiguous sibling launches of the capture
;; in the loop body, measured on the body before the hoist; typed per-row
;; Dst-counter increments between launches are discounted when they will
;; be folded into address-mode increments after replay formation.  A run
;; of R launches executes for R * EXEC while delivering only R words; once
;; that surplus covers the record pass's delivery, the record pass is
;; hidden and the hoist's benefit degenerates to -record.  A single launch
;; of a delivery-bound payload can never hide its record pass
;; (WORDS * 100 - 123 < (1 + WORDS) * 123 for all lengths), so
;; counted-loop hoists are unaffected.  The term is part of the benefit,
;; not of the threshold, so -mtt-tensix-replay-hoist-min-benefit= cannot
;; force a hoist whose record delivery is hidden.  A contiguous run
;; created after the decision (by the launch-loop unroll below) exists
;; only in the hoisted program and must not re-price the hoist: the
;; preheader record pass runs before the run it feeds, not beside it.
;; Hoist first, then unroll; the unroll never feeds back.
;;
;; MIN_BENEFIT = 60 centislots (0.6 slot per loop entry).  Refusing leaves
;; the code unchanged and costs nothing; every calibration shape that lost
;; on hardware models negative, and 60 is about half the smallest benefit
;; that won, a margin standing in for the dynamic pipeline costs around a
;; hoisted record-only capture that this static model does not itemize.
;; -mtt-tensix-replay-hoist-min-benefit= (same units) overrides it.
;;
;; A completion-guarded caller (one that includes the drain in its timing)
;; does not assume that the hoisted record delivery hides behind the
;; loop's execution: an execution-bound re-record then charges record =
;; deliver_record + RECORD_OVERHEAD.  The other branches already charge
;; the full record.  A runtime trip count is not accepted in that mode
;; (record-hoist-completion-runtime-trips-unproven).
;;
;; Issue-side record-hoist pricing.  For a re-record body whose capture
;; window is iteration-invariant (every payload word uses only hard LREGs
;; and constants; record-hoist-variant-encoding otherwise), the hoisted
;; and unhoisted programs execute the identical word stream, so the
;; execution terms cancel and the delta is delivery only:
;;
;;   benefit = trips * (deliver_body - TURNAROUND)
;;             - (deliver_record + RECORD_OVERHEAD)
;;
;; TURNAROUND charges the launch boundary that the hoist adds.  This
;; pricing ignores that the per-trip record delivery may already be hidden
;; behind execution, so it is not the default.  Under the completion guard
;; the reissue audit is kept and the shared model above applies; for an
;; execution-bound re-record of W words the two differ by
;; trips * (300 - 123 * W), negative for every W >= MIN_SEQUENCE (4), so
;; the guard only refuses more.
;;
;; A runtime trip count is accepted when the record lands in the dedicated
;; preheader of a single-block loop, so that executing the record implies
;; at least one trip, and the two-trip benefit clears MIN_BENEFIT with a
;; positive per-trip saving; the worst case is then a single-trip exposure
;; of PUSH + RECORD_OVERHEAD + TURNAROUND, once per entry
;; (record-hoist-runtime-trips-break-even otherwise).
;;
;; A loop preserves the replay buffer if no word in it can record: raw
;; `.ttinsn' constants and volatile-stored words must provably not be
;; REPLAY (opcode 0x04, the decode in rvtt-raw-boundary.cc); stores
;; provably outside the instruction FIFO aperture are inert; launches
;; formed by the compiler are accepted and user-authored launches are not;
;; a MOP dispatch is accepted only when every MopCfg[0..8] word is a
;; function-programmed non-REPLAY constant stored before the loop and the
;; function contains no call, since the MOP expander may emit REPLAY
;; words.  The MopCfg aperture is write-only, so a scalar store-load
;; roundtrip never accesses it.  A volatile store whose address may lie in
;; the FIFO aperture could push any word and is not accepted.
;;
;; A no-exec record placed within W_drain issue words of a row that would
;; otherwise become a mod-write forces that row back to explicit
;; increments: a different executed stream that this delivery-only model
;; cannot price.  Such hoists refuse
;; (record-hoist-downstream-fallback-unprofitable), using the same
;; distance walk as the composition rule.  A Dst-store payload whose
;; record would land in a preheader inside another loop refuses
;; (noexec-rerecord-dststore-composition-unaudited).
;;
;; Two placement variants reuse the same pricing.  A first-trip peel moves
;; the loop's first trip into the preheader with the capture switched to
;; exec-while-record and turns every other record site into a launch
;; (proven constant trips >= 2, single-block loop).  A placement lift
;; moves a storeless no-exec record outward across enclosing loops that
;; preserve the replay buffer, up to the function entry.  The modeled
;; no-exec hoist benefit is a lower bound for both.
;;
;; Launch-loop unroll.  A counted loop whose body is reduced to replay
;; launches and typed Dst steps still delivers two scalar control words
;; per trip.  With a provable trip count it is replicated; the benefit,
;; trips * 2 * RISC_PUSH, is positive for every trips >= 2, so the only
;; gate besides the shape proof is code size:
;;
;;   XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS bounds the total delivered words
;;   of the unrolled run (trips * per-trip delivered words); 128 words are
;;   512 bytes of straight-line launches.
;;
;; Counted-capture peel.  A counted-loop capture that the plain hoist
;; refuses pays the payload's full re-delivery in the preheader.  Peeling
;; the first trip, with the capture switched to exec-while-record, avoids
;; that re-delivery:
;;
;;   benefit_peel = (trips - 1) * (before - after)
;;                  - (RISC_PUSH + RECORD_OVERHEAD)
;;
;; It is tried only when the plain hoist refuses
;; (counted-capture-peel-trips-unproven, counted-capture-peel-benefit).
;;
;; Window sizing under a hoisted record.  In-block formation prefers more
;; instances of a shorter window, because the record's own words stay in
;; the body.  With a hoisted record the per-trip cost is the launch words
;; alone, so the widest word-exact window that fits the free replay slots
;; is preferred, and a trailing run that is a prefix of the recorded
;; window is delivered as one partial launch: a REPLAY launch plays
;; ReplayBuffer[(Index + i) % 32] for i in [0, Count), independent of the
;; recorded length.  Candidates are compared by delivered words; launch
;; boundary cost is not modeled, which only favors fewer launches.
;;
;; Launch flatten.  A counted innermost delivery loop (typed replay
;; records and launches, fixed raw words, computed-word volatile stores,
;; typed SFPU builtins, its own scalar control) is completely unrolled
;; when its trip count is proven.  Per loop, the bounds are
;; XTT_REPLAY_LOOP_UNROLL_MIN_WORDS per trip and
;; XTT_REPLAY_LOOP_UNROLL_MAX_WORDS in total.
;;
;;   XTT_LAUNCH_FLATTEN_FN_BUDGET_WORDS bounds the sum of flattened totals
;;   (words * trips) over one function, since the per-loop bound cannot
;;   stop a function with many such loops from overflowing the TRISC code
;;   region.  1024 is four per-loop budgets; the word estimate
;;   overestimates real growth, as typed plumbing words fold away.  Loops
;;   refuse in program order once it is exhausted
;;   (launch-flatten-function-budget).
;;
;; These constants describe replay-unit delivery economics only.  They
;; are deliberately independent of any operation identity, opcode
;; calendar, coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_REPLAY_COST_RISC_PUSH_X100   123)
  (XTT_REPLAY_COST_REPLAY_SLOT_X100 100)
  (XTT_REPLAY_HOIST_MIN_BENEFIT      60)
  (XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS 128)
  ;; Interlock-aware reissue pricing:
  ;;
  ;;   TURNAROUND_X100 (70)  - per-launch replay reissue turnaround, a
  ;;     hardware-fitted machine constant like RISC_PUSH_X100 (about 0.7
  ;;     cycles per launch over payload lengths 4-27).
  ;;   RECORD_OVERHEAD_X100 (300) - per-record-pass engine overhead beyond
  ;;     word delivery.
  (XTT_REPLAY_COST_TURNAROUND_X100      70)
  (XTT_REPLAY_COST_RECORD_OVERHEAD_X100 300)
  ;; Replay-window loop-unroll request.  A counted single-block SFPU row
  ;; loop delivers its whole row every trip; unrolled, the replay former
  ;; captures it as one execute-while-record pass plus (factor - 1)
  ;; one-word launches per group.
  ;;
  ;;   LOOP_UNROLL_FACTOR (8) - the group size requested.  The per-group
  ;;     record amortization (W+1+(k-1))/k flattens beyond k = 8 while the
  ;;     re-record delivery per group stays W+1, so larger factors buy
  ;;     little and cost code size.  Production SFPU row kernels use
  ;;     `#pragma GCC unroll 8' for the same reason.
  ;;   LOOP_UNROLL_MIN_WORDS (4) - the replay former's MIN_SEQUENCE:
  ;;     smaller rows cannot form a window, so unrolling them is pure
  ;;     code growth.
  ;;   LOOP_UNROLL_MAX_WORDS (256) - code-size bound on factor times
  ;;     the estimated row words (the analog of
  ;;     XTT_REPLAY_LAUNCH_UNROLL_MAX_WORDS).
  (XTT_REPLAY_LOOP_UNROLL_FACTOR      8)
  (XTT_REPLAY_LOOP_UNROLL_MIN_WORDS   4)
  (XTT_REPLAY_LOOP_UNROLL_MAX_WORDS 256)
  (XTT_LAUNCH_FLATTEN_FN_BUDGET_WORDS 1024)
  ;; Round-chain interleave request.  A counted round loop whose
  ;; iterations are independent by dataflow (every loop-carried value's
  ;; recurrence is at most a single word-delivering update) is unrolled
  ;; by two so the post-RA cyclic list scheduler can interleave the two
  ;; copies' chains, one iteration ahead.
  ;;
  ;;   ROUND_INTERLEAVE_FACTOR (2) - the minimal depth that creates
  ;;     cross-copy slack; each further copy multiplies the live-range
  ;;     bound toward the eight-LREG file (the reason modulo variable
  ;;     expansion is not used on this target).
  ;;   ROUND_INTERLEAVE_MIN_WORDS (2) - the doubled body must present
  ;;     at least the scheduler's three-node interleave minimum
  ;;     (2 * 2 = 4 > 3); a one-word body has nothing to interleave.
  ;;   ROUND_INTERLEAVE_MAX_WORDS (128) - code-size bound on the doubled
  ;;     body (2 * estimated row words), half of LOOP_UNROLL_MAX_WORDS
  ;;     since the factor is fixed at two.
  (XTT_ROUND_INTERLEAVE_FACTOR      2)
  (XTT_ROUND_INTERLEAVE_MIN_WORDS   2)
  (XTT_ROUND_INTERLEAVE_MAX_WORDS 128)
])

;; ---------------------------------------------------------------------
;; Delivery-shape arbitration constants.
;;
;; For a counted single-block SFPU row loop with a proven trip count, the
;; delivery-shape solver (rvtt_bnb_delivery_shape in rvtt-bnb.cc, an exact
;; branch-and-bound) enumerates unroll factor U x payload rows R, predicts
;; for each U the shape that replay formation and the replay-hoist model
;; above will produce (using their constants for prediction only), prices
;; each predicted shape with the table below, and requests the cheapest
;; through the loop->unroll annotation.
;;
;;   WORD_X100 (100) - one delivered word (a RISC-pushed SFPU word or a
;;     scalar control word) on an issue-bound path costs about one cycle.
;;     Partial dual issue of scalar pairs (up to about 7%) is treated as
;;     model error.  The RISC_PUSH_X100 premium is deliberately not used:
;;     it belongs to the replay-hoist model, which is only mirrored.
;;   BOUNDARY_{LB,UB}_X100 (130, 180) - per-launch boundary cost on
;;     serial-chain replay windows, where the payload's dependence chain
;;     allows no overlap across launches.  Carried as an interval: every
;;     candidate is priced at both ends and a non-rolled request must
;;     clear the threshold at both.  TURNAROUND_X100 (70) instead averages
;;     over shapes whose boundaries partly overlap.
;;   record pass = (1 + payload_slots) delivered words at WORD_X100,
;;     added to the execution time, since record delivery is exposed.
;;     Loop control on a replay path is hidden under execution; on an
;;     issue-bound path it costs WORD_X100 per word.
;;   execution = payload slots at one cycle each, plus one slot per
;;     next-slot acceptance stall (SFPSWAP family).  Mad-family latency-1
;;     stalls are absorbed by chain-heavy payloads and are not charged.  A
;;     row member with no latency entry refuses
;;     (delivery-shape-exec-term-unaudited).
;;
;; The census runs on GIMPLE, before the RTL attributes are readable, and
;; uses these additional latencies: the CC family
;; (SFPSETCC/SFPENCC/SFPCOMPC/SFPPUSHC/SFPPOPC), SFPARECIP (Blackhole,
;; simple sub-unit) and the immediate forms of SFPSETEXP/SFPSETMAN/
;; SFPSETSGN have latency 0, as their ISA pages carry no next-cycle rule;
;; structured float compares lower to a mad-family member and are charged
;; one slot; integer compares lower to iadd, latency 0.  Per-mod
;; refinements are enforced by the RTL consumers; an inexact entry here
;; only shifts the modeled benefit, never correctness, since the
;; transformation is an unroll request.
;;
;; The solver does not model the mod-write crossing charge (see
;; XTT_AUTOINCR_SETUP_COST_X100 below) or wider record-hoist scopes.
;; Where the cheapest shape is rolled but the replay-hoist gate is
;; predicted to form a window anyway, the disagreement is only reported.
;;
;; CAPTURE_SLOTS (32) - the replay buffer's 32 entries (the bound split
;; by the S+L <= 32 MOP/replay co-ownership invariant, rvtt-mop-tables.h);
;; no payload may exceed it.
;;
;; MIN_BENEFIT (60) - as for the replay hoist: staying rolled leaves the
;; code unchanged and costs nothing, and the boundary interval adds
;; margin.
;;
;; These constants describe delivery economics only.  They are
;; deliberately independent of any operation identity, opcode
;; calendar, coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_DELIVERY_WORD_X100           100)
  (XTT_DELIVERY_BOUNDARY_LB_X100    130)
  (XTT_DELIVERY_BOUNDARY_UB_X100    180)
  (XTT_DELIVERY_CAPTURE_SLOTS        32)
  (XTT_DELIVERY_SHAPE_MIN_BENEFIT    60)
])
;; ---------------------------------------------------------------------
;; Dst auto-increment frontend constants.  The per-target auto-increment
;; capability entries are initialized from these.
;;
;;   AUTOINCR_MIN_CONFIG_DISTANCE (2) - issued words required between a
;;     SETC16 and its consumer: the configuration issue class is a
;;     two-cycle resource (rvtt_issue_cfg below), so two intervening
;;     words guarantee that the configuration write has retired.  Also
;;     the once-per-entry drain residual charged to a configuration
;;     program.
;;   MODWRITE_DRAINED_FRONTEND_WINDOW (7) - W_drain, the mod-write
;;     retirement window described with xtt_result_latency above.  Used
;;     for Blackhole and Wormhole (same frontend class); Quasar has no
;;     capability entry.
;;   CONFIG_ISSUE_SLOTS (2) - frontend issue slots occupied by one
;;     SETC16 configuration word, one more than the single-cycle class a
;;     removed TTINCRWC occupies; slot programs are priced in these units
;;     so that both sides of the comparison are frontend issue slots.
;;   AUTOINCR_SETUP_COST_X100 (0) - Dst auto-increment setup charge of a
;;     replay-delivered shape in the delivery-shape solver, read through
;;     rvtt-delivery-cost only.  Zero because the delivery table above
;;     already absorbs the SETC16 program in the once-per-group record
;;     delivery.
(define_constants [
  (XTT_AUTOINCR_MIN_CONFIG_DISTANCE      2)
  (XTT_MODWRITE_DRAINED_FRONTEND_WINDOW  7)
  (XTT_CONFIG_ISSUE_SLOTS                2)
  (XTT_AUTOINCR_SETUP_COST_X100          0)
])


;; ---------------------------------------------------------------------
;; Cross-row pairing constants (two consecutive rows of a counted loop
;; interleaved by the post-RA scheduler).
;;
;; PAIR_FACTOR (2) - exactly two consecutive iterations are paired: two
;; rows are the smallest interleave that fills a distance-1
;; producer/consumer adjacency and the row seam, the extra Dst row (+2
;; address units) stays inside the separator's own per-iteration frame,
;; and the doubled live set must still fit the eight-LREG file.  Wider
;; factors refuse.
;;
;; MIN_ROW_WORDS (4) - the replay former's MIN_SEQUENCE
;; (rtl-rvtt-replay.cc): pairing exists to speed up the execution of a
;; captured window, never to displace the capture.
;;
;; For the same reason the doubled row of 2n words must fit
;; XTT_DELIVERY_CAPTURE_SLOTS (32); otherwise the counted-loop capture
;; would stop firing and a rolled two-row loop would lose its replay
;; delivery.  The row separator stays explicit per launch and may be
;; folded into address modes afterwards exactly as in the single-row
;; stream.
;;
;; Dst disjointness: the row separator TTINCRWC (0, 2, 0, 0) advances the
;; Dst RWC by 2 address units, one 32-bit row in SFPLOAD/SFPSTORE
;; addressing.  Two accesses at constant offsets A and A+2 of one counter
;; frame with A == 0 mod 4 therefore touch disjoint footprints, and the
;; doubled separator (0, 4, 0, 0) advances exactly the two rows consumed
;; (4 is within the TTINCRWC immediate range).
;;
;; CC placement: interleaving is legal because the CC state is equal in
;; both orders.  A flat atom (a CC writer through the word-exact all-lanes
;; SFPENCC restore) stays indivisible and in its original interior order,
;; so every atom word executes under its own row's lane state, and every
;; position between atoms is all-lanes in both orders, provided the loop
;; entry is.  That is proved by walking back to the nearest reaching CC
;; writer, which must be the all-lanes restore, or to the function entry,
;; which the structured-CC lowering in gimple-rvtt-cc.cc leaves all-lanes.
;; Register-rename webs may root only in that ambient state
;; (crossrow-pairing-rename-cc-domain): a lane-predicated definition
;; renamed to a dead LREG would expose stale disabled-lane values.
;;
;; These constants describe structural bounds only.  They are
;; deliberately independent of any operation identity, opcode calendar,
;; coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_CROSSROW_PAIR_FACTOR           2)
  (XTT_CROSSROW_MIN_ROW_WORDS         4)
])


;; Preservation seeds for cross-row pairing.  A web rooted inside a CC
;; atom can still be renamed when a seed copy makes the fresh register F
;; lane-wise equal to the old register R.  The seed is SFPMOV mod 2 (the
;; bare-SET rvtt_sfpassign alternative), which writes every lane
;; regardless of the CC state.  It is placed directly after the last
;; definition of R preceding the root: before the atom's first CC writer
;; when the value enters the atom unwritten, or inside the atom after an
;; in-atom producer, where the seed joins the atom.  The root then writes
;; the same enabled lanes into F as it did into R, F's disabled lanes hold
;; what R's held (a read-modify-write root reads the equal F), and
;; rewriting the rest of the web through F keeps that equality, so an
;; all-lanes consumer or store observes the identical value.  A web whose
;; root is itself an SFPMOV mod 2 copy needs no seed.
;;
;; A seed is one issued word, counted in the same steady-state II model
;; and against the same XTT_DELIVERY_CAPTURE_SLOTS bound as every row
;; word; a seeded candidate is kept only if its II is strictly lower, so
;; no new constant is needed.
;;
;; Words carrying the next-slot acceptance stall (xtt_next_slot_stall,
;; the SFPSWAP family) take part with their result latency and an issue
;; occupancy of two slots, but count as one recorded word against the
;; capture bound.  Free LREGs are offered first to the webs of the copied
;; row, since breaking that row's serialization is the whole benefit.
;; Among ready items the one with the longer remaining critical path goes
;; first.  A doubled record is admitted only while the row words plus the
;; NOP pad sites still owed fit the capture slots.
;;
;; Pairing runs before the NOP inserter, which re-establishes every static
;; and dynamic delay contract over the final order (including the
;; Blackhole SFPMAD->SFPSWAP scoreboard erratum, xtt_dynamic_bug); pairing
;; only prices those NOPs.


;; ---------------------------------------------------------------------
;; MOP loop-delivery formation.  The replay-hoist constants above are
;; reused.
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
;; During replay playback RISC delivery is concurrent with execution, so
;; a row costs max (exec, 1.23 x delivered words), not their sum.  A
;; replay-launch row prices
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
;; so MOP formation relieves only delivery-bound rows:
;;
;;   benefit = N * max (0, d * PUSH - (len+k) * SLOT) - config_words * PUSH
;;
;; where the configuration block is serial delivery bought at full price
;; (it precedes the loop it feeds, so no execution hides it):
;;
;;   config_words = mop_sync (lui+sw, the reprogramming guard)
;;                  + config-base lui + one MMIO store per programmed
;;                  template register (flags, the launch word, and any
;;                  flags&2 step/NOP slots; li 1..2 per distinct nonzero
;;                  value, zero from x0) + MOP_CFG + MOP = 8..9 words for
;;                  the bare launch class, up to ~16 with three step
;;                  slots; computed exactly per candidate.
;;
;; Consequences: execution-bound rows (len >= 2 in a straight-line run)
;; model <= 0 and are left unchanged, since their launch pushes are
;; already hidden.  The profitable class is delivery-bound rows:
;; single-slot payload runs (len 1: 23 centislots per row, profitable from
;; N of about 51) and counted launch loops whose control words dominate
;; the row (d = 3: len 1 from N of about 5, len 2 from about 7, len 3 from
;; about 17).
;;
;; MIN_BENEFIT (60) - as for the replay hoist: refusal leaves the code
;; unchanged and costs nothing.
;;
;; The formed template lives in thread-shared registers that survive the
;; function's return, so formation also requires proof that no caller
;; template is live across the call (mop-caller-template-live-unproven):
;; a caller that programs its own template and launches it after the call
;; would otherwise expand the callee's template.  Saving and restoring
;; the template is impossible at any price because the MOP configuration
;; registers are write-only from the RISC (rvtt-mop-tables.h).
;;
;; These constants describe MOP delivery economics only.  They are
;; deliberately independent of any operation identity, opcode calendar,
;; coefficient value, or instruction-word fingerprint.  MOP capability
;; facts (encodings, register file, S+L <= 32 replay co-ownership) live
;; in rvtt-mop-tables.h.
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
;; Constant residency and rematerialization pricing.
;;
;; Both mechanisms move SFPLOADI materializations, so they price in the
;; same delivered-word units as the replay model above (RISC_PUSH_X100).
;;
;;   materialize (fp32)   = 2 pushed words  (SFPLOADI lo + hi pair; the
;;                          16-bit-encodable forms are 1 word -- the
;;                          conservative bound prices 2)
;;   PRGM programming     = 3 pushed words once (2 staging SFPLOADI +
;;                          1 SFPCONFIG)
;;   PRGM read-back       = 0 allocatable-LREG pressure and 0 delivered
;;                          words (the constant register is used directly
;;                          as the consumer's operand)
;;
;; Residency of an in-loop invariant materialization therefore pays for
;; itself when 2 * trips >= 3, i.e. from two proven trips; the loop class
;; requires a structural proof of a second trip (trip-count-unproven).  An
;; out-of-loop materialization executes once either way, so parking it
;; saves no delivered words; its value is one freed LREG, and the
;; pressure class applies only while the liveness model exceeds the
;; eight-LREG file.  Rematerialization reissues the materialization per
;; use: 2 pushed words per use (about 2.46 slots outside launch runs at
;; RISC_PUSH_X100, free inside a run, where delivery is concurrent).
;; Residency (free reads) is therefore preferred, and rematerialization is
;; used only under residual over-pressure, where the alternative is a
;; register-allocation failure.
;;
;; Residency peel: when the loop body contains a lowered v_if region
;; ending in the all-lanes SFPENCC, the programming point is created by
;; peeling the first iteration onto the entry edge.  The programming then
;; executes after the peeled copy's own all-lanes SFPENCC, the state in
;; which every later iteration begins.  The peeled iteration's words are
;; pushed by the RISC instead of replayed, so:
;;
;;   save  = SLOT * candidate_words        per iteration after the first
;;   cost  = PUSH * (candidate_words + n_SFPCONFIG)          ; programming
;;         + (PUSH - SLOT) * body_words                      ; peel class
;;                                                           ; change
;;   fire  when (trips - 1) * save >= cost, with trips proven by bounded
;;   forward evaluation of the loop's own scalar control (never profile
;;   data); the refusal is peel-trip-count-unproven.
;;
;; body_words is a delivered-word proxy over the GIMPLE body (one word
;; per typed RVTT call or raw word, the immediate-encoding word count for
;; materializations, zero for scalar statements, which are RISC-side and
;; concurrent).  Overestimating body_words only raises the required trip
;; count; it never admits an unpriced transformation.
;;
;; MAD pair: a constant already hoisted out of the loop executes once
;; either way, so the per-iteration saving is normally zero.  But when it
;; is the shortened SFPLOADI FLOATB form feeding one half of a single-use
;; mul+add pair in the loop, the muli/addi immediate folds (which run in
;; preference to mul,add->mad) consume it and the loop pays MUL+ADDI
;; (2 words) instead of MAD (1 word) every iteration.  Placing exactly
;; those constants in PRGM registers re-offers the pair to the mad
;; combine:
;;
;;   cost  = PUSH * 1 per claim once   (the SFPCONFIG; the staging
;;                                      materialization replaces the
;;                                      hoisted one word for word)
;;   save  = 1 pushed word per iteration (MAD for MUL+ADDI)
;;
;; A proven single trip is a wash and refuses (trip-count-single-trip); a
;; runtime trip count is accepted because the programming point is the
;; hoist's own execution point.  Placement is all-or-none per pair: a
;; half-claimed pair pays its programming word while the remaining
;; immediate fold still blocks the mad rule (madpair-prgm-exhausted).
;;
;; Pressure park: a candidate at or after the body's first CC writer is
;; admitted when every consumer is in the lane-predicated set
;; (postcc-phi-use, consumer-lane-discipline-unaudited otherwise).  The
;; parked register holds the constant in every lane, a superset of the
;; lanes the predicated SFPLOADI wrote, so every originally defined lane
;; is unchanged.  When the three PRGM registers are taken, a candidate may
;; instead be hoisted to the same programming point as a plain LREG live
;; range, at no word cost, charged one register against the function-wide
;; pressure model (lreg-file-exhausted beyond eight).
;;
;; Store sources: SFPSTORE reads only L0-L11, so a constant parked in
;; L12-L14 costs a per-use SFPMOV copy at each store,
;; one word per row inside a loop.  Per store-consumed loop candidate:
;;
;;   parked                1 word/row (the copy) + programming words
;;   LREG-tier hoisted     0 words/row, no programming words,
;;                         1 LREG of function-wide pressure
;;   left in the loop      1 word/row (short immediate) or 2 words/row
;;                         (wide constant)
;;
;; so such a candidate goes to the LREG tier first and falls back to the
;; PRGM park, which is never worse than leaving it in the loop.

;; Cross-loop placement of peel-class constants.  The residency peel
;; exists only to create an all-lanes programming point inside a loop
;; whose body writes CC, and it re-executes on every iteration of each
;; enclosing loop.  When the enclosing loops contain only structured,
;; typed CC atoms (whose whole effect is the lane-enable state plus an SSA
;; definition, out of reach of the parked constant register) and no CC
;; write reaches the outer preheader, the constants are programmed there
;; as plain loop-class residency and no peel is created:
;;
;;   peel                per enclosing entry: body_w peeled words +
;;                       (sum_w + nprog) programming words
;;   lifted              once: (sum_w + nprog) programming words; no
;;                       peeled words; in-loop candidates read the parked
;;                       register on every iteration
;;
;; The lift is cheaper than the peel wherever both apply and follows the
;; plain loop class's trip policy.  The parked load writes every lane
;; under the proven ambient state, and no CC-writing statement is
;; inserted, deleted or reordered.  Refusals: crossloop-cc-atom-unproven
;; (a CC writer outside the typed set), crossloop-cc-peel-entrycc-unproven
;; (a CC write reaches the lifted preheader).
;;
;; ---------------------------------------------------------------------
;; Cross-call configuration prefix and residency.  A callee prefix pair
;; (an all-constant immediate materialization whose single consumer
;; writes a programmable-constant register, 11..14) can be programmed
;; once per proven caller-loop entry, ahead of the cross-call contract
;; loads, and deleted from the callee: it saves 2 words per call at the
;; cost of the same 2 words once per loop entry.  The caller must deliver
;; no SFPCONFIG-class word and its MOP templates must contain no
;; configuration word; LReg[8..14] reads never occupy an allocatable LREG.
;; The contract entry is lifted across enclosing caller loops level by
;; level while each level passes the same proofs; a failing level stops
;; the walk.  No new constant.

;; ---------------------------------------------------------------------
;; Dual-bank binding caps for pinned chains in register allocation.  These
;; are structural bounds, not prices: an exceeded cap is a named refusal
;; that keeps the existing allocation.
;;
;;   maximum operands = 24          rvtt_sfptransp8_int, with 16 operands
;;                                  (8 exact-pinned outputs + 8 matching
;;                                  inputs), is the largest exact-register
;;                                  pattern in rvtt.md
;;                                  (dualbank-pin-shape-unmodeled).
;;
;;   maximum alternatives = 16      rvtt_sfpswap_indexed_int has 12
;;                                  (the ordered value pairs with
;;                                  companion == value + 4); every other
;;                                  pinning pattern has one.
;;
;;   search budget = 4096           depth-first alternative applications
;;                                  before the search refuses.
;;                                  Forced-color pruning keeps anchored
;;                                  chains near linear; the budget bounds
;;                                  the exhaustion of infeasible
;;                                  instances.
;;
;; Inter-row drain for paired macro windows.  The minimal spacing between
;; consecutive rows is derived from the same architectural facts as the
;; boundary and backedge drain proofs (retire-before-issue), made exact by
;; per-event footprints:
;;
;;   F1  Hosted-event operands.  For Simple/MAD/Round events the launch VD
;;       joins the reads (Insn.VB or Insn.VC := VD) and the result
;;       register is the launch VD or LReg[16] per the SequenceBits VD16
;;       bit; the scheduled store's value register is LReg[16] (0x40), the
;;       template's own VD (0x80), or the launch VD (SFPLOADMACRO.md).
;;   F2  Dst row footprint.  An access at constant address A touches lane
;;       rows [A & ~3, (A & ~3) + 3]; the 32-bit format class
;;       (mod0 3/4/7/9/12) maps lane row r to physical rows adj32(r) and
;;       adj32(r)+8 with adj32(r) = ((r & 0x1F8) << 1) | (r & 0x207);
;;       config-resolved classes (mod0 0) take the union of both layouts
;;       (SFPLOAD.md "Row = (Addr & ~3) + (Lane / 8)"; Dst.md).
;;   F3  Column parity.  Column = (Lane & 7) * 2, plus 1 when
;;       ((Addr & 2) || DEST_{RD,WR}_COL_EXCHANGE), so two accesses whose
;;       addresses differ in bit 1 touch disjoint columns provided the
;;       column-exchange LaneConfig bits hold their default; any local
;;       writer that could reach SFPCONFIG destination 15 refuses.
;;   F4  Mod0 10 (INT32_ALL) adds the Sp counter into the address and
;;       decrements Sp, breaking the shared-base distance arithmetic, and
;;       refuses.
;;   F5  The row-to-row distance is the schedule's absorbed typed stride:
;;       the absorbing access holds the row's last issue slot, and
;;       SFPLOAD/SFPSTORE resolve their own address before
;;       ApplyPartialAddrMod runs.  Pending stores latch their Dst row at
;;       launch, so later counter advances never move them.
;;   F6  Same-cycle events on distinct sub-units with disjoint data are
;;       allowed (the Vector Unit executes one instruction per sub-unit
;;       per cycle, SFPLOADMACRO.md); the same sub-unit, or overlapping
;;       data whose order the cycle arithmetic does not preserve, refuses.
;;       A pending store reads the live lane mask, so any CC write inside
;;       its horizon conflicts.
;;
;;   F5' When the single absorbing advance rides an earlier word p, the
;;       accesses carried at or before p resolve at the row-entry counter
;;       (phase 0) and later ones one stride later (phase 1): each access
;;       latches its Dst address at its carrying word, and an
;;       SFPLOADMACRO-hosted event's Dst row is computed at launch and
;;       never re-resolved.  Rebasing every footprint by phase * stride
;;       restores the uniform distance arithmetic; with the absorber last
;;       every phase is 0.  An event with no provable carrying word
;;       refuses (window-pairing-stride-unproven).
;;
;; No constant is introduced: every distance derives from the schedule's
;; delays, cross-checked against the descriptor's own SequenceBits
;; (window-pairing-delay-unproven on mismatch).  Whole-word programs leave
;; DELAY_UNKNOWN in the schedule and therefore refuse.

;; ---------------------------------------------------------------------
;; Raw words in replay and issue counting.
;;
;; A canonical single-constant `.ttinsn' asm word (the TTI_ macro shape)
;; is exactly one 32-bit Tensix word: it occupies one replay-buffer slot
;; when recorded (the REPLAY load loop stores every incoming word, without
;; opcode filtering) and one frontend issue slot.  Counting raw words this
;; way changes no classification: they remain foreign to every rewrite.
;;
;; A replayed SFPLOAD, SFPSTORE or INCRWC executes exactly like the inline
;; word.  RWC effects (INCRWC, ApplyPartialAddrMod) are cumulative counter
;; adds per execution, and the ADDR_MOD increment values in ThreadConfig
;; are not reset by a launch.  A counter walk through replayed accesses is
;; therefore linear in the number of executions, and the only possible
;; skew is a mismatch between the executions of a carried access and the
;; removed explicit increments.  Payload coverage rules that out: every
;; execution site (launch or executing capture) of a rewritten payload
;; must be a transformed row.
;; ---------------------------------------------------------------------

;; ---------------------------------------------------------------------
;; Replay formation after Dst auto-increment folding.
;;
;; Explicit per-row TTINCRWC separators are replay barriers (xtt_replay
;; barrier), so a row body that carries its increment in the address mode
;; is word-uniform, and therefore capturable, only after folding.
;; Capturing the increments instead costs more than it saves (raw sync
;; words stay outside windows, plus boundary and epilogue cost).
;; Formation can therefore be deferred to run once, after folding: an
;; earlier run would consume replay slots on small windows and starve the
;; larger post-fold ones.  Nothing is lost, because folding only removes
;; barrier words and retargets the modifier operands of the rows they
;; separated, so every run capturable before folding is capturable after.
;;
;; Restricted to word-exact replacements, formation only inserts words
;; into the delivered stream: in a block, one exec-while-record word
;; before the first clone and one launch per other clone, whose expansion
;; re-emits exactly the clone's words; with a hoisted record, words
;; ingested (not executed) in the preheader and one launch per clone.
;; Consequently carried RWC walks are preserved, positionally discharged
;; delay shadows only grow, and no payload, configuration or modifier
;; operand is edited.  Mechanisms that deliver words other than the
;; replaced site's own (isomorphic-run conversion under a register
;; rename, the first-trip peel) refuse carried members.  The un-hoist
;; rules for no-exec captures apply unchanged.  No new constant.
;;
;; ---------------------------------------------------------------------
;; Pre-peel placement of parked constants.  When a loop is peeled, a
;; constant parked at the post-peel programming point would be
;; materialized twice per loop entry (once in the peeled copy, once by
;; the park).  The park materialization is instead placed at the head of
;; the peel block and the peel's duplicate erased, provided the CC state
;; entering the peel block is all-lanes: every backward path reaches the
;; function entry or a block ending in the all-lanes SFPENCC before any
;; CC-affecting statement (typed CC setters, SFPPUSHC/SFPPOPC).  Calls
;; and raw asm are CC-transparent here only because the transformation
;; requires the translation unit's raw-word audit to pass.  Otherwise the
;; post-peel placement is kept (park-prepeel-ambient-unproven).  No new
;; constant.
;;
;; ---------------------------------------------------------------------
;; Entry-ambient enable and immediate-delta rows.
;;
;; A macro region that needs all lanes but has no typed enable, no proven
;; trailing enable and no in-row restore proves the function-entry
;; all-lanes state by a kill-aware backward walk from the configuration
;; point (kill: the word-exact all-lanes SFPENCC; calls, asm and opaque or
;; unrecognized insns are dirty) and then emits the canonical all-lanes
;; enable (rvtt_sfpencc_all_lanes) at the prefix head.  This rewrites a
;; state already proved, so it is a machine-state no-op, and the
;; configuration prefix cost already counts that word.  The refusal is
;; all-lanes-proof-missing.
;;
;; Rows equal to the first row up to one common typed Dst-address
;; immediate delta are admitted when the absolute progression is uniform
;; (row k's accumulated separator advance plus its delta equals k*S, and
;; the total separator advance equals rows*S), the same +S-per-row
;; progression a separator-carried shape expresses.  Such regions
;; require the absorbed-stride calendar (imm-stride-not-absorbed), the
;; address rewrite of every offset row's Dst accesses is checked first
;; (imm-stride-rewrite-unproven), and emission normalizes explicit copies
;; to the first row's base while the absorbed auto-increment supplies the
;; advance.  The explicit-side cost still extrapolates from the first row,
;; which undercounts its separators and so errs toward refusal.
;;
;; ---------------------------------------------------------------------
;; Run pricing with a hoisted configuration prefix.
;;
;; A straight-line macro run is profitable when
;;     config_prefix + rows*ii + drain  <  rows * explicit_row_words
;; (all in issue words).  When the cross-call init hoist removes the
;; configuration prefix from the callee, the prefix executes once per
;; caller-loop entry instead.  With E caller-loop entries and B in-loop
;; call executions from the caller's profile counts (B >= E > 0), the
;; comparison becomes, cross-multiplied without rounding,
;;     config_prefix * E + (rows*ii + drain) * B
;;         <  rows * explicit_row_words * B.
;; Each run of a multi-run region still charges the full prefix.  Without
;; the hoist, or without a usable profile weight, the per-run comparison
;; is used unchanged.  The hoist proof is run first without committing,
;; and a formation priced with the amortization refuses
;; (init-hoist-commit-diverged) should the committing run diverge from it.
;; The placement arbitration likewise weighs the formed side's prefix
;; push words (XTT_REPLAY_COST_RISC_PUSH_X100 each) by E and per-call
;; words by B.  The per-execution configuration pricing above is separate:
;; it covers SETC16 slot programs that a non-hoisted callee re-emits on
;; every call.
;; ---------------------------------------------------------------------
