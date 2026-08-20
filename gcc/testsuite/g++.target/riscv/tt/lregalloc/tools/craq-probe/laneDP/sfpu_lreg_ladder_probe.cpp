// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0
//
// Lane DS LREG-allocator arsenal: CRAQ golden probe for the integer
// pressure ladders.  Structure mirrors sfpu_welford_prefix_snapshot.cpp
// (the proven custom-body + multi-tile-pack precedent), with UInt32
// unpack-to-dest input and raw-builtin probe bodies.
//
// PROBE_MODE (template parameter):
//   0 identity  : dst tile0 rows 0..15  -> tile3 rows 0..15 (calibration)
//   1 rowtag    : tile3 row i = 0x00A00000 + i               (calibration)
//   2 lanetag   : tile3 rows 0..15 = vConstTileId (creg 15)  (calibration)
//   3..6        : hand-spilled ladder twins N = 9 / 10 / 12 / 16
//                 (arsenal ladder-spilled-body.h; inputs tile0 rows
//                 0..N-1, scratch tile2 bytes 160.., outputs tile3)
//   7 control   : the 8-live ladder rung (arsenal ladder-body.h)

#include <array>
#include <cstdint>

#include "ckernel.h"
#include "llk_defs.h"
#include "params.h"

// Globals
std::uint32_t unp_cfg_context          = 0;
std::uint32_t pack_sync_tile_dst_ptr   = 0;
std::uint32_t math_sync_tile_dst_index = 0;

static constexpr ckernel::DstSync DST_SYNC = ckernel::DstSync::SyncHalf;

#ifdef LLK_TRISC_UNPACK
#include "llk_unpack_A.h"
#include "llk_unpack_common.h"
void run_kernel(RUNTIME_PARAMETERS params)
{
#if defined(RUNTIME_FORMATS) && !defined(SPEED_OF_LIGHT)
    const FormatConfig& formats = params.formats;
#endif
    _llk_unpack_hw_configure_<is_fp32_dest_acc_en>(
        formats.unpack_A_src, formats.unpack_B_src, formats.unpack_A_dst, formats.unpack_B_dst, FACE_R_DIM, FACE_R_DIM, TILE_NUM_FACES, TILE_NUM_FACES);
    _llk_unpack_A_init_<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, unpack_to_dest>(
        0, 0, ckernel::make_tensor_shape_from_legacy(FACE_R_DIM, TILE_NUM_FACES), formats.unpack_A_src, formats.unpack_A_dst);
    _llk_unpack_A_<BroadcastType::NONE, false, EltwiseBinaryReuseDestType::NONE, unpack_to_dest>(
        L1_ADDRESS(params.buffer_A[0]), formats.unpack_A_src, formats.unpack_A_dst);
}
#endif

#ifdef LLK_TRISC_MATH
#include "llk_lib_math_wrappers.h"
#include "llk_math_eltwise_unary_sfpu.h"
#include "llk_math_welfords_sfpu_params.h"
using namespace ckernel;

// The sfpi headers wrap several builtins in arity-reducing macros that
// splice in ckernel::instrn_buffer.  The probe bodies (and the arsenal
// ladder headers) use the RAW builtin arity with a nullptr iptr -- the
// pointer is a compile-time token (codegen emits direct Tensix
// mnemonics), proven by the dg arsenal.  Drop the macros here.
#ifdef __builtin_rvtt_sfpload
#undef __builtin_rvtt_sfpload
#endif
#ifdef __builtin_rvtt_sfpstore
#undef __builtin_rvtt_sfpstore
#endif
#ifdef __builtin_rvtt_sfpxloadi
#undef __builtin_rvtt_sfpxloadi
#endif

