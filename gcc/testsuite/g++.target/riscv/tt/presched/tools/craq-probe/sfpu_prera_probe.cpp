// SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
// SPDX-License-Identifier: Apache-2.0
//
// Lane EH pre-RA pressure scheduler: CRAQ probe.  Structure mirrors the
// lane DS ladder probe (tools/craq-probe in lregalloc/, the proven
// custom-body + multi-tile-pack precedent).
//
// PROBE_MODE (template parameter):
//   0 identity : dst tile0 rows 0..15 -> tile3 rows 0..15 (calibration)
//   1 rowtag   : tile3 row i = 0x00A00000 + i              (calibration)
//   2 lanetag  : tile3 rows 0..15 = vConstTileId (creg 15) (calibration)
//   3 wide int fire  : the ten-live two-chain XOR body in WIDE order,
//        five inputs (tile0 rows 0..4), five discriminating outputs
//        (r0..r3, out -> tile3 rows 0..4).  Compiles ONLY under
//        -mtt-tensix-optimize-pressure-schedule-prera (ten live as
//        written); the schedule must be bit-exact vs the host golden.
//   4 seq int golden : the same dataflow in chain-wise (narrow) order;
//        compiles at plain flags; same five outputs -- the golden twin.
//   5 wide float fire: mul/add/mad two-chain wide order (ten live as
//        written, mad-family latency 1); outputs r0,r1,r2,s0 ->
//        tile3 rows 0..3.  Fire leg of the float differential.
//   6 seq float      : same float dataflow, narrow order -- the
//        differential reference (bit-identical outputs required; no
//        value is reassociated, only issue order moves).

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

// Drop the sfpi arity-reducing builtin macros (raw-builtin bodies use
// the raw arity with a nullptr iptr -- the lane DS probe precedent).
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

/* Shared int-fire leaf loads + output stores; the two bodies differ
   ONLY in issue order (same dataflow, host golden identical).  */
#define PRERA_LOAD5                                                     \
    auto a = __builtin_rvtt_sfpload(nullptr, 0, 0, 0, 4, 7);            \
    auto b = __builtin_rvtt_sfpload(nullptr, 2, 0, 0, 4, 7);            \
    auto c = __builtin_rvtt_sfpload(nullptr, 4, 0, 0, 4, 7);            \
    auto d = __builtin_rvtt_sfpload(nullptr, 6, 0, 0, 4, 7);            \
    auto e = __builtin_rvtt_sfpload(nullptr, 8, 0, 0, 4, 7)
#define PRERA_STORE5                                                    \
    __builtin_rvtt_sfpstore(nullptr, r0, 192, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, r1, 194, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, r2, 196, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, r3, 198, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, out, 200, 0, 0, 4, 7)

/* Ten live as written: the whole probe TU is compiled under
   -mtt-tensix-optimize-pressure-schedule-prera (PROBE_MODE is a
   constexpr, not a macro -- every body lands in every variant, the
   lane DS probe precedent), so this body compiles everywhere and the
   sim executes it only in mode 3.  */
inline void body_wide_int()
{
    PRERA_LOAD5;
    auto u0 = __builtin_rvtt_sfpxor(a, b);
    auto u1 = __builtin_rvtt_sfpxor(b, c);
    auto u2 = __builtin_rvtt_sfpxor(c, d);
    auto u3 = __builtin_rvtt_sfpxor(d, e);
    auto u4 = __builtin_rvtt_sfpxor(e, a);
    auto u5 = __builtin_rvtt_sfpxor(a, c);
    auto u6 = __builtin_rvtt_sfpxor(b, d);
    auto u7 = __builtin_rvtt_sfpxor(c, e);
    auto r0 = __builtin_rvtt_sfpxor(u0, u1);
    auto r1 = __builtin_rvtt_sfpxor(u2, u3);
    auto r2 = __builtin_rvtt_sfpxor(u4, u5);
    auto r3 = __builtin_rvtt_sfpxor(u6, u7);
    auto s0 = __builtin_rvtt_sfpxor(r0, r1);
    auto s1 = __builtin_rvtt_sfpxor(r2, r3);
    auto out = __builtin_rvtt_sfpxor(s0, s1);
    PRERA_STORE5;
}

