// The _lv instructions MUST follow the non-live versions of the same instruction

// Columns are: name, decl, enable, can_set_cc, live, has_half_offset, dst_arg_pos, mod1_pos

#ifndef SFPU_PAD_BUILTIN
#define SFPU_PAD_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef SFPU_PAD_NO_TGT_BUILTIN
#define SFPU_PAD_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef SFPU_GS_BUILTIN
#define SFPU_GS_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef SFPU_GS_NO_TGT_BUILTIN
#define SFPU_GS_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef SFPU_WH_BUILTIN
#define SFPU_WH_BUILTIN(a, b, c, d, e, f, g, h)
#endif

#ifndef SFPU_WH_NO_TGT_BUILTIN
#define SFPU_WH_NO_TGT_BUILTIN(a, b, c, d, e, f, g, h)
#endif

// Note: each architecture MUST have the SAME NUMBER of entries in the SAME ORDER!
// This can be ensured by using the SFPU_PAD define

// Grayskull builtin intrinsics
SFPU_GS_BUILTIN (sfpassign_lv,    RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 1, 0, -1, -1)
SFPU_GS_BUILTIN (sfpassignlr,     RISCV_V64SF_FTYPE_USI,                             grayskull, 0, 0, 0, -1, -1)
SFPU_GS_BUILTIN (sfpload,         RISCV_V64SF_FTYPE_POINTER_USI_USI,                 grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfpload_lv,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 0, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfploadi_ex,     RISCV_V64SF_FTYPE_POINTER_USI_USI,                 grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfploadi_ex_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 0, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfpmov,          RISCV_V64SF_FTYPE_V64SF_USI,                       grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfpmov_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 0, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfpmul,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 0, 0, 1, -1,  2)
SFPU_GS_BUILTIN (sfpmul_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           grayskull, 0, 1, 1, -1,  3)
SFPU_GS_BUILTIN (sfpadd,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 0, 0, 1, -1,  2)
SFPU_GS_BUILTIN (sfpadd_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           grayskull, 0, 1, 1, -1,  3)
SFPU_GS_BUILTIN (sfpmuli,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 0, 0, 1,  1,  3)
SFPU_GS_BUILTIN (sfpaddi,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 0, 0, 1,  1,  3)
SFPU_GS_BUILTIN (sfpiadd_v,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 1, 0, 0,  0,  2)
SFPU_GS_BUILTIN (sfpiadd_v_ex,    RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 1, 0, 0,  0,  2)
SFPU_GS_BUILTIN (sfpiadd_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 1, 0, 0, -1,  3)
SFPU_GS_BUILTIN (sfpiadd_i_lv,    RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI,     grayskull, 1, 1, 0, -1,  4)
SFPU_GS_BUILTIN (sfpiadd_i_ex,    RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           grayskull, 1, 0, 0, -1,  3)
SFPU_GS_BUILTIN (sfpiadd_i_ex_lv, RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI,     grayskull, 1, 1, 0, -1,  4)
SFPU_GS_BUILTIN (sfpshft_v,       RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpshft_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI,               grayskull, 0, 0, 0,  1, -1)
SFPU_GS_BUILTIN (sfpabs,          RISCV_V64SF_FTYPE_V64SF_USI,                       grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfpabs_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 0, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfpand,          RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpor,           RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpnot,          RISCV_V64SF_FTYPE_V64SF,                           grayskull, 0, 0, 0, -1, -1)
SFPU_GS_BUILTIN (sfpnot_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 1, 0, -1, -1)
SFPU_GS_BUILTIN (sfplz,           RISCV_V64SF_FTYPE_V64SF_USI,                       grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfplz_lv,        RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 1, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfpsetexp_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpsetexp_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               grayskull, 0, 0, 0, -1, -1)
SFPU_GS_BUILTIN (sfpsetexp_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         grayskull, 0, 1, 0, -1, -1)
SFPU_GS_BUILTIN (sfpsetman_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpsetman_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               grayskull, 0, 0, 0, -1, -1)
SFPU_GS_BUILTIN (sfpsetman_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         grayskull, 0, 1, 0, -1, -1)
SFPU_GS_BUILTIN (sfpsetsgn_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     grayskull, 0, 0, 0,  0, -1)
SFPU_GS_BUILTIN (sfpsetsgn_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               grayskull, 0, 0, 0, -1, -1)
SFPU_GS_BUILTIN (sfpsetsgn_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         grayskull, 0, 1, 0, -1, -1)
SFPU_GS_BUILTIN (sfpmad,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           grayskull, 0, 0, 1, -1,  3)
SFPU_GS_BUILTIN (sfpmad_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,     grayskull, 0, 1, 1, -1,  4)
SFPU_GS_BUILTIN (sfpdivp2,        RISCV_V64SF_FTYPE_POINTER_USI_V64SF_USI,           grayskull, 0, 0, 0, -1,  3)
SFPU_GS_BUILTIN (sfpdivp2_lv,     RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF_USI,     grayskull, 0, 1, 0, -1,  4)
SFPU_GS_BUILTIN (sfpexexp,        RISCV_V64SF_FTYPE_V64SF_UHI,                       grayskull, 1, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfpexexp_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 1, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfpexman,        RISCV_V64SF_FTYPE_V64SF_UHI,                       grayskull, 0, 0, 0, -1,  1)
SFPU_GS_BUILTIN (sfpexman_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 grayskull, 0, 1, 0, -1,  2)
SFPU_GS_BUILTIN (sfplut,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,     grayskull, 0, 0, 0,  3,  4)

