;; Machine description for Tenstorrent SFPU Intrinsics -- lane condition codes.
;; Copyright (C) 2022-2026 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)
;; Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 3, or (at your option)
;; any later version.

;; GCC is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; Lane condition-code words: sfpsetcc, sfpencc, sfpcompc and the
;; CC stack (sfppushc / sfppopc).
(define_insn "rvtt_sfpsetcc_i"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI   0 "const_int_operand" "n")
     (match_operand:SI   1 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\tL0, %1, %0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

;; Audited so predicated shapes name their CC capability at the
;; predicate write instead of dissolving into an opaque boundary; since
;; the CC-template extension this is the select calendar's
;; predicate-definition event (region discovery admits it as a row
;; member; unproven CC forms still refuse by name).
(define_insn "rvtt_sfpsetcc_v"
  [(unspec_volatile:XTT32SI [
     (match_operand:XTT32SI 0 "reg_or_cstlreg_operand"  "xrxc")
     (match_operand:SI   1 "const_int_operand" "n")
     ] UNSPECV_SFPSETCC)]
  "TARGET_XTT_TENSIX"
  "SFPSETCC\t%x0, 0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "2")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   ;; Lane-gated consumer (typed-effect attributes).
   (set_attr "xtt_lane_gated" "yes")])

(define_insn "rvtt_sfpencc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     ] UNSPECV_SFPENCC)]
  "TARGET_XTT_TENSIX"
  "SFPENCC\t%1, %0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "write")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")])

  ;; Effect audit (2026-08-18, Lane BM; three sources): pure lane-flag
  ;; state, no register result -- [ISA] SFPCOMPC.md: reads and rewrites the
  ;; lane flags / flag stack only; [SIM] the reference simulator's TENSIX_EXECUTE_SFPCOMPC
  ;; (TT_VERSION <= 1) touches cc/cc_en/cc_stack only, no LREG, config,
  ;; or counter state; [HAND] the hardware-proven hand sign/is*-family
  ;; calendars issue these back-to-back inside recorded replay payloads.
  ;; Latency entry 1 (= 0 slots): there is no register result to wait
  ;; on; the flag update is architecturally visible to the next issued
  ;; instruction.
(define_insn "rvtt_sfpcompc"
  [(unspec_volatile:XTT32SI [
     (const_int 0)
     ] UNSPECV_SFPCOMPC)]
  "TARGET_XTT_TENSIX"
  "SFPCOMPC"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

  ;; Effect audit (2026-08-18, Lane BM; three sources): pure lane-flag
  ;; state, no register result -- [ISA] SFPPUSHC.md: reads and rewrites the
  ;; lane flags / flag stack only; [SIM] the reference simulator's TENSIX_EXECUTE_SFPPUSHC
  ;; (TT_VERSION <= 1) touches cc/cc_en/cc_stack only, no LREG, config,
  ;; or counter state; [HAND] the hardware-proven hand sign/is*-family
  ;; calendars issue these back-to-back inside recorded replay payloads.
  ;; Latency entry 1 (= 0 slots): there is no register result to wait
  ;; on; the flag update is architecturally visible to the next issued
  ;; instruction.
(define_insn "rvtt_sfppushc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPUSHC)]
  "TARGET_XTT_TENSIX"
  "SFPPUSHC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

  ;; Effect audit (2026-08-18, Lane BM; three sources): pure lane-flag
  ;; state, no register result -- [ISA] SFPPOPC.md: reads and rewrites the
  ;; lane flags / flag stack only; [SIM] the reference simulator's TENSIX_EXECUTE_SFPPOPC
  ;; (TT_VERSION <= 1) touches cc/cc_en/cc_stack only, no LREG, config,
  ;; or counter state; [HAND] the hardware-proven hand sign/is*-family
  ;; calendars issue these back-to-back inside recorded replay payloads.
  ;; Latency entry 1 (= 0 slots): there is no register result to wait
  ;; on; the flag update is architecturally visible to the next issued
  ;; instruction.
(define_insn "rvtt_sfppopc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     ] UNSPECV_SFPPOPC)]
  "TARGET_XTT_TENSIX"
  "SFPPOPC\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "safe")
   (set_attr "xtt_subunit" "simple")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "readwrite")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "none")
   (set_attr "xtt_result_latency" "1")])

