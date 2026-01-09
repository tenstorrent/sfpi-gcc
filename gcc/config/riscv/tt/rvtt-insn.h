/* TT builtin instruction specs
   Copyright (C) 2022-2025 Tenstorrent Inc.
   Originated by Paul Keller (pkeller@tenstorrent.com).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */


// The _lv instructions MUST follow the non-live versions of the same instruction

// NO_TGT doesn't return a value
// PAD ensures that each arch has the same # insns (PAD aren't instanced)

#ifndef RVTT_BUILTIN
#define RVTT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef RVTT_NO_TGT_BUILTIN
#define RVTT_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h, i)
#endif

#ifndef RVTT_WH_PAD_BUILTIN
#define RVTT_WH_PAD_BUILTIN(a)
#endif

#ifndef RVTT_WH_PAD_NO_TGT_BUILTIN
#define RVTT_WH_PAD_NO_TGT_BUILTIN(a)
#endif

#ifndef RVTT_WH_BUILTIN
#define RVTT_WH_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef RVTT_WH_NO_TGT_BUILTIN
#define RVTT_WH_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef RVTT_BH_PAD_BUILTIN
#define RVTT_BH_PAD_BUILTIN(a)
#endif

#ifndef RVTT_BH_PAD_NO_TGT_BUILTIN
#define RVTT_BH_PAD_NO_TGT_BUILTIN(a)
#endif

#ifndef RVTT_BH_BUILTIN
#define RVTT_BH_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef RVTT_BH_NO_TGT_BUILTIN
#define RVTT_BH_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

// Note: each architecture MUST have the SAME NUMBER of entries in the SAME ORDER!
// This can be ensured by using the RVTT_PAD_XX define

// flags: see INSN_FLAGS in rvtt.h
// dst_arg_pos: which argument number contains the destination for src-as-dst insns, -1 otherwise
// mod_pos: which argument number contains the mod value
// schedule: non-zero if this instruction needs scheduling (nop pads), see INSN_SCHED_* in rvtt-protos.h
// nonimm_pos: argument position of the nonimm argument (runtime supplied immediate), -1 otherwise
// nonimm_mask: bit mask to get to the nonimm value (eg, 0xFFFF for 16 bit nonimm)
// nonimm_shft: shift to right justify the nonimm value

// Common builtin intrinsics.  args are (id, fmt, flags, dst_arg_pos, mod_pos, nonimm_pos, nonimm_mask, nonimm_shft)
RVTT_BUILTIN (synth_opcode,       RISCV_USI_FTYPE_USI_USI,                                          0x20, -1, -1, -1,      0, 0)

RVTT_BUILTIN (sfpxicmps,          RISCV_USI_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,                 0x01, -1,  5,  2,      0, 0)
RVTT_BUILTIN (sfpxicmpv,          RISCV_USI_FTYPE_XTT_VEC_XTT_VEC_USI,                              0x01, -1,  2, -1,      0, 0)
RVTT_BUILTIN (sfpxbool,           RISCV_USI_FTYPE_USI_USI_USI,                                      0x00, -1, -1, -1,      0, 0)
RVTT_BUILTIN (sfpxcondi,          RISCV_XTT_VEC_FTYPE_USI,                                          0x00, -1, -1, -1,      0, 0)
RVTT_BUILTIN (sfpxvif,            RISCV_USI_FTYPE,                                                  0x00, -1, -1, -1,      0, 0)
RVTT_NO_TGT_BUILTIN (sfpxcondb,   RISCV_VOID_FTYPE_USI_USI,                                         0x00, -1, -1, -1,      0, 0)

RVTT_BUILTIN (sfpreadlreg,        RISCV_XTT_VEC_FTYPE_USI,                                          0x00, -1, -1, -1,      0, 0)
RVTT_NO_TGT_BUILTIN (sfpwritelreg,RISCV_VOID_FTYPE_XTT_VEC_USI,                                     0x00, -1, -1, -1,      0, 0)

RVTT_BUILTIN (sfpselect2,         RISCV_XTT_VEC_FTYPE_XTT_VEC2_USI,                                 0x00, -1, -1, -1,      0, 0)