inline void body_seq_int()
{
    PRERA_LOAD5;
    auto u0 = __builtin_rvtt_sfpxor(a, b);
    auto u1 = __builtin_rvtt_sfpxor(b, c);
    auto r0 = __builtin_rvtt_sfpxor(u0, u1);
    auto u2 = __builtin_rvtt_sfpxor(c, d);
    auto u3 = __builtin_rvtt_sfpxor(d, e);
    auto r1 = __builtin_rvtt_sfpxor(u2, u3);
    auto s0 = __builtin_rvtt_sfpxor(r0, r1);
    auto u4 = __builtin_rvtt_sfpxor(e, a);
    auto u5 = __builtin_rvtt_sfpxor(a, c);
    auto r2 = __builtin_rvtt_sfpxor(u4, u5);
    auto u6 = __builtin_rvtt_sfpxor(b, d);
    auto u7 = __builtin_rvtt_sfpxor(c, e);
    auto r3 = __builtin_rvtt_sfpxor(u6, u7);
    auto s1 = __builtin_rvtt_sfpxor(r2, r3);
    auto out = __builtin_rvtt_sfpxor(s0, s1);
    PRERA_STORE5;
}

#define PRERA_FSTORE4                                                   \
    __builtin_rvtt_sfpstore(nullptr, r0, 192, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, r1, 194, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, r2, 196, 0, 0, 4, 7);              \
    __builtin_rvtt_sfpstore(nullptr, s0, 198, 0, 0, 4, 7)

inline void body_wide_float()
{
    PRERA_LOAD5;
    auto u0 = __builtin_rvtt_sfpmul(a, b, 0);
    auto u1 = __builtin_rvtt_sfpmul(b, c, 0);
    auto u2 = __builtin_rvtt_sfpmul(c, d, 0);
    auto u3 = __builtin_rvtt_sfpmul(d, e, 0);
    auto u4 = __builtin_rvtt_sfpmul(e, a, 0);
    auto u5 = __builtin_rvtt_sfpadd(a, c, 0);
    auto u6 = __builtin_rvtt_sfpadd(b, d, 0);
    auto u7 = __builtin_rvtt_sfpadd(c, e, 0);
    auto r0 = __builtin_rvtt_sfpmad(u0, u1, u2, 0);
    auto r1 = __builtin_rvtt_sfpmad(u3, u4, u5, 0);
    auto r2 = __builtin_rvtt_sfpadd(u6, u7, 0);
    auto s0 = __builtin_rvtt_sfpmad(r0, r1, r2, 0);
    PRERA_FSTORE4;
}

inline void body_seq_float()
{
    PRERA_LOAD5;
    auto u0 = __builtin_rvtt_sfpmul(a, b, 0);
    auto u1 = __builtin_rvtt_sfpmul(b, c, 0);
    auto u2 = __builtin_rvtt_sfpmul(c, d, 0);
    auto r0 = __builtin_rvtt_sfpmad(u0, u1, u2, 0);
    auto u3 = __builtin_rvtt_sfpmul(d, e, 0);
    auto u4 = __builtin_rvtt_sfpmul(e, a, 0);
    auto u5 = __builtin_rvtt_sfpadd(a, c, 0);
    auto r1 = __builtin_rvtt_sfpmad(u3, u4, u5, 0);
    auto u6 = __builtin_rvtt_sfpadd(b, d, 0);
    auto u7 = __builtin_rvtt_sfpadd(c, e, 0);
    auto r2 = __builtin_rvtt_sfpadd(u6, u7, 0);
    auto s0 = __builtin_rvtt_sfpmad(r0, r1, r2, 0);
    PRERA_FSTORE4;
}

inline void probe_body()
{
    if constexpr (PROBE_MODE == 0)
        body_identity();
    else if constexpr (PROBE_MODE == 1)
        body_rowtag();
    else if constexpr (PROBE_MODE == 2)
        body_lanetag();
    else if constexpr (PROBE_MODE == 3)
        body_wide_int();
    else if constexpr (PROBE_MODE == 4)
        body_seq_int();
    else if constexpr (PROBE_MODE == 5)
        body_wide_float();
    else
        body_seq_float();
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
    math::reset_counters(p_setrwc::SET_ABD_F);
    _llk_math_welfords_sfpu_params_(+[]()
    {
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
