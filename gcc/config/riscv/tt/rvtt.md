;; Machine description for Tenstorrent SFPU Intrinsics.
;; Copyright (C) 2022 Free Software Foundation, Inc.
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

(include "tt/rvtt-predicates.md")
(include "tt/rvtt-tune.md")

; & in spec means early clobber, written before inputs are used, cannot reuse input reg

(define_mode_iterator RVTT_ANY_INT [SI HI QI])
(define_mode_attr rvtt_any_int_mode_name [(SI "si") (HI "hi") (QI "qi")])
(define_mode_attr rvtt_any_int_mode_mnem [(SI "w") (HI "h") (QI "b")])
(define_mode_attr rvtt_any_uint_mode_load_mod [(SI "") (HI "u") (QI "u")])

(define_c_enum "unspecv" [
  ;; Tenstorrent SFPU unspecs.
  ;; INT for internal
  ;; IMM for immediate
  ;; LV for keep dst reg alive as input for predicated liveness
  UNSPECV_LOAD_IMMEDIATE
  UNSPECV_SFPASSIGNLREG
  UNSPECV_SFPASSIGNLREG_INT
  UNSPECV_SFPXFCMPV
  UNSPECV_SFPXICMPS
  UNSPECV_SFPXICMPV
  UNSPECV_SFPXVIF
  UNSPECV_SFPXBOOL
  UNSPECV_SFPXCONDB
  UNSPECV_SFPXCONDI
  UNSPECV_SFPNONIMM_DST
  UNSPECV_SFPNONIMM_DST_SRC
  UNSPECV_SFPNONIMM_SRC
  UNSPECV_SFPNONIMM_STORE
  UNSPECV_TTINCRWC
])

(define_expand "movv64sf"
  [(set (match_operand:V64SF 0 "")
        (match_operand:V64SF 1 ""))]
  ""
{
  if (riscv_legitimize_move (V64SFmode, operands[0], operands[1]))
    DONE;
})

(define_insn "rvtt_load_immediate"
  [(set (match_operand:SI 0 "register_operand" "=r")
         (unspec [(match_operand:SI   1 "immediate_operand" "")] UNSPECV_LOAD_IMMEDIATE))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  static char str[100];
  sprintf(str, "%s\t# %lx", "li\t%0, %1", INTVAL(operands[1]));
  return str;
})

(define_expand "rvtt_sfpassignlreg"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "immediate_operand" "M04U")] UNSPECV_SFPASSIGNLREG))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  rvtt_emit_sfpassignlreg(operands[0], operands[1]);
  DONE;
})

(define_insn "rvtt_sfpassignlreg_int"
  [(set (match_operand:V64SF 0 "register_operand" "=x")
        (unspec_volatile [(const_int 0)] UNSPECV_SFPASSIGNLREG_INT))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
  "")

(define_insn "rvtt_sfpnonimm_dst"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops / scheduling info
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:SI    4 "immediate_operand" "i, i") ; dst reg shft
                          (match_operand:SI    5 "register_operand"  "r, r") ; nonimm value to store
                          (match_operand:DI    6 "immediate_operand" "i, i") ; op
                          (match_operand:SI    7 "immediate_operand" "i, i") ; loadimm id/fallback flag
                                                                         ] UNSPECV_SFPNONIMM_DST))
        (clobber (match_scratch:SI 8 "=&r, &r"))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  const char *mn;
  unsigned int id = INTVAL(operands[7]);

  if ((id & 0xFF000000)) {
    // Fallback
    unsigned int op = INTVAL(operands[6]);
    unsigned int mask = 0xF << INTVAL(operands[4]);
    unsigned int old_reg = op & mask;
    unsigned int new_reg = rvtt_sfpu_regno(operands[0]) << INTVAL(operands[4]);
    unsigned int reg_update = old_reg ^ new_reg;
    if (reg_update == 0) { // otherwise, why fallback?
      fprintf(stderr, "unexpected non-imm fallback id:0x%x old regs=0x%x new regs=0x%x\n",
              id, old_reg, new_reg);
      gcc_unreachable();
    }
    // Use xor instead of and/or so we can update w/ 1 tmp reg in 1 operation
    operands[4] = gen_rtx_CONST_INT(SImode, reg_update);
    output_asm_insn("li\t%8, %4", operands);
    output_asm_insn("xor\t%8, %8, %5", operands);
    mn = "sw\t%8, 0(%1)";
  } else {
    mn = "sw\t%5, 0(%1)";
  }

  static char out[200];
  char lv[20];
  sprintf(out, "%s\t# Op(0x%lx) %s d(lr%d)", mn, UINTVAL(operands[6]) >> 24, rvtt_sfpu_lv_regno_str(lv, operands[3]), rvtt_sfpu_regno(operands[0]));
  return rvtt_output_nonimm_and_nops(out, INTVAL(operands[2]), operands);
})

