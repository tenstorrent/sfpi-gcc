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


;; Un-optimization to ensure a NOP fits between a store/load
(define_peephole2
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "immediate_operand" "")
                     (match_operand:SI    2 "immediate_operand" "")] UNSPECV_SFPSTORE_INT)
   (set (match_operand:V64SF 3 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 4 "nonmemory_operand" "")
                          (match_operand:SI    5 "immediate_operand" "")
                          (match_operand:SI    6 "immediate_operand" "")] UNSPECV_SFPLOAD_INT))]
  "TARGET_SFPU"
  [(const_int 0)]
{
  emit_insn(gen_riscv_sfpstore_int(operands[0], operands[1], operands[2]));
  emit_insn(gen_riscv_sfpnop());
  emit_insn(gen_riscv_sfpload_int(operands[3], operands[4], operands[5], operands[6]));
})

;; Un-optimization to ensure a NOP fits between a store/load
(define_peephole2
  [(unspec_volatile [(match_operand:V64SF 0 "register_operand"  "")
                     (match_operand:SI    1 "address_operand"   "")
                     (match_operand:SI    2 "immediate_operand" "")
                     (match_operand:SI    3 "immediate_operand" "")
                     (match_operand:SI    4 "register_operand"  "")] UNSPECV_SFPNONIMM_STORE)
   (set (match_operand:V64SF 5 "register_operand" "")
        (unspec_volatile [(match_operand:V64SF 6 "nonmemory_operand" "")
                          (match_operand:SI    7 "immediate_operand" "")
                          (match_operand:SI    8 "immediate_operand" "")] UNSPECV_SFPLOAD_INT))
   (clobber (match_scratch:SI 9 ""))]
  "TARGET_SFPU"
  [(const_int 0)]
{
  emit_insn(gen_riscv_sfpnonimm_store(operands[0], operands[1], GEN_INT(1), operands[2], operands[3], operands[4]));
  emit_insn(gen_riscv_sfpload_int(operands[5], operands[6], operands[7], operands[8]));
})


;; LZ
(define_peephole2
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "const_0_operand")] UNSPECV_SFPLZ_INT))
   (unspec_volatile [(match_dup:V64SF     2)
                     (match_operand:SI    4 "const_setcc_z_or_nez")] UNSPECV_SFPSETCC_V)]

  "TARGET_SFPU"
  [(const_int 0)]
{
  int mod1b = INTVAL(operands[4]);
  // Only legal values of SETCC are 2 or 6 which map to 2 and 10
  rtx mod = GEN_INT((mod1b == 2) ? 2 : 10);

  emit_insn(gen_riscv_sfplz_int(operands[0], operands[1], operands[2], mod));
})

(define_peephole2
  [(set (match_operand:V64SF 0 "register_operand")
        (unspec_volatile [(match_operand:V64SF 1 "nonmemory_operand")
                          (match_operand:V64SF 2 "register_operand")
                          (match_operand:SI    3 "const_0_operand")] UNSPECV_SFPLZ_INT))
   (unspec_volatile [(const_int 0)] UNSPECV_SFPPUSHC)
   (unspec_volatile [(match_dup:V64SF     2)
                     (match_operand:SI    4 "const_setcc_z_or_nez")] UNSPECV_SFPSETCC_V)]

  "TARGET_SFPU"
  [(const_int 0)]
{
  int mod1b = INTVAL(operands[4]);
  // Only legal values of SETCC are 2 or 6 which map to 2 and 10
  rtx mod = GEN_INT((mod1b == 2) ? 2 : 10);

  emit_insn(gen_riscv_sfppushc());
  emit_insn(gen_riscv_sfplz_int(operands[0], operands[1], operands[2], mod));
})