SFPU_GS_NO_TGT_BUILTIN (sfpkeepalive,   RISCV_VOID_FTYPE_V64SF_USI,                  grayskull, 0, 0, 0, -1, -1)
SFPU_GS_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                    grayskull, 1, 0, 0, -1,  1)
SFPU_GS_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_V64SF_USI,                  grayskull, 1, 0, 0, -1,  1)
SFPU_GS_NO_TGT_BUILTIN (sfpscmp_ex,     RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI,      grayskull, 1, 0, 0, -1,  3)
SFPU_GS_NO_TGT_BUILTIN (sfpvcmp_ex,     RISCV_VOID_FTYPE_V64SF_V64SF_USI,            grayskull, 1, 0, 0, -1,  2)
SFPU_GS_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                    grayskull, 1, 0, 0, -1,  1)
SFPU_GS_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                            grayskull, 1, 0, 0, -1, -1)
SFPU_GS_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE,                            grayskull, 0, 0, 0, -1, -1)
SFPU_GS_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE,                            grayskull, 1, 0, 0, -1, -1)
SFPU_GS_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI,      grayskull, 0, 0, 0, -1,  2)
SFPU_GS_NO_TGT_BUILTIN (sfpnop,         RISCV_VOID_FTYPE,                            grayskull, 0, 0, 0, -1, -1)
SFPU_GS_NO_TGT_BUILTIN (sfpillegal,     RISCV_VOID_FTYPE,                            grayskull, 0, 0, 0, -1, -1)


// Wormhole builtin intrinsics
SFPU_WH_BUILTIN (sfpassign_lv,    RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 1, 0, -1, -1)
SFPU_WH_BUILTIN (sfpassignlr,     RISCV_V64SF_FTYPE_USI,                             wormhole, 0, 0, 0, -1, -1)
SFPU_WH_BUILTIN (sfpload,         RISCV_V64SF_FTYPE_POINTER_USI_USI_USI,             wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfpload_lv,      RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 0, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfploadi_ex,     RISCV_V64SF_FTYPE_POINTER_USI_USI,                 wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfploadi_ex_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 0, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfpmov,          RISCV_V64SF_FTYPE_V64SF_USI,                       wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfpmov_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 0, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfpmul,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 0, 0, 1, -1,  2)
SFPU_WH_BUILTIN (sfpmul_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           wormhole, 0, 1, 1, -1,  3)
SFPU_WH_BUILTIN (sfpadd,          RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 0, 0, 1, -1,  2)
SFPU_WH_BUILTIN (sfpadd_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           wormhole, 0, 1, 1, -1,  3)
SFPU_WH_BUILTIN (sfpmuli,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 0, 0, 1,  1,  3)
SFPU_WH_BUILTIN (sfpaddi,         RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 0, 0, 1,  1,  3)
SFPU_WH_BUILTIN (sfpiadd_v,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 1, 0, 0,  0,  2)
SFPU_WH_BUILTIN (sfpiadd_v_ex,    RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 1, 0, 0,  0,  2)
SFPU_WH_BUILTIN (sfpiadd_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 1, 0, 0, -1,  3)
SFPU_WH_BUILTIN (sfpiadd_i_lv,    RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI,     wormhole, 1, 1, 0, -1,  4)
SFPU_WH_BUILTIN (sfpiadd_i_ex,    RISCV_V64SF_FTYPE_POINTER_V64SF_USI_USI,           wormhole, 1, 0, 0, -1,  3)
SFPU_WH_BUILTIN (sfpiadd_i_ex_lv, RISCV_V64SF_FTYPE_POINTER_V64SF_V64SF_USI_USI,     wormhole, 1, 1, 0, -1,  4)
SFPU_WH_BUILTIN (sfpshft_v,       RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpshft_i,       RISCV_V64SF_FTYPE_POINTER_V64SF_USI,               wormhole, 0, 0, 0,  1, -1)
SFPU_WH_BUILTIN (sfpabs,          RISCV_V64SF_FTYPE_V64SF_USI,                       wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfpabs_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 0, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfpand,          RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpor,           RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpnot,          RISCV_V64SF_FTYPE_V64SF,                           wormhole, 0, 0, 0, -1, -1)
SFPU_WH_BUILTIN (sfpnot_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 1, 0, -1, -1)
SFPU_WH_BUILTIN (sfplz,           RISCV_V64SF_FTYPE_V64SF_USI,                       wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfplz_lv,        RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 1, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfpsetexp_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpsetexp_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               wormhole, 0, 0, 0, -1, -1)
SFPU_WH_BUILTIN (sfpsetexp_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         wormhole, 0, 1, 0, -1, -1)
SFPU_WH_BUILTIN (sfpsetman_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpsetman_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               wormhole, 0, 0, 0, -1, -1)
SFPU_WH_BUILTIN (sfpsetman_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         wormhole, 0, 1, 0, -1, -1)
SFPU_WH_BUILTIN (sfpsetsgn_v,     RISCV_V64SF_FTYPE_V64SF_V64SF,                     wormhole, 0, 0, 0,  0, -1)
SFPU_WH_BUILTIN (sfpsetsgn_i,     RISCV_V64SF_FTYPE_POINTER_USI_V64SF,               wormhole, 0, 0, 0, -1, -1)
SFPU_WH_BUILTIN (sfpsetsgn_i_lv,  RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF,         wormhole, 0, 1, 0, -1, -1)
SFPU_WH_BUILTIN (sfpmad,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_USI,           wormhole, 0, 0, 1, -1,  3)
SFPU_WH_BUILTIN (sfpmad_lv,       RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,     wormhole, 0, 1, 1, -1,  4)
SFPU_WH_BUILTIN (sfpdivp2,        RISCV_V64SF_FTYPE_POINTER_USI_V64SF_USI,           wormhole, 0, 0, 0, -1,  3)
SFPU_WH_BUILTIN (sfpdivp2_lv,     RISCV_V64SF_FTYPE_POINTER_V64SF_USI_V64SF_USI,     wormhole, 0, 1, 0, -1,  4)
SFPU_WH_BUILTIN (sfpexexp,        RISCV_V64SF_FTYPE_V64SF_UHI,                       wormhole, 1, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfpexexp_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 1, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfpexman,        RISCV_V64SF_FTYPE_V64SF_UHI,                       wormhole, 0, 0, 0, -1,  1)
SFPU_WH_BUILTIN (sfpexman_lv,     RISCV_V64SF_FTYPE_V64SF_V64SF_USI,                 wormhole, 0, 1, 0, -1,  2)
SFPU_WH_BUILTIN (sfplut,          RISCV_V64SF_FTYPE_V64SF_V64SF_V64SF_V64SF_USI,     wormhole, 0, 0, 0,  3,  4)

