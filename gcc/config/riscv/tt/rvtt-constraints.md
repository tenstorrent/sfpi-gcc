;; Machine description for Tenstorrent SFPU Blackhole Intrinsics.
;; Copyright (C) 2022-2025 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)

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

;; Register constraints

(define_register_constraint "x" "SFPU_REGS"
  "Any of the SFPU Registers L0 - L3 (L0 - L7 for Wormhole)")

(define_register_constraint "Q0" "SFPU_REGS_L0"
  "SFPU Register L0")

(define_register_constraint "Q1" "SFPU_REGS_L1"
  "SFPU Register L1")

(define_register_constraint "Q2" "SFPU_REGS_L2"
  "SFPU Register L2")

(define_register_constraint "Q3" "SFPU_REGS_L3"
  "SFPU Register L3")

(define_register_constraint "Q4" "SFPU_REGS_L4"
  "SFPU Register L4")

(define_register_constraint "Q5" "SFPU_REGS_L5"
  "SFPU Register L5")

(define_register_constraint "Q6" "SFPU_REGS_L6"
  "SFPU Register L6")

(define_register_constraint "Q7" "SFPU_REGS_L7"
  "SFPU Register L7")

;; General constraints
;; used for unused register inputs
(define_constraint "z"
  "Constant vector"
  (match_code "const_vector"))

(define_constraint "M01U"
  "A 1-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 1)")))

(define_constraint "M02U"
  "A 2-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 3)")))

(define_constraint "M03U"
  "A 3-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 7)")))

(define_constraint "M04U"
  "A 4-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 15)")))

(define_constraint "M05U"
  "A 5-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 31)")))

(define_constraint "MP5U"
  "A 5-bit unsigned immediate for SFPU instruction modifiers."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 1, 32)")))

(define_constraint "M12S"
  "A 12-bit signed immediate for SFPU."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, -2048, 2047)")))

(define_constraint "M12U"
  "A 12-bit unsigned immediate for SFPU."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 4095)")))

(define_constraint "M13U"
  "A 13-bit unsigned immediate for SFPU."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 8191)")))

(define_constraint "M14U"
  "A 14-bit unsigned immediate for SFPU."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 16383)")))

(define_constraint "M16U"
  "A 16-bit unsigned immediate for SFPU load/store instruction offsets."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, 0, 65535)")))

(define_constraint "M16S"
  "A 16-bit signed immediate for SFPU load/store instruction offsets."
  (and (match_code "const_int")
       (match_test "IN_RANGE (ival, -32768, 32767)")))