namespace
{

/* PROBE_MODE arrives as a constexpr (build.h), NOT a macro: dispatch is
   if-constexpr; every body is compiled into every variant.  */

inline void body_identity()
{
#define IDENT(i)                                                        \
    do                                                                  \
    {                                                                   \
        auto v = __builtin_rvtt_sfpload(nullptr, 2 * (i), 0, 0, 4, 7);  \
        __builtin_rvtt_sfpstore(nullptr, v, 192 + 2 * (i), 0, 0, 4, 7); \
    } while (0)
    IDENT(0); IDENT(1); IDENT(2); IDENT(3);
    IDENT(4); IDENT(5); IDENT(6); IDENT(7);
    IDENT(8); IDENT(9); IDENT(10); IDENT(11);
    IDENT(12); IDENT(13); IDENT(14); IDENT(15);
#undef IDENT
}

inline void body_rowtag()
{
#define ROWTAG(i)                                                               \
    do                                                                          \
    {                                                                           \
        auto v = __builtin_rvtt_sfpxloadi(nullptr, 0x00A00000 + (i), 0, 0, 31); \
        __builtin_rvtt_sfpstore(nullptr, v, 192 + 2 * (i), 0, 0, 4, 7);         \
    } while (0)
    ROWTAG(0); ROWTAG(1); ROWTAG(2); ROWTAG(3);
    ROWTAG(4); ROWTAG(5); ROWTAG(6); ROWTAG(7);
    ROWTAG(8); ROWTAG(9); ROWTAG(10); ROWTAG(11);
    ROWTAG(12); ROWTAG(13); ROWTAG(14); ROWTAG(15);
#undef ROWTAG
}

inline void body_lanetag()
{
    auto v = __builtin_rvtt_sfpreadlreg(15); /* vConstTileId */
#define LANETAG(i) __builtin_rvtt_sfpstore(nullptr, v, 192 + 2 * (i), 0, 0, 4, 7)
    LANETAG(0); LANETAG(1); LANETAG(2); LANETAG(3);
    LANETAG(4); LANETAG(5); LANETAG(6); LANETAG(7);
    LANETAG(8); LANETAG(9); LANETAG(10); LANETAG(11);
    LANETAG(12); LANETAG(13); LANETAG(14); LANETAG(15);
#undef LANETAG
}

#define LSP_NAME body_spill9
#define LSP_N 9
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-spilled-body.h"
#undef LSP_NAME
#undef LSP_N
#undef LSP_FMT
#undef LSP_NOINC
#undef LSP_TRIPS
#undef LSP_SCRATCH

#define LSP_NAME body_spill10
#define LSP_N 10
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-spilled-body.h"
#undef LSP_NAME
#undef LSP_N
#undef LSP_FMT
#undef LSP_NOINC
#undef LSP_TRIPS
#undef LSP_SCRATCH

#define LSP_NAME body_spill12
#define LSP_N 12
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-spilled-body.h"
#undef LSP_NAME
#undef LSP_N
#undef LSP_FMT
#undef LSP_NOINC
#undef LSP_TRIPS
#undef LSP_SCRATCH

#define LSP_NAME body_spill16
#define LSP_N 16
#define LSP_FMT 4
#define LSP_NOINC 7
#define LSP_TRIPS 8
#define LSP_SCRATCH 160
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-spilled-body.h"
#undef LSP_NAME
#undef LSP_N
#undef LSP_FMT
#undef LSP_NOINC
#undef LSP_TRIPS
#undef LSP_SCRATCH

#define LADDER_NAME body_control8
#define LADDER_N 8
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-body.h"
#undef LADDER_NAME
#undef LADDER_N
#undef LADDER_FMT
#undef LADDER_NOINC
#undef LADDER_TRIPS
#undef LADDER_TWIST


#define LADDER_NAME body_rung9
#define LADDER_N 9
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-body.h"
#undef LADDER_NAME
#undef LADDER_N
#undef LADDER_FMT
#undef LADDER_NOINC
#undef LADDER_TRIPS
#undef LADDER_TWIST

#define LADDER_NAME body_rung10
#define LADDER_N 10
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-body.h"
#undef LADDER_NAME
#undef LADDER_N
#undef LADDER_FMT
#undef LADDER_NOINC
#undef LADDER_TRIPS
#undef LADDER_TWIST

#define LADDER_NAME body_rung12
#define LADDER_N 12
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-body.h"
#undef LADDER_NAME
#undef LADDER_N
#undef LADDER_FMT
#undef LADDER_NOINC
#undef LADDER_TRIPS
#undef LADDER_TWIST

#define LADDER_NAME body_rung16
#define LADDER_N 16
#define LADDER_FMT 4
#define LADDER_NOINC 7
#define LADDER_TRIPS 8
#define LADDER_TWIST 0
#include "/home/ttuser/sfpi-uplift/sfpi-gcc-laneDP/gcc/testsuite/g++.target/riscv/tt/lregalloc/ladder-body.h"
#undef LADDER_NAME
#undef LADDER_N
#undef LADDER_FMT
#undef LADDER_NOINC
#undef LADDER_TRIPS
#undef LADDER_TWIST

/* Mode 12: DP-8 measured-corruption witness.  A canary is parked at
   Dst row 500 through the runtime-resolved (mod0 0) view, nine live
   loadi values force the allocator to spill (scratch offset 252 by
   the deterministic chooser: physical rows 500..503/508..511 under
   the 32-bit map), the canary is read back through mod0 0 and its
   XOR against a rematerialized copy is stored to rows 192..198.
   Under a TRUE 32-bit layout every row is disjoint and the diff is
   zero; under a 16-bit layout the mod0-0 canary lives at IDENTITY
   row 500 -- exactly where the spill's 32-bit-geometry stores land --
   and the diff is nonzero: the silent corruption the
   -mtt-tensix-dst-layout-32b declaration contract carries.  */
inline void body_dp8_witness ()
{
    constexpr unsigned CANARY = 0x41420000u;
    auto c0 = __builtin_rvtt_sfpxloadi (nullptr, CANARY, 0, 0, 31);
    __builtin_rvtt_sfpstore (nullptr, c0, 500, 0, 0, 0, 7);
    auto b0 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0001u, 0, 0, 31);
    auto b1 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0102u, 0, 0, 31);
    auto b2 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0203u, 0, 0, 31);
    auto b3 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0304u, 0, 0, 31);
    auto b4 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0405u, 0, 0, 31);
    auto b5 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0506u, 0, 0, 31);
    auto b6 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0607u, 0, 0, 31);
    auto b7 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0708u, 0, 0, 31);
    auto b8 = __builtin_rvtt_sfpxloadi (nullptr, 0x3e4b0809u, 0, 0, 31);
    for (unsigned ix = 0; ix != 8; ++ix)
    {
        b0 = __builtin_rvtt_sfpxor (b0, b1);
        b1 = __builtin_rvtt_sfpxor (b1, b2);
        b2 = __builtin_rvtt_sfpxor (b2, b3);
        b3 = __builtin_rvtt_sfpxor (b3, b4);
        b4 = __builtin_rvtt_sfpxor (b4, b5);
        b5 = __builtin_rvtt_sfpxor (b5, b6);
        b6 = __builtin_rvtt_sfpxor (b6, b7);
        b7 = __builtin_rvtt_sfpxor (b7, b8);
        b8 = __builtin_rvtt_sfpxor (b8, b0);
    }
    auto r = __builtin_rvtt_sfpload (nullptr, 500, 0, 0, 0, 7);
    auto c1 = __builtin_rvtt_sfpxloadi (nullptr, CANARY, 0, 0, 31);
    auto d = __builtin_rvtt_sfpxor (r, c1);
    __builtin_rvtt_sfpstore (nullptr, d, 192, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, d, 194, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, d, 196, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, d, 198, 0, 0, 0, 7);
    /* keep the ring alive so nothing folds */
    auto z = __builtin_rvtt_sfpxor (b0, b8);
    __builtin_rvtt_sfpstore (nullptr, z, 300, 0, 0, 0, 7);
}