RVTT_NO_TGT_BUILTIN (sfpnop,      RISCV_VOID_FTYPE,                                                 0x00, -1, -1, -1,      0, 0)
RVTT_BUILTIN (sfpswap,            RISCV_XTT_VEC2_FTYPE_XTT_VEC_XTT_VEC_USI,                         0x00, -1, -1, -1,      0, 0)

RVTT_NO_TGT_BUILTIN (ttincrwc,    RISCV_VOID_FTYPE_USI_USI_USI_USI,                                 0x00, -1, -1, -1,      0, 0)
// The length operand is [1,32], which is awkward
RVTT_NO_TGT_BUILTIN (ttreplay,    RISCV_VOID_FTYPE_XTT_IPTR_USI_USI_USI_USI_USI_USI,                0x00, -1, -1,  1, (unsigned)-1, 4)

// Wormhole builtin intrinsics
RVTT_WH_BUILTIN (sfpassign_lv,    RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x02, -1, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpload,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI_USI,                 0x00, -1,  2,  3, 0x3FFF, 0)
RVTT_WH_BUILTIN (sfpload_lv,      RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_USI,         0x02, -1,  3,  4, 0x3FFF, 0)
RVTT_WH_BUILTIN (sfpxloadi,       RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI,                     0x00, -1,  1,  2, 0xFFFF, 0)
RVTT_WH_BUILTIN (sfpxloadi_lv,    RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x02, -1,  2,  3, 0xFFFF0000, -16)
RVTT_WH_BUILTIN (sfpmov,          RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmov_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmul,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmul_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x02, -1,  3, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmuli,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x00,  1,  5,  2, 0xFFFF, 8)
RVTT_WH_BUILTIN (sfpadd,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpadd_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x02, -1,  3, -1,      0, 0)
RVTT_WH_BUILTIN (sfpaddi,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x00,  1,  5,  2, 0xFFFF, 8)
RVTT_WH_BUILTIN (sfpxiadd_v,      RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x01,  0,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxiadd_i,      RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x01, -1,  5,  2,      0, 0)
RVTT_WH_BUILTIN (sfpxiadd_i_lv,   RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_XTT_VEC_USI_USI_USI_USI,     0x03, -1,  6,  3,      0, 0)
RVTT_WH_BUILTIN (sfpshft_v,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft_i,       RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI,                 0x00,  1, -1,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpabs,          RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpabs_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpand,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpor,           RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpnot,          RISCV_XTT_VEC_FTYPE_XTT_VEC,                                      0x00, -1, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpnot_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x02, -1, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfplz,           RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfplz_lv,        RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x03, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetexp_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetexp_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC,                 0x00, -1, -1,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetexp_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC,         0x02, -1, -1,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetman_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetman_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC_USI,             0x00, -1,  5,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetman_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC_USI,     0x02, -1,  6,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetsgn_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetsgn_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC,                 0x00, -1, -1,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetsgn_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC,         0x02, -1, -1,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpmad,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x00, -1,  3, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmad_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x02, -1,  4, -1,      0, 0)
RVTT_WH_BUILTIN (sfpdivp2,        RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC_USI,             0x00, -1,  5,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpdivp2_lv,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC_USI,     0x02, -1,  6,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpexexp,        RISCV_XTT_VEC_FTYPE_XTT_VEC_UHI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexexp_lv,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x03, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexman,        RISCV_XTT_VEC_FTYPE_XTT_VEC_UHI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexman_lv,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfplut,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x00,  3,  4, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxfcmps,       RISCV_USI_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,                 0x01, -1,  5,  2,      0, 0)
RVTT_WH_BUILTIN (sfpxfcmpv,       RISCV_USI_FTYPE_XTT_VEC_XTT_VEC_USI,                              0x01, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxor,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpcast,         RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpcast_lv,      RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft2_e,      RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft2_e_lv,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_WH_BUILTIN (sfpstochrnd_i,   RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI_XTT_VEC_USI,         0x00, -1,  6,  2,   0xFF, 8)
RVTT_WH_BUILTIN (sfpstochrnd_i_lv,RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_XTT_VEC_USI, 0x02, -1,  7,  3,   0xFF, 8)
RVTT_WH_BUILTIN (sfpstochrnd_v,   RISCV_XTT_VEC_FTYPE_USI_XTT_VEC_XTT_VEC_USI,                      0x00, -1,  3, -1,      0, 0)
RVTT_WH_BUILTIN (sfpstochrnd_v_lv,RISCV_XTT_VEC_FTYPE_XTT_VEC_USI_XTT_VEC_XTT_VEC_USI,              0x02, -1,  4, -1,      0, 0)
RVTT_WH_BUILTIN (sfplutfp32_3r,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x00, -1,  4, -1,      0, 0)
RVTT_WH_BUILTIN (sfplutfp32_6r,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI, 0x00, -1,  7, -1,      0, 0)
RVTT_WH_PAD_BUILTIN (sfpmul24)
RVTT_WH_PAD_BUILTIN (sfpmul24_lv)
RVTT_WH_PAD_BUILTIN (sfpgt)
RVTT_WH_PAD_BUILTIN (sfple)
RVTT_WH_PAD_BUILTIN (sfparecip)
RVTT_WH_PAD_BUILTIN (sfparecip_lv)
RVTT_WH_PAD_BUILTIN (sfpmov_config)
RVTT_WH_PAD_BUILTIN (sfpmov_config_lv)

RVTT_WH_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                                   0x01, -1,  1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_XTT_VEC_USI,                               0x01, -1,  1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                                   0x01, -1,  1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                                           0x01, -1, -1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE_USI,                                       0x00, -1,  0, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE_USI,                                       0x01, -1,  0, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_USI,      0x00, -1,  3,  4, 0x3FFF, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpconfig_v,    RISCV_VOID_FTYPE_XTT_VEC_USI,                               0x00, -1, -1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfptransp,      RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC,           0x00, -1, -1, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpshft2_g,     RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,       0x00, -1,  4, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpshft2_ge,    RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC,   0x00, -1, -1, -1,      0, 0)

