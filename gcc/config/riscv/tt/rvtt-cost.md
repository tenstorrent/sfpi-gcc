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
;; Units: one Tensix instruction issue slot (the same unit as the
;; issue reservations below; the SFPU issue reservation is 1).
;;
;; A replay recording pass -- with or without execution -- pushes every
;; captured slot through the Tensix instruction FIFO exactly once, so both
;; the per-slot re-record cost and the per-slot record-only issue cost are
;; one slot.  A capture instruction itself costs its own issue slot plus one
;; slot of replay-unit setup occupancy.  A playback launch is a single issue
;; slot.
;;
;; The profitability model in rtl-rvtt-replay.cc is
;;
;;   re_record   = CAPTURE + length * SLOT_RECORD   ; one in-loop recording
;;   record_only = CAPTURE + length * SLOT_ISSUE    ; hoisted preheader pass
;;   benefit     = (trips - 1) * re_record          ; recordings removed
;;                 - record_only                    ; capture pass added
;;                 - trips * LAUNCH                 ; launches added
;;   hoist iff benefit >= MIN_BENEFIT               ; trips provably constant
;;
;; MIN_BENEFIT calibration (2026-08-16 Blackhole same-source silicon A/Bs;
;; each ran the identical source and inputs with only compiler flags
;; differing; TILE_LOOP mean(MATH_ISOLATE) cycles/tile, three fresh
;; processes per selector):
;;
;;   shape                          trips length  modeled benefit  silicon
;;   counted-loop payload             8     24         148         -9.83%
;;   repeated-sequence (17 slots)     3     17          16         +1.81%
;;   repeated-sequence (31 slots)     3     31          30         +2.30%
;;
;; Structurally identical CRAQ-green shapes with 28-slot and 32-slot
;; captures at 3 trips model at 27 and 31 and belong to the same losing
;; class.  The dynamic-pipeline costs Blackhole adds around a hoisted
;; record-only capture are not itemized by this static model, so the
;; threshold must sit strictly above every member of the measured losing
;; class (max modeled benefit 31) and strictly below the measured winning
;; shape (modeled benefit 148).  64 keeps a >= 2x margin over the losing
;; class and >= 2x headroom under the winner; a marginal shape is refused
;; because refusal is byte-identical code and costs nothing.  The
;; -mtt-tensix-replay-hoist-min-benefit= option overrides this table value
;; for experimentation.
;;
;; These constants describe replay-unit issue economics only.  They are
;; deliberately independent of any operation identity, opcode calendar,
;; coefficient value, or instruction-word fingerprint.
(define_constants [
  (XTT_REPLAY_COST_CAPTURE       2)
  (XTT_REPLAY_COST_SLOT_ISSUE    1)
  (XTT_REPLAY_COST_SLOT_RECORD   1)
  (XTT_REPLAY_COST_LAUNCH        1)
  (XTT_REPLAY_HOIST_MIN_BENEFIT 64)
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