/* Mode 13: positional calibration for the DP-8 witness -- stores a
   known marker to exactly the witness's diff rows (no pressure, no
   spills), so the test can assert positions rather than whole-buffer
   emptiness (the scratch offset 252 is inside the packed window and
   legitimately visible).  */
inline void body_dp8_marker ()
{
    auto mk = __builtin_rvtt_sfpxloadi (nullptr, 0x5a5a0000u, 0, 0, 31);
    __builtin_rvtt_sfpstore (nullptr, mk, 192, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, mk, 194, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, mk, 196, 0, 0, 0, 7);
    __builtin_rvtt_sfpstore (nullptr, mk, 198, 0, 0, 0, 7);
}

__attribute__((noinline)) void probe_body()
{
    if constexpr (PROBE_MODE == 0)
        body_identity();
    else if constexpr (PROBE_MODE == 1)
        body_rowtag();
    else if constexpr (PROBE_MODE == 2)
        body_lanetag();
    else if constexpr (PROBE_MODE == 3)
        body_spill9();
    else if constexpr (PROBE_MODE == 4)
        body_spill10();
    else if constexpr (PROBE_MODE == 5)
        body_spill12();
    else if constexpr (PROBE_MODE == 6)
        body_spill16();
    else if constexpr (PROBE_MODE == 8)
        body_rung9();
    else if constexpr (PROBE_MODE == 9)
        body_rung10();
    else if constexpr (PROBE_MODE == 10)
        body_rung12();
    else if constexpr (PROBE_MODE == 11)
        body_rung16();
    else if constexpr (PROBE_MODE == 12)
        body_dp8_witness();
    else if constexpr (PROBE_MODE == 13)
        body_dp8_marker();
    else
        body_control8();
}

} // namespace

