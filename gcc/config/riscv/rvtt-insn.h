// The _lv instructions MUST follow the non-live versions of the same instruction

// Columns are: name, decl, enable, can_set_cc, live, has_half_offset, dst_arg_pos, mod1_pos

#ifndef RVTT_INTERNAL
#define RVTT_INTERNAL(a, b, c, d)
#endif

#ifndef RVTT_BUILTIN
#define RVTT_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

#ifndef RVTT_NO_TGT_BUILTIN
#define RVTT_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

#ifndef RVTT_GS_PAD_BUILTIN
#define RVTT_GS_PAD_BUILTIN(a)
#endif

#ifndef RVTT_GS_PAD_NO_TGT_BUILTIN
#define RVTT_GS_PAD_NO_TGT_BUILTIN(a)
#endif

#ifndef RVTT_GS_PAD_INTERNAL
#define RVTT_GS_PAD_INTERNAL(a)
#endif

#ifndef RVTT_GS_INTERNAL
#define RVTT_GS_INTERNAL(a, b)
#endif

#ifndef RVTT_GS_BUILTIN
#define RVTT_GS_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

#ifndef RVTT_GS_NO_TGT_BUILTIN
#define RVTT_GS_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

#ifndef RVTT_WH_INTERNAL
#define RVTT_WH_INTERNAL(a, b)
#endif

#ifndef RVTT_WH_BUILTIN
#define RVTT_WH_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

#ifndef RVTT_WH_NO_TGT_BUILTIN
#define RVTT_WH_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h, i, j, k)
#endif

// Note: each architecture MUST have the SAME NUMBER of entries in the SAME ORDER!
// This can be ensured by using the RVTT_PAD_XX define

// Common internal (rtl only) insns
RVTT_INTERNAL (sfpnonimm_dst,     5, 0x40,  1)
RVTT_INTERNAL (sfpnonimm_dst_src, 7, 0x40,  1)
RVTT_INTERNAL (sfpnonimm_src,     5, 0x40,  2)
RVTT_INTERNAL (sfpnonimm_store,   5, 0x40,  2)
RVTT_INTERNAL (sfpgccmov_cc,     -1, 0x00, -1)

