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