(define_insn "rvtt_sfpnonimm_dst_src"
  [(set (match_operand:V64SF 0 "register_operand" "=x, x")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "r, r") ; instrn_buf_add
                          (match_operand:SI    2 "immediate_operand" "i, i") ; # nops / scheduling info
                          (match_operand:V64SF 3 "nonmemory_operand" "E, 0") ; live lreg_dst
                          (match_operand:V64SF 4 "register_operand"  "x, x") ; lreg_src
                          (match_operand:SI    5 "immediate_operand" "i, i") ; dst shft
                          (match_operand:SI    6 "immediate_operand" "i, i") ; src shft
                          (match_operand:SI    7 "register_operand"  "r, r") ; non imm value to store
                          (match_operand:DI    8 "immediate_operand" "i, i") ; op
                          (match_operand:SI    9 "immediate_operand" "i, i") ; loadimm id/fallback flag
                                                                         ] UNSPECV_SFPNONIMM_DST_SRC))
        (clobber (match_scratch:SI 10 "=&r, &r"))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  const char *mn;
  unsigned int id = INTVAL(operands[9]);

  if ((id & 0xFF000000)) {
    // Fallback
    unsigned int op = INTVAL(operands[8]);
    unsigned int dst_mask = 0xF << INTVAL(operands[5]);
    unsigned int src_mask = 0xF << INTVAL(operands[6]);
    unsigned int old_reg = op & (dst_mask | src_mask);
    unsigned int new_reg =
        (rvtt_sfpu_regno(operands[0]) << INTVAL(operands[5])) |
        (rvtt_sfpu_regno(operands[4]) << INTVAL(operands[6]));

    unsigned int reg_update = old_reg ^ new_reg;
    if (reg_update == 0) { // otherwise, why fallback?
      fprintf(stderr, "unexpected non-imm fallback id:0x%x old regs=0x%x new regs=0x%x\n",
              id, old_reg, new_reg);
      gcc_unreachable();
    }
    // Use xor instead of and/or so we can update w/ 1 tmp reg in 1 operation
    operands[5] = gen_rtx_CONST_INT(SImode, reg_update);
    output_asm_insn("li\t%10, %5", operands);
    output_asm_insn("xor\t%10, %10, %7", operands);
    mn = "sw\t%10, 0(%1)";
  } else {
    mn = "sw\t%7, 0(%1)";
  }

  static char out[200];
  char lv[20];
  sprintf(out, "%s\t# Op(0x%lx) %s d(lr%d) s(lr%d)", mn, UINTVAL(operands[8]) >> 24, rvtt_sfpu_lv_regno_str(lv, operands[3]), rvtt_sfpu_regno(operands[0]), rvtt_sfpu_regno(operands[4]));
  return rvtt_output_nonimm_and_nops(out, INTVAL(operands[2]), operands);
})