SFPU_WH_NO_TGT_BUILTIN (sfpkeepalive,   RISCV_VOID_FTYPE_V64SF_USI,                  wormhole, 0, 0, 0, -1, -1)
SFPU_WH_NO_TGT_BUILTIN (sfpsetcc_i,     RISCV_VOID_FTYPE_USI_USI,                    wormhole, 1, 0, 0, -1,  1)
SFPU_WH_NO_TGT_BUILTIN (sfpsetcc_v,     RISCV_VOID_FTYPE_V64SF_USI,                  wormhole, 1, 0, 0, -1,  1)
SFPU_WH_NO_TGT_BUILTIN (sfpscmp_ex,     RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI,      wormhole, 1, 0, 0, -1,  3)
SFPU_WH_NO_TGT_BUILTIN (sfpvcmp_ex,     RISCV_VOID_FTYPE_V64SF_V64SF_USI,            wormhole, 1, 0, 0, -1,  2)
SFPU_WH_NO_TGT_BUILTIN (sfpencc,        RISCV_VOID_FTYPE_USI_USI,                    wormhole, 1, 0, 0, -1,  1)
SFPU_WH_NO_TGT_BUILTIN (sfpcompc,       RISCV_VOID_FTYPE,                            wormhole, 1, 0, 0, -1, -1)
SFPU_WH_NO_TGT_BUILTIN (sfppushc,       RISCV_VOID_FTYPE,                            wormhole, 0, 0, 0, -1, -1)
SFPU_WH_NO_TGT_BUILTIN (sfppopc,        RISCV_VOID_FTYPE,                            wormhole, 1, 0, 0, -1, -1)
SFPU_WH_NO_TGT_BUILTIN (sfpstore,       RISCV_VOID_FTYPE_POINTER_V64SF_USI_USI,      wormhole, 0, 0, 0, -1,  2)
SFPU_WH_NO_TGT_BUILTIN (sfpnop,         RISCV_VOID_FTYPE,                            wormhole, 0, 0, 0, -1, -1)
SFPU_WH_NO_TGT_BUILTIN (sfpillegal,     RISCV_VOID_FTYPE,                            wormhole, 0, 0, 0, -1, -1)

#undef SFPU_PAD_BUILTIN
#undef SFPU_PAD_NO_TGT_BUILTIN
#undef SFPU_GS_BUILTIN
#undef SFPU_GS_NO_TGT_BUILTIN
#undef SFPU_WH_BUILTIN
#undef SFPU_WH_NO_TGT_BUILTIN
