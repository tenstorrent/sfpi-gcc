;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2011-2020 Free Software Foundation, Inc.
;; Contributed by Ayonam Ray (ayonam@helprack.com)
;; Based on RISC-V target for GNU compiler.

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

(define_predicate "const_setcc_z_or_nez"
  (match_code "const_int,const_wide_int")
{
  return (GET_CODE(op) == CONST_INT) && (INTVAL(op) == 2 || INTVAL(op) == 6);
})

(define_predicate "const_iadd_i_nosetcc"
  (match_code "const_int,const_wide_int")
{
  return (GET_CODE(op) == CONST_INT) && (INTVAL(op) == 5);
})

(define_predicate "const_iadd_i_setcc"
  (match_code "const_int,const_wide_int")
{
  return (GET_CODE(op) == CONST_INT) && (INTVAL(op) == 1 || INTVAL(op) == 9);
})

(define_predicate "const_iadd_v_nosetcc"
  (match_code "const_int,const_wide_int")
{
  return (GET_CODE(op) == CONST_INT) && (INTVAL(op) == 4 || INTVAL(op) == 6);
})