void run_kernel(RUNTIME_PARAMETERS params)
{
#if defined(RUNTIME_FORMATS) && !defined(SPEED_OF_LIGHT)
    const FormatConfig& formats = params.formats;
#endif
    _llk_math_eltwise_unary_datacopy_init_wrapper_<DataCopyType::A2D, is_fp32_dest_acc_en, BroadcastType::NONE, false, PackMode::Default>(
        TILE_NUM_FACES, formats.math);
    _llk_math_hw_configure_<is_fp32_dest_acc_en>(formats.math, formats.math);
    _llk_math_pack_sync_init_<DST_SYNC, is_fp32_dest_acc_en>();
    _llk_math_wait_for_dest_available_<DST_SYNC>();
    _llk_math_eltwise_unary_datacopy_<DataCopyType::A2D, DST_SYNC, is_fp32_dest_acc_en, BroadcastType::NONE, unpack_to_dest>(
        0, formats.math, formats.math);
    _llk_math_eltwise_unary_sfpu_init_once_();
    /* Datacopy leaves the dest RWC counter advanced; the per-op eltwise
       init (and the welford init) reset it before SFPU work.  */
    math::reset_counters(p_setrwc::SET_ABD_F);
    _llk_math_welfords_sfpu_params_(+[]()
    {
        /* The raw probe bodies carry no predication, so the compiler
           emits no SFPENCC; enable all lanes explicitly.  */
        __builtin_rvtt_sfpencc_all_lanes();
        probe_body();
    }, 0);
    _llk_math_dest_section_done_<DST_SYNC, is_fp32_dest_acc_en>();
}
#endif

#ifdef LLK_TRISC_PACK
#include "llk_lib_pack_wrappers.h"
#include "llk_pack_common.h"
void run_kernel(RUNTIME_PARAMETERS params)
{
#if defined(RUNTIME_FORMATS) && !defined(SPEED_OF_LIGHT)
    const FormatConfig& formats = params.formats;
#endif
    _llk_pack_hw_configure_wrapper_<is_fp32_dest_acc_en, PackMode::Default>(
        formats.pack_src, formats.pack_dst, FACE_R_DIM * FACE_C_DIM * TILE_NUM_FACES);
    _llk_pack_init_wrapper_<PackMode::Default, false>(formats.pack_dst, FACE_R_DIM, TILE_C_DIM, TILE_NUM_FACES);
    _llk_pack_dest_init_<DST_SYNC, is_fp32_dest_acc_en>();
    _llk_packer_wait_for_math_done_();
    _llk_pack_<DST_SYNC, is_fp32_dest_acc_en, ckernel::PackMode::Default>(0, L1_ADDRESS(params.buffer_Res[0]));
    _llk_pack_<DST_SYNC, is_fp32_dest_acc_en, ckernel::PackMode::Default>(1, L1_ADDRESS(params.buffer_Res[1]));
    _llk_pack_<DST_SYNC, is_fp32_dest_acc_en, ckernel::PackMode::Default>(2, L1_ADDRESS(params.buffer_Res[2]));
    _llk_pack_<DST_SYNC, is_fp32_dest_acc_en, ckernel::PackMode::Default>(3, L1_ADDRESS(params.buffer_Res[3]));
    _llk_pack_dest_section_done_<DST_SYNC, is_fp32_dest_acc_en>();
}
#endif