// Common builtin intrinsics
RVTT_BUILTIN (load_immediate,  RISCV_USI_FTYPE_USI,                                              0, 0, 0, -1, -1, 0x10, -1,      0, 0)
RVTT_BUILTIN (sfpassignlreg,   RISCV_V64SF_FTYPE_USI,                                            0, 0, 0, -1, -1, 0x10, -1,      0, 0)
RVTT_BUILTIN (sfpxicmps,       RISCV_USI_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                    1, 0, 0, -1,  5, 0x00,  2,      0, 0)
RVTT_BUILTIN (sfpxicmpv,       RISCV_USI_FTYPE_V64SF_V64SF_USI,                                  1, 0, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_BUILTIN (sfpxbool,        RISCV_USI_FTYPE_USI_USI_USI,                                      0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_BUILTIN (sfpxcondi,       RISCV_V64SF_FTYPE_USI,                                            0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_BUILTIN (sfpxvif,         RISCV_USI_FTYPE,                                                  0, 0, 0, -1, -1, 0x00, -1,      0, 0)

RVTT_NO_TGT_BUILTIN (sfpxcondb,      RISCV_VOID_FTYPE_USI_USI,                                   0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_NO_TGT_BUILTIN (sfpincrwc,      RISCV_VOID_FTYPE_USI_USI_USI_USI,                           0, 0, 0, -1, -1, 0x10, -1,      0, 0)


// Grayskull internal (rtl only) insns
RVTT_GS_INTERNAL(sfpload_int,             0)
RVTT_GS_INTERNAL(sfploadi_int,            0)
RVTT_GS_INTERNAL(sfpstore_int,            0)
RVTT_GS_INTERNAL(sfpmuli_int,             0)
RVTT_GS_INTERNAL(sfpaddi_int,             0)
RVTT_GS_INTERNAL(sfpmul_int,              0)
RVTT_GS_INTERNAL(sfpadd_int,              0)
RVTT_GS_INTERNAL(sfpiadd_v_int,           0)
RVTT_GS_INTERNAL(sfpiadd_i_int,           0)
RVTT_GS_INTERNAL(sfpshft_i_int,           0)
RVTT_GS_INTERNAL(sfpabs_int,              0)
RVTT_GS_INTERNAL(sfpnot_int,              0)
RVTT_GS_INTERNAL(sfplz_int,               0)
RVTT_GS_INTERNAL(sfpsetman_i_int,         0)
RVTT_GS_INTERNAL(sfpsetexp_i_int,         0)
RVTT_GS_INTERNAL(sfpsetsgn_i_int,         0)
RVTT_GS_INTERNAL(sfpmad_int,              0)
RVTT_GS_INTERNAL(sfpmov_int,              0)
RVTT_GS_INTERNAL(sfpdivp2_int,            0)
RVTT_GS_INTERNAL(sfpexexp_int,            0)
RVTT_GS_INTERNAL(sfpexman_int,            0)
RVTT_GS_INTERNAL(sfpassignlreg_int,       0)
RVTT_GS_INTERNAL(sfppreservelreg0_int,    0)
RVTT_GS_INTERNAL(sfppreservelreg1_int,    0)
RVTT_GS_INTERNAL(sfppreservelreg2_int,    0)
RVTT_GS_INTERNAL(sfppreservelreg3_int,    0)
RVTT_GS_PAD_INTERNAL(sfppreservelreg4_int)
RVTT_GS_PAD_INTERNAL(sfppreservelreg5_int)
RVTT_GS_PAD_INTERNAL(sfppreservelreg6_int)
RVTT_GS_PAD_INTERNAL(sfppreservelreg7_int)
RVTT_GS_PAD_INTERNAL(sfpcast_int)
RVTT_GS_PAD_INTERNAL(sfpshft2_e_int)
RVTT_GS_PAD_INTERNAL(sfpstochrnd_i_int)
RVTT_GS_PAD_INTERNAL(sfpstochrnd_v_int)
RVTT_GS_PAD_INTERNAL(sfpswap_int)

// Grayskull builtin intrinsics
RVTT_GS_BUILTIN (sfpassign_lv,    RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 1, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpload,         RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_USI,                        0, 0, 0, -1,  1, 0x00,  2, 0xFFFF, 0)
RVTT_GS_BUILTIN (sfpload_lv,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 1, 0, -1,  2, 0x00,  3, 0xFFFF, 0)
RVTT_GS_BUILTIN (sfpxloadi,       RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_USI,                        0, 0, 0, -1,  1, 0x00,  2, 0xFFFF, 0)
RVTT_GS_BUILTIN (sfpxloadi_lv,    RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 1, 0, -1,  2, 0x00,  3, 0xFFFF, 0)
RVTT_GS_BUILTIN (sfpmov,          RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpmov_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpmul,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 0, 1, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpmul_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 1, 1, -1,  3, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpmuli,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 0, 1,  1,  5, 0x00,  2, 0xFFFF, 8)
RVTT_GS_BUILTIN (sfpadd,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 0, 1, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpadd_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 1, 1, -1,  3, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpaddi,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 0, 1,  1,  5, 0x00,  2, 0xFFFF, 8)
RVTT_GS_BUILTIN (sfpxiadd_v,      RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 0, 0,  0,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpxiadd_i,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  1, 0, 0, -1,  5, 0x00,  2,      0, 0)
RVTT_GS_BUILTIN (sfpxiadd_i_lv,   RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI_USI_USI,            1, 1, 0, -1,  6, 0x00,  3,      0, 0)
RVTT_GS_BUILTIN (sfpshft_v,       RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpshft_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI,                      0, 0, 0,  1, -1, 0x00,  2,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpabs,          RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpabs_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpand,          RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpor,           RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpnot,          RISCV_V64SF_FTYPE_V64SF,                                          0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpnot_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 1, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfplz,           RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfplz_lv,        RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpsetexp_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpsetexp_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF,                      0, 0, 0, -1, -1, 0x00,  1,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpsetexp_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF,                0, 1, 0, -1, -1, 0x00,  2,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpsetman_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpsetman_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF,                      0, 0, 0, -1, -1, 0x00,  1,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpsetman_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF,                0, 1, 0, -1, -1, 0x00,  2,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpsetsgn_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpsetsgn_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF,                      0, 0, 0, -1, -1, 0x00,  1,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpsetsgn_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF,                0, 1, 0, -1, -1, 0x00,  2,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpmad,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 0, 1, -1,  3, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpmad_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,                    0, 1, 1, -1,  4, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpdivp2,        RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF_USI,                  0, 0, 0, -1,  5, 0x00,  1,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpdivp2_lv,     RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF_USI,            0, 1, 0, -1,  6, 0x00,  2,  0xFFF, 12)
RVTT_GS_BUILTIN (sfpexexp,        RISCV_V64SF_FTYPE_V64SF_UHI,                                      1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpexexp_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpexman,        RISCV_V64SF_FTYPE_V64SF_UHI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpexman_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfplut,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,                    0, 0, 0,  3,  4, 0x00, -1,      0, 0)
RVTT_GS_BUILTIN (sfpxfcmps,       RISCV_USI_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                    1, 0, 0, -1,  5, 0x00,  2,      0, 0)
RVTT_GS_BUILTIN (sfpxfcmpv,       RISCV_USI_FTYPE_V64SF_V64SF_USI,                                  1, 0, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_GS_PAD_BUILTIN (sfpxor)
RVTT_GS_PAD_BUILTIN (sfpcast)
RVTT_GS_PAD_BUILTIN (sfpcast_lv)
RVTT_GS_PAD_BUILTIN (sfpshft2_e)
RVTT_GS_PAD_BUILTIN (sfpshft2_e_lv)
RVTT_GS_PAD_BUILTIN (sfpstochrnd_i)
RVTT_GS_PAD_BUILTIN (sfpstochrnd_i_lv)
RVTT_GS_PAD_BUILTIN (sfpstochrnd_v)
RVTT_GS_PAD_BUILTIN (sfpstochrnd_v_lv)
RVTT_GS_PAD_BUILTIN (sfplutfp32_3r)
RVTT_GS_PAD_BUILTIN (sfplutfp32_6r)

RVTT_GS_NO_TGT_BUILTIN (sfppreservelreg,RISCV_VOID_FTYPE_V64SF_USI,                                 0, 0, 0, -1, -1, 0x10, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                                   1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_V64SF_USI,                                 1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                                   1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                                           1, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE,                                           0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE,                                           1, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI_USI_USI,             0, 0, 0, -1,  2, 0x00,  3, 0xFFFF, 0)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfpconfig_v)
RVTT_GS_NO_TGT_BUILTIN (sfpnop,         RISCV_VOID_FTYPE,                                           0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfpswap)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfptransp)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfpshft2_g)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfpshft2_ge)
RVTT_GS_PAD_NO_TGT_BUILTIN (sfpreplay)


// Grayskull internal (rtl only) insns
RVTT_WH_INTERNAL(sfpload_int,             0x00)
RVTT_WH_INTERNAL(sfploadi_int,            0x00)
RVTT_WH_INTERNAL(sfpstore_int,            0x00)
RVTT_WH_INTERNAL(sfpmuli_int,             0x21)
RVTT_WH_INTERNAL(sfpaddi_int,             0x21)
RVTT_WH_INTERNAL(sfpmul_int,              0x21)
RVTT_WH_INTERNAL(sfpadd_int,              0x21)
RVTT_WH_INTERNAL(sfpiadd_v_int,           0x00)
RVTT_WH_INTERNAL(sfpiadd_i_int,           0x00)
RVTT_WH_INTERNAL(sfpshft_i_int,           0x00)
RVTT_WH_INTERNAL(sfpabs_int,              0x00)
RVTT_WH_INTERNAL(sfpnot_int,              0x00)
RVTT_WH_INTERNAL(sfplz_int,               0x00)
RVTT_WH_INTERNAL(sfpsetman_i_int,         0x00)
RVTT_WH_INTERNAL(sfpsetexp_i_int,         0x00)
RVTT_WH_INTERNAL(sfpsetsgn_i_int,         0x00)
RVTT_WH_INTERNAL(sfpmad_int,              0x21)
RVTT_WH_INTERNAL(sfpmov_int,              0x00)
RVTT_WH_INTERNAL(sfpdivp2_int,            0x00)
RVTT_WH_INTERNAL(sfpexexp_int,            0x00)
RVTT_WH_INTERNAL(sfpexman_int,            0x00)
RVTT_WH_INTERNAL(sfpassignlreg_int,       0x10)
RVTT_WH_INTERNAL(sfppreservelreg0_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg1_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg2_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg3_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg4_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg5_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg6_int,    0x10)
RVTT_WH_INTERNAL(sfppreservelreg7_int,    0x10)
RVTT_WH_INTERNAL(sfpcast_int,             0x00)
RVTT_WH_INTERNAL(sfpshft2_e_int,          0x01)
RVTT_WH_INTERNAL(sfpstochrnd_i_int,       0x00)
RVTT_WH_INTERNAL(sfpstochrnd_v_int,       0x00)
RVTT_WH_INTERNAL(sfpswap_int,             0x01)

// Wormhole builtin intrinsics
RVTT_WH_BUILTIN (sfpassign_lv,    RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 1, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpload,         RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_USI_USI,                    0, 0, 0, -1,  2, 0x00,  3, 0x3FFF, 0)
RVTT_WH_BUILTIN (sfpload_lv,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI_USI,              0, 1, 0, -1,  3, 0x00,  4, 0x3FFF, 0)
RVTT_WH_BUILTIN (sfpxloadi,       RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_USI,                        0, 0, 0, -1,  1, 0x00,  2, 0xFFFF, 0)
RVTT_WH_BUILTIN (sfpxloadi_lv,    RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 1, 0, -1,  2, 0x00,  3, 0xFFFF0000, -16)
RVTT_WH_BUILTIN (sfpmov,          RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmov_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmul,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 0, 1, -1,  2, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmul_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 1, 1, -1,  3, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmuli,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 0, 1,  1,  5, 0x21,  2, 0xFFFF, 8)
RVTT_WH_BUILTIN (sfpadd,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 0, 1, -1,  2, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpadd_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 1, 1, -1,  3, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpaddi,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  0, 0, 1,  1,  5, 0x21,  2, 0xFFFF, 8)
RVTT_WH_BUILTIN (sfpxiadd_v,      RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 0, 0,  0,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxiadd_i,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                  1, 0, 0, -1,  5, 0x00,  2,      0, 0)
RVTT_WH_BUILTIN (sfpxiadd_i_lv,   RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI_USI_USI,            1, 1, 0, -1,  6, 0x00,  3,      0, 0)
RVTT_WH_BUILTIN (sfpshft_v,       RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI,                      0, 0, 0,  1, -1, 0x00,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpabs,          RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpabs_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpand,          RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpor,           RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpnot,          RISCV_V64SF_FTYPE_V64SF,                                          0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpnot_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 1, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfplz,           RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfplz_lv,        RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetexp_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetexp_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF,                      0, 0, 0, -1, -1, 0x00,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetexp_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF,                0, 1, 0, -1, -1, 0x00,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetman_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetman_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF_USI,                  0, 0, 0, -1,  5, 0x00,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetman_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF_USI,            0, 1, 0, -1,  6, 0x00,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetsgn_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpsetsgn_i,     RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF,                      0, 0, 0, -1, -1, 0x00,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpsetsgn_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF,                0, 1, 0, -1, -1, 0x00,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpmad,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,                          0, 0, 1, -1,  3, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpmad_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,                    0, 1, 1, -1,  4, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpdivp2,        RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_V64SF_USI,                  0, 0, 0, -1,  5, 0x00,  1,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpdivp2_lv,     RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_V64SF_USI,            0, 1, 0, -1,  6, 0x00,  2,  0xFFF, 12)
RVTT_WH_BUILTIN (sfpexexp,        RISCV_V64SF_FTYPE_V64SF_UHI,                                      1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexexp_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                1, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexman,        RISCV_V64SF_FTYPE_V64SF_UHI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpexman_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfplut,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,                    0, 0, 0,  3,  4, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxfcmps,       RISCV_USI_FTYPE_POINTER_V64SF_USI_USI_USI_USI,                    1, 0, 0, -1,  5, 0x00,  2,      0, 0)
RVTT_WH_BUILTIN (sfpxfcmpv,       RISCV_USI_FTYPE_V64SF_V64SF_USI,                                  1, 0, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpxor,          RISCV_V64SF_FTYPE_V64SF_V64SF,                                    0, 0, 0,  0, -1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpcast,         RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpcast_lv,      RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft2_e,      RISCV_V64SF_FTYPE_V64SF_USI,                                      0, 0, 0, -1,  1, 0x01, -1,      0, 0)
RVTT_WH_BUILTIN (sfpshft2_e_lv,   RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                                0, 1, 0, -1,  2, 0x01, -1,      0, 0)
RVTT_WH_BUILTIN (sfpstochrnd_i,   RISCV_V64SF_FTYPE_POINTER_USI_USI_USI_USI_V64SF_USI,              0, 0, 0, -1,  6, 0x00,  2,   0xFF, 8)
RVTT_WH_BUILTIN (sfpstochrnd_i_lv,RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI_USI_USI_V64SF_USI,        0, 1, 0, -1,  7, 0x00,  3,   0xFF, 8)
RVTT_WH_BUILTIN (sfpstochrnd_v,   RISCV_V64SF_FTYPE_USI_V64SF_V64SF_USI,                            0, 0, 0, -1,  3, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfpstochrnd_v_lv,RISCV_V64SF_FTYPE_V64SF_USI_V64SF_V64SF_USI,                      0, 1, 0, -1,  4, 0x00, -1,      0, 0)
RVTT_WH_BUILTIN (sfplutfp32_3r,   RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,                    0, 0, 0, -1,  4, 0x21, -1,      0, 0)
RVTT_WH_BUILTIN (sfplutfp32_6r,   RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_V64SF_V64SF_V64SF_USI,  0, 0, 0, -1,  7, 0x21, -1,      0, 0)

RVTT_WH_NO_TGT_BUILTIN (sfppreservelreg,RISCV_VOID_FTYPE_V64SF_USI,                                 0, 0, 0, -1, -1, 0x10, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                                   1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_V64SF_USI,                                 1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                                   1, 0, 0, -1,  1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                                           1, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE_USI,                                       0, 0, 0, -1,  0, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE_USI,                                       1, 0, 0, -1,  0, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI_USI_USI_USI,         0, 0, 0, -1,  3, 0x00,  4, 0x3FFF, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpconfig_v,    RISCV_VOID_FTYPE_V64SF_USI,                                 0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpnop,         RISCV_VOID_FTYPE,                                           0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpswap,        RISCV_VOID_FTYPE_V64SF_V64SF_USI,                           0, 0, 0, -1,  2, 0x01, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfptransp,      RISCV_VOID_FTYPE_V64SF_V64SF_V64SF_V64SF,                   0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpshft2_g,     RISCV_VOID_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,               0, 0, 0, -1,  4, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpshft2_ge,    RISCV_VOID_FTYPE_V64SF_V64SF_V64SF_V64SF_V64SF,             0, 0, 0, -1, -1, 0x00, -1,      0, 0)
RVTT_WH_NO_TGT_BUILTIN (sfpreplay,      RISCV_VOID_FTYPE_USI_USI_USI_USI,                           0, 0, 0, -1, -1, 0x00, -1,      0, 0)

#undef RVTT_INTERNAL
#undef RVTT_BUILTIN
#undef RVTT_NO_TGT_BUILTIN
#undef RVTT_GS_PAD_BUILTIN
#undef RVTT_GS_PAD_NO_TGT_BUILTIN
#undef RVTT_GS_PAD_INTERNAL
#undef RVTT_GS_INTERNAL
#undef RVTT_GS_BUILTIN
#undef RVTT_GS_NO_TGT_BUILTIN
#undef RVTT_WH_INTERNAL
#undef RVTT_WH_BUILTIN
#undef RVTT_WH_NO_TGT_BUILTIN