// Blackhole builtin intrinsics
RVTT_BH_BUILTIN (sfpassign_lv,    RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x02, -1, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpload,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI_USI,                 0x00, -1,  2,  3, 0x1FFF, 0)
RVTT_BH_BUILTIN (sfpload_lv,      RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_USI,         0x02, -1,  3,  4, 0x1FFF, 0)
RVTT_BH_BUILTIN (sfpxloadi,       RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI,                     0x00, -1,  1,  2, 0xFFFF, 0)
RVTT_BH_BUILTIN (sfpxloadi_lv,    RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x02, -1,  2,  3, 0xFFFF0000, -16)
RVTT_BH_BUILTIN (sfpmov,          RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmov_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmul,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmul_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x02, -1,  3, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmuli,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x00,  1,  5,  2, 0xFFFF, 8)
RVTT_BH_BUILTIN (sfpadd,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpadd_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x02, -1,  3, -1,      0, 0)
RVTT_BH_BUILTIN (sfpaddi,         RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x00,  1,  5,  2, 0xFFFF, 8)
RVTT_BH_BUILTIN (sfpxiadd_v,      RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x01,  0,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpxiadd_i,      RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x01, -1,  5,  2,      0, 0)
RVTT_BH_BUILTIN (sfpxiadd_i_lv,   RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_XTT_VEC_USI_USI_USI_USI,     0x03, -1,  6,  3,      0, 0)
RVTT_BH_BUILTIN (sfpshft_v,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00,  0,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpshft_i,       RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,             0x00,  1,  5,  2,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpabs,          RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpabs_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpand,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpor,           RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpnot,          RISCV_XTT_VEC_FTYPE_XTT_VEC,                                      0x00, -1, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpnot_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x02, -1, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfplz,           RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfplz_lv,        RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x03, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpsetexp_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpsetexp_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC,                 0x00, -1, -1,  1,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpsetexp_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC,         0x02, -1, -1,  2,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpsetman_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpsetman_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC_USI,             0x00, -1,  5,  1,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpsetman_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC_USI,     0x02, -1,  6,  2,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpsetsgn_v,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpsetsgn_i,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC,                 0x00, -1, -1,  1,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpsetsgn_i_lv,  RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC,         0x02, -1, -1,  2,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpmad,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x00, -1,  3, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmad_lv,       RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x02, -1,  4, -1,      0, 0)
RVTT_BH_BUILTIN (sfpdivp2,        RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_XTT_VEC_USI,             0x00, -1,  5,  1,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpdivp2_lv,     RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_XTT_VEC_USI,     0x02, -1,  6,  2,  0xFFF, 12)
RVTT_BH_BUILTIN (sfpexexp,        RISCV_XTT_VEC_FTYPE_XTT_VEC_UHI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpexexp_lv,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x03, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpexman,        RISCV_XTT_VEC_FTYPE_XTT_VEC_UHI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpexman_lv,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfplut,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x00,  3,  4, -1,      0, 0)
RVTT_BH_BUILTIN (sfpxfcmps,       RISCV_USI_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI,                 0x01, -1,  5,  2,      0, 0)
RVTT_BH_BUILTIN (sfpxfcmpv,       RISCV_USI_FTYPE_XTT_VEC_XTT_VEC_USI,                              0x01, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpxor,          RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC,                              0x00,  0, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpcast,         RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpcast_lv,      RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpshft2_e,      RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpshft2_e_lv,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpstochrnd_i,   RISCV_XTT_VEC_FTYPE_XTT_IPTR_USI_USI_USI_USI_XTT_VEC_USI,         0x00, -1,  6,  2,   0xFF, 8)
RVTT_BH_BUILTIN (sfpstochrnd_i_lv,RISCV_XTT_VEC_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_XTT_VEC_USI, 0x02, -1,  7,  3,   0xFF, 8)
RVTT_BH_BUILTIN (sfpstochrnd_v,   RISCV_XTT_VEC_FTYPE_USI_XTT_VEC_XTT_VEC_USI,                      0x00, -1,  3, -1,      0, 0)
RVTT_BH_BUILTIN (sfpstochrnd_v_lv,RISCV_XTT_VEC_FTYPE_XTT_VEC_USI_XTT_VEC_XTT_VEC_USI,              0x02, -1,  4, -1,      0, 0)
RVTT_BH_BUILTIN (sfplutfp32_3r,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,          0x00, -1,  4, -1,      0, 0)
RVTT_BH_BUILTIN (sfplutfp32_6r,   RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI, 0x00, -1,  7,  -1,      0, 0)
RVTT_BH_BUILTIN (sfpmul24,        RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x00, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmul24_lv,     RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_USI,                  0x02, -1,  3, -1,      0, 0)
RVTT_BH_BUILTIN (sfpgt,           RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfple,           RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x01, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfparecip,       RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                                  0x00, -1,  1, -1,      0, 0)
RVTT_BH_BUILTIN (sfparecip_lv,    RISCV_XTT_VEC_FTYPE_XTT_VEC_XTT_VEC_USI,                          0x02, -1,  2, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmov_config,       RISCV_XTT_VEC_FTYPE_USI,                                      0x00, -1, -1, -1,      0, 0)
RVTT_BH_BUILTIN (sfpmov_config_lv,    RISCV_XTT_VEC_FTYPE_XTT_VEC_USI,                              0x02, -1, -1, -1,      0, 0)

