;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2022-2025 Tenstorrent Inc.
;; Originated by Ayonam Ray (ayonam@helprack.com)

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

(define_predicate "vec0_operand"
  ;; close enough
  (match_code "const_vector"))

(define_predicate "reg_or_const_int_operand"
  (ior (match_operand 0 "register_operand")
       (match_code "const_int")))

(define_predicate "reg_or_vec0_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "vec0_operand")))