;;; Differentiate between src and store as store is used in the peephole un-optimization
(define_int_iterator nonimm_srcstore [UNSPECV_SFPNONIMM_SRC UNSPECV_SFPNONIMM_STORE])
(define_int_attr nonimm_srcstore_name [(UNSPECV_SFPNONIMM_SRC "src") (UNSPECV_SFPNONIMM_STORE "store")])

(define_insn "rvtt_sfpnonimm_<nonimm_srcstore_name>"
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "x") ; src
                     (match_operand:SI    1 "address_operand"   "r") ; instrn_buf_add
                     (match_operand:SI    2 "immediate_operand" "i") ; # nops / scheduling info
                     (match_operand:SI    3 "immediate_operand" "i") ; src shft
                     (match_operand:SI    4 "register_operand"  "r") ; non imm value to store
                     (match_operand:DI    5 "immediate_operand" "i") ; op
                     (match_operand:SI    6 "immediate_operand" "i") ; loadimm id/fallback flag
                                                                         ] nonimm_srcstore)
            (clobber (match_scratch:SI    7 "=&r"))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  const char *mn;
  unsigned int id = INTVAL(operands[6]);

  if ((id & 0xFF000000)) {
    // Fallback
    unsigned int op = INTVAL(operands[5]);
    unsigned int mask = 0xF << INTVAL(operands[3]);
    unsigned int old_reg = op & mask;
    unsigned int new_reg = rvtt_sfpu_regno(operands[0]) << INTVAL(operands[3]);
    unsigned int reg_update = old_reg ^ new_reg;
    if (reg_update == 0) { // otherwise, why fallback?
      fprintf(stderr, "unexpected non-imm fallback id:0x%x old regs=0x%x new regs=0x%x\n",
              id, old_reg, new_reg);
      gcc_unreachable();
    }
    // Use xor instead of and/or so we can update w/ 1 tmp reg in 1 operation
    operands[3] = gen_rtx_CONST_INT(SImode, reg_update);
    output_asm_insn("li\t%7, %3", operands);
    output_asm_insn("xor\t%7, %7, %4", operands);
    mn = "sw\t%7, 0(%1)";
  } else {
    mn = "sw\t%4, 0(%1)";
  }

  static char out[200];
  sprintf(out, "%s\t# Op(0x%lx) s(lr%d)", mn, UINTVAL(operands[5]) >> 24, rvtt_sfpu_regno(operands[0]));
  return rvtt_output_nonimm_and_nops(out, INTVAL(operands[2]), operands);
})

(define_expand "rvtt_sfpxicmps"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI    1 "address_operand"   "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "nonmemory_operand" "")
                          (match_operand:SI    4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_SFPXICMPS))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_expand "rvtt_sfpxicmpv"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 1 "register_operand"  "")
                          (match_operand:V64SF 2 "register_operand"  "")
                          (match_operand:SI    3 "immediate_operand" "")] UNSPECV_SFPXICMPV))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_expand "rvtt_sfpxvif"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(const_int 0)] UNSPECV_SFPXVIF))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_expand "rvtt_sfpxbool"
  [(set (match_operand:SI 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXBOOL))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_expand "rvtt_sfpxcondb"
  [(unspec_volatile [(match_operand:SI 0 "register_operand"  "")
                     (match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXCONDB)]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_expand "rvtt_sfpxcondi"
  [(set (match_operand:V64SF 0 "register_operand" "")
        (unspec_volatile [(match_operand:SI 1 "register_operand"  "")] UNSPECV_SFPXCONDI))]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
{
  gcc_unreachable();
})

(define_insn "rvtt_ttincrwc"
  [(unspec_volatile [(match_operand:SI    0 "immediate_operand" "")
                     (match_operand:SI    1 "immediate_operand" "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")] UNSPECV_TTINCRWC)]
  "TARGET_RVTT_WH || TARGET_RVTT_BH"
  "TTINCRWC\t%0, %1, %2, %3")