RVTT_BH_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                                   0x01, -1,  1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_XTT_VEC_USI,                               0x01, -1,  1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                                   0x01, -1,  1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                                           0x01, -1, -1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE_USI,                                       0x00, -1,  0, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE_USI,                                       0x01, -1,  0, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_XTT_IPTR_XTT_VEC_USI_USI_USI_USI_USI,      0x00, -1,  3,  4, 0x1FFF, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpconfig_v,    RISCV_VOID_FTYPE_XTT_VEC_USI,                               0x00, -1, -1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfptransp,      RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC,           0x00, -1, -1, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpshft2_g,     RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_USI,       0x00, -1,  4, -1,      0, 0)
RVTT_BH_NO_TGT_BUILTIN (sfpshft2_ge,    RISCV_VOID_FTYPE_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC_XTT_VEC,   0x00, -1, -1, -1,      0, 0)

#undef RVTT_BUILTIN
#undef RVTT_NO_TGT_BUILTIN
#undef RVTT_WH_PAD_BUILTIN
#undef RVTT_WH_PAD_NO_TGT_BUILTIN
#undef RVTT_WH_BUILTIN
#undef RVTT_WH_NO_TGT_BUILTIN
#undef RVTT_BH_PAD_BUILTIN
#undef RVTT_BH_PAD_NO_TGT_BUILTIN
#undef RVTT_BH_BUILTIN
#undef RVTT_BH_NO_TGT_BUILTIN
