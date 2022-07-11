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

(include "sfpu-predicates.md")

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_SFPASSIGNLR
  UNSPECV_SFPASSIGNLR_INT
  UNSPECV_SFPXFCMPV
  UNSPECV_SFPXICMPS
  UNSPECV_SFPXICMPV
  UNSPECV_SFPXVIF
  UNSPECV_SFPXBOOL
  UNSPECV_SFPXCONDB
  UNSPECV_SFPXCONDI
  UNSPECV_SFPINCRWC
  UNSPECV_SFPILLEGAL
  UNSPECV_SFPNONIMM_DST
  UNSPECV_SFPNONIMM_DST_SRC
  UNSPECV_SFPNONIMM_SRC
  UNSPECV_SFPNONIMM_STORE
])

(define_expand "movv64sf"
  [(set (match_operand:V64SF 0 "")
        (match_operand:V64SF 1 ""))]
  ""
{
  if (riscv_legitimize_move (V64SFmode, operands[0], operands[1]))
    DONE;
})

(define_expand "riscv_sfpassignlr"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_SFPASSIGNLR))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  riscv_sfpu_emit_sfpassignlr(operands[0], operands[1]);
  DONE;
})

(define_insn "riscv_sfpassignlr_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(const_int 0)] UNSPECV_SFPASSIGNLR_INT))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
  "")

(define_insn "riscv_sfpnonimm_dst"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:SI    4 "immediate_operand" "i, i") ; op
                          (match_operand:SI    5 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    6 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_SFPNONIMM_DST))
        (clobber (match_scratch:SI 7 "=&r, &r"))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  operands[4] = gen_rtx_CONST_INT(SImode, INTVAL(operands[4]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[5])));
  output_asm_insn("li\t%7,%4", operands);
  output_asm_insn("add\t%7, %7, %6", operands);

  char lv[10];
  asm_fprintf(asm_out_file, "# Op(0x%x) %s d(%d)\n", UINTVAL(operands[4]) >> 24,
                            riscv_sfpu_lv_regno_str(lv, operands[3]), riscv_sfpu_regno(operands[0]));

  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%7,0(%1)", INTVAL(operands[2]), operands);
})

(define_insn "riscv_sfpnonimm_dst_src"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:V64SF 4 "register_operand"  "x, x") ; lreg_src
                          (match_operand:SI    5 "immediate_operand" "i, i") ; op
                          (match_operand:SI    6 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    7 "immediate_operand" "i, i") ; src shft
                          (match_operand:SI    8 "register_operand"  "r, r") ; insn_base
                                                                         ] UNSPECV_SFPNONIMM_DST_SRC))
        (clobber (match_scratch:SI 9 "=&r, &r"))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  operands[5] = gen_rtx_CONST_INT(SImode, INTVAL(operands[5]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[6])) +
                                          (riscv_sfpu_regno(operands[4]) << INTVAL(operands[7])));
  output_asm_insn("li\t%9,%5", operands);
  output_asm_insn("add\t%9, %9, %8", operands);

  char lv[10];
  asm_fprintf(asm_out_file, "# Op(0x%x) %s d(%d) s(%d)\n", UINTVAL(operands[5]) >> 24,
                            riscv_sfpu_lv_regno_str(lv, operands[3]),
                            riscv_sfpu_regno(operands[0]), riscv_sfpu_regno(operands[4]));

  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%9,0(%1)", INTVAL(operands[2]), operands);
})

;;; Differentiate between src and store as store is used in the peephole un-optimization
(define_int_iterator nonimm_srcstore [UNSPECV_SFPNONIMM_SRC UNSPECV_SFPNONIMM_STORE])
(define_int_attr nonimm_srcstore_name [(UNSPECV_SFPNONIMM_SRC "src") (UNSPECV_SFPNONIMM_STORE "store")])

(define_insn "riscv_sfpnonimm_<nonimm_srcstore_name>"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x")
                     (match_operand:SI    1 "address_operand"   "r") ; instrn_buf_add
                     (match_operand:SI    2 "immediate_operand" "i") ; # nops
                     (match_operand:SI    3 "immediate_operand" "i") ; op
                     (match_operand:SI    4 "immediate_operand" "i") ; dst shft
                     (match_operand:SI    5 "register_operand"  "r") ; insn_base
                                                                         ] nonimm_srcstore)
            (clobber (match_scratch:SI    6 "=&r"))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  operands[3] = gen_rtx_CONST_INT(SImode, INTVAL(operands[3]) +
                                          (riscv_sfpu_regno(operands[0]) << INTVAL(operands[4])));
  output_asm_insn("li\t%6,%3", operands);
  output_asm_insn("add\t%6, %6, %5", operands);

  asm_fprintf(asm_out_file, "# Op(0x%x) s(%d)\n", UINTVAL(operands[3]) >> 24, riscv_sfpu_regno(operands[0]));

  return riscv_sfpu_output_nonimm_store_and_nops("sw\t%6,0(%1)", INTVAL(operands[2]), operands);
})

(define_expand "riscv_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "immediate_operand" "")] UNSPECV_SFPXICMPS))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_expand "riscv_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_SFPXICMPV))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_expand "riscv_sfpxvif"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(const_int 0)] UNSPECV_SFPXVIF))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_expand "riscv_sfpxbool"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXBOOL))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_expand "riscv_sfpxcondb"
  [(unspec_volatile [(match_operand:SI 0 "register_operand"  "")
                     (match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXCONDB)]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_expand "riscv_sfpxcondi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXCONDI))]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
{
  gcc_assert(0);
})

(define_insn "riscv_sfpincrwc"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "")
                     (match_operand:SI    1 "immediate_operand" "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")] UNSPECV_SFPINCRWC)]
  "TARGET_SFPU_GS || TARGET_SFPU_WH"
  "SFPINCRWC\t%0, %1, %2, %3")
