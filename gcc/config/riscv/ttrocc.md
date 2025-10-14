;; Machine description for Tenstorrent ROCC Intrinsics.
;; Copyright (C) 2025 Tenstorrent Inc.

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

;; TT rocc extension

;; FIXME: We need to use a proper target string like TARGET_XVTTROCC but once the 15.1 update is complete
;; for now, keep the empty string

(define_c_enum "unspecv" [
  UNSPECV_NOC_FENCE
  UNSPECV_DBG_POSTCODE
])

(define_insn "riscv_ttrocc_noc_fence"
  [(unspec_volatile [(const_int 0)] UNSPECV_NOC_FENCE)]
  ""
  "tt.rocc.noc_fence")

(define_insn "riscv_ttrocc_dbg_postcode"
  [(unspec_volatile [(match_operand:DI 0 "const_int_operand" "")] UNSPECV_DBG_POSTCODE)]
  ""
  "tt.rocc.dbg_postcode\t%0")
