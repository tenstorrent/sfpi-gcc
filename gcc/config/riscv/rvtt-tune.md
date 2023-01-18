;; TT tune profile
;; Copyright (C) 2022 Free Software Foundation, Inc.
;; Contributed by Paul Keller (pkeller@tenstorrent.com)

;; This file is part of GCC.

;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.

;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.

;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.


;; Profile for grayskull and wormhole b1 CPUs

(define_insn_reservation "rvtt_l1_load_ptr" 12
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "load,fpload")
       (match_test "rvtt_l1_load_p(PATTERN(insn))"))
  "alu")

(define_insn_reservation "rvtt_reg_load_ptr" 4
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "load,fpload")
       (match_test "rvtt_reg_load_p(PATTERN(insn))"))
  "alu")

(define_insn_reservation "rvtt_alu" 1
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "unknown,const,arith,shift,slt,multi,auipc,nop,logical,move"))
  "alu")

(define_insn_reservation "rvtt_load" 3
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "load,fpload"))
  "alu")

(define_insn_reservation "rvtt_store" 1
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "store,fpstore"))
  "alu")

;;; Note: imul stalls issue during its latency window
(define_insn_reservation "rvtt_imul" 1
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "imul"))
  "imuldiv")

;;; Note: idiv stalls issue during its latency window
(define_insn_reservation "rvtt_idivsi" 1
  (and (eq_attr "tune" "rvtt_b1")
       (eq_attr "type" "idiv"))
  "imuldiv")
