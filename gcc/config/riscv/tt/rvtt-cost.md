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
