/* Transpose-involution formation and Dst-park elision.
   Copyright (C) 2026 Tenstorrent Inc.

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

/* -mtt-tensix-optimize-transp-involution (default off).

   THE PROBLEM.  Architectural SFPTRANSP permutes BOTH four-register
   banks (SFPTRANSP.md: Transpose4(0) and Transpose4(4); craq-sim
   TENSIX_EXECUTE_SFPTRANSP agrees).  The typed `subvec_transp' tuple
   (rvtt_sfptransp_int) models only its four operands and is
   DELIBERATELY UNAUDITED, so any value the allocator leaves in the
   companion bank across a transpose is silently lane-scrambled.  Clean
   semantic bodies therefore PARK live values in Dst scratch around
   every transpose (2 stores + 2 loads a block in the welford shape).

   THE MECHANISM (rename-through-permutation, composed to identity).
   The rotating-register-file lineage (Dehnert/Hsu/Bratt, "Compiling
   for the Cydra 5"; Rau, "Iterative Modulo Scheduling", MICRO-27)
   tracks values THROUGH a deterministic hardware renaming instead of
   moving data.  For SFPTRANSP the permutation acts on (register,
   lane-subgroup) pairs -- element (reg B+i, lane j*8+c) swaps with
   (reg B+j, lane i*8+c) -- so ONE transpose scatters a whole-register
   value across all four registers of its bank at quarter-register
   granularity: no whole-register name survives, and no typed consumer
   can read the scattered value.  The only composition under which
   whole-register values become addressable again is the involution
   pi*pi = identity: TWO transposes with only full-bank L0-L3
   definitions between them.  That composed-identity case is exactly
   the hand kernels' TRANSP / loads / TRANSP idiom, and it is the case
   this pass forms:

     r = sfptransp (a, b, c, d)   where a..d are single-use results of
                                  four constant-address, no-increment,
                                  same-format SFPLOADs in this block
       ==>
     r = sfptransp_gather (addr0..3, mod0, addr_mode, 0)

   one ATOMIC multi-word instruction (SFPTRANSP; 4x SFPLOAD; SFPTRANSP,
   pattern rvtt_sfptransp_gather_int) whose net effect on the L4-L7
   companion bank is the identity, so the register allocator may keep
   values live across it.  Atomicity is what makes the mid-bundle
   scramble unobservable: no spill, reload, or scheduled insn can land
   inside the window.

   LANE-STATE OBLIGATION.  Under a partial lane-enable state the
   transpose pair is NOT an involution (mixed-enable swap pairs move
   one way only) and partially-enabled loads do not fully overwrite
   L0-L3, so formation requires the all-lanes state.  This pass takes
   NO reaching-state axiom for that hardware claim: it PROVES the state
   by dominating word-exact all-lanes SFPENCC (the capability-table
   encoding, rvtt_macro::sfpencc_all_lanes_word) with only proven
   lane-state-preserving statements between, or it FORCES the state by
   materializing that SFPENCC itself at the head of the formation
   group.  Emitting an all-lanes enable outside every typed CC region
   is the same emission the structured-CC lowering (gimple-rvtt-cc.cc)
   already performs at region exits; any typed CC writer other than the
   word-exact all-lanes enable anywhere in the function refuses the
   whole function (transp-involution-cc-region, conservative v1).

   PARK ELISION.  With the bundle formed, a Dst park around it is a
   store/load identity round-trip:

     sfpstore (v, A, M, noinc); ... bundle ...; w = sfpload (A, M, noinc)

   forwards v to w's uses and deletes the load, provided (all proven,
   refusing default):
     - M is a bit-exact round-trip format (audited table below);
     - v is provably never a nonzero fp32 denormal when M's store arm
       flushes denormals (audited producer classes below);
     - every statement between store and load is proven free of
       Dst writes overlapping A's physical rows, of RWC effects, of
       configuration writes, and of unaudited raw words (the value's
       new register lifetime must also cross no hidden raw LREG
       writer);
     - the store and load both sit at proven all-lanes points (a
       partially-enabled store or load would not round-trip the full
       register);
     - at most four values end up live across the bundle (the
       companion bank's capacity; more would force an unsupported
       SFPU spill).
   Park stores whose loads all forwarded die when a later same-address
   same-format store overwrites them with no intervening overlapping
   read (bounded dead-store elimination); the body's final deposit
   stores survive, so the externally observable Dst state is unchanged.

   AUDITED ARCHITECTURAL FACTS used here (spec + corrected-simulator
   provenance cited at each table):
     - SFPTRANSP permutation and lane gating: SFPTRANSP.md functional
       model; craq-sim TENSIX_EXECUTE_SFPTRANSP.
     - SFPLOAD/SFPSTORE lane gating, address->physical-row mapping and
       format codecs: SFPLOAD.md/SFPSTORE.md; craq-sim
       TENSIX_EXECUTE_SFPLOAD / sfpstore_values / read_dst32b /
       write_dst32b / dst32b_adjust_row / encode_fp32 / decode_fp32 /
       denormals_as_zeros.
     - no-increment address mode: rvtt_no_increment_address_mode
       (the platform contract the dst-autoincr pass stands on).
     - FMA-family results are never nonzero denormals: craq-sim
       fma.cpp fma_model_bh / fma_model_wh (input and output flush).
     - fixed constant registers 8/9/10: craq-sim tensix reset state
       (0x3F56594B / 0 / 0x3F800000), never nonzero denormals;
       programmable constant registers 11..14 refuse.

   REFUSAL NAMES (dump-visible, append-only):
     transp-involution-unsupported-target, transp-involution-cc-region,
     transp-involution-operand-shape, transp-involution-window,
     transp-park-format, transp-park-denormal-unproven,
     transp-park-lane-state, transp-park-window,
     transp-park-pressure.

   All refusals leave the function byte-identical.  */

#define INCLUDE_VECTOR
#define INCLUDE_ALGORITHM
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-pretty-print.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "ssa-iterators.h"
#include "tree-ssanames.h"
#include "tree-cfg.h"
#include "cfghooks.h"
#include "cfganal.h"
#include "dominance.h"
#include "fold-const.h"
#include "rvtt-protos.h"
#include "rvtt.h"
#include "rvtt-effects.h"
#include "rvtt-raw-boundary.h"
#include "rvtt-macro-tables.h"
#include "tree-dfa.h"

namespace {

#define DUMP(...) if (dump_file) fprintf (dump_file, __VA_ARGS__)

/* ------------------------------------------------------------------ */
/* Statement classification (refusing default).			      */

enum stmt_class
{
  SC_SKIP,	/* debug / label / nop: no code			      */
  SC_SAFE,	/* proven: no Dst access, no RWC, no config write, no
		   CC write, no hidden (raw) LREG write		      */
  SC_DST_READ,	/* typed constant-address Dst load (access known)     */
  SC_DST_WRITE,	/* typed constant-address Dst store (access known)    */
  SC_GATHER,	/* an already-formed involution bundle: four known
		   Dst reads; lane-state preserving		      */
  SC_ENCC_ALL,	/* word-exact all-lanes SFPENCC: lane-state forcing   */
  SC_CC_WRITE,	/* any other typed CC writer: refuses the function    */
  SC_BARRIER,	/* everything unproven				      */
};

struct dst_access
{
  unsigned addr;
  unsigned mod0;
  unsigned addr_mode;
  tree value;		/* stored value (SC_DST_WRITE only)  */
};

/* Typed builtins proven to have no Dst-memory access, no RWC effect,
   no configuration write, and no hidden fixed-LREG contract.  A member
   may still write CC through a mod operand; callers check
   insnd->sets_cc on the concrete call and classify SC_CC_WRITE first.
   Everything absent from this list keeps the refusing default
   (SC_BARRIER).  */

static bool
safe_compute_id_p (rvtt_insn_data::insn_id id)
{
  switch (id)
    {
    case rvtt_insn_data::synth_opcode:
    case rvtt_insn_data::sfpnop:
    case rvtt_insn_data::sfpnovalue:
    case rvtt_insn_data::sfpselect2:
    case rvtt_insn_data::sfpselect4:
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
    case rvtt_insn_data::sfploadi:
    case rvtt_insn_data::sfploadi_lv:
    case rvtt_insn_data::sfpxloadi:
    case rvtt_insn_data::sfpmov:
    case rvtt_insn_data::sfpmov_lv:
    case rvtt_insn_data::sfpexexp:
    case rvtt_insn_data::sfpexexp_lv:
    case rvtt_insn_data::sfpexman:
    case rvtt_insn_data::sfpexman_lv:
    case rvtt_insn_data::sfpabs:
    case rvtt_insn_data::sfpabs_lv:
    case rvtt_insn_data::sfplz:
    case rvtt_insn_data::sfplz_lv:
    case rvtt_insn_data::sfpand:
    case rvtt_insn_data::sfpand_lv:
    case rvtt_insn_data::sfpor:
    case rvtt_insn_data::sfpor_lv:
    case rvtt_insn_data::sfpxor:
    case rvtt_insn_data::sfpxor_lv:
    case rvtt_insn_data::sfpnot:
    case rvtt_insn_data::sfpnot_lv:
    case rvtt_insn_data::sfpshft_v:
    case rvtt_insn_data::sfpshft_v_lv:
    case rvtt_insn_data::sfpshft_i:
    case rvtt_insn_data::sfpshft_i_lv:
    case rvtt_insn_data::sfpiadd_v:
    case rvtt_insn_data::sfpiadd_v_lv:
    case rvtt_insn_data::sfpiadd_i:
    case rvtt_insn_data::sfpiadd_i_lv:
    case rvtt_insn_data::sfpxiadd_v:
    case rvtt_insn_data::sfpxiadd_i:
    case rvtt_insn_data::sfpxiadd_i_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
    case rvtt_insn_data::sfpsetexp_v:
    case rvtt_insn_data::sfpsetexp_v_lv:
    case rvtt_insn_data::sfpsetexp_i:
    case rvtt_insn_data::sfpsetexp_i_lv:
    case rvtt_insn_data::sfpsetman_v:
    case rvtt_insn_data::sfpsetman_v_lv:
    case rvtt_insn_data::sfpsetman_i:
    case rvtt_insn_data::sfpsetman_i_lv:
    case rvtt_insn_data::sfpsetsgn_v:
    case rvtt_insn_data::sfpsetsgn_v_lv:
    case rvtt_insn_data::sfpsetsgn_i:
    case rvtt_insn_data::sfpsetsgn_i_lv:
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpdivp2:
    case rvtt_insn_data::sfpdivp2_lv:
    case rvtt_insn_data::sfpcast:
    case rvtt_insn_data::sfpcast_lv:
    case rvtt_insn_data::sfpstochrnd_i:
    case rvtt_insn_data::sfpstochrnd_i_lv:
    case rvtt_insn_data::sfpstochrnd_v:
    case rvtt_insn_data::sfpstochrnd_v_lv:
    case rvtt_insn_data::sfplut:
    case rvtt_insn_data::sfplutfp32_3r:
    case rvtt_insn_data::sfplutfp32_6r:
    case rvtt_insn_data::sfpswap:
    case rvtt_insn_data::sfpmul24:
    case rvtt_insn_data::sfpmul24_lv:
    case rvtt_insn_data::sfparecip:
    case rvtt_insn_data::sfparecip_lv:
    case rvtt_insn_data::sfpnonlinear:
    case rvtt_insn_data::sfpnonlinear_lv:
    case rvtt_insn_data::sfpreadconfig:
    case rvtt_insn_data::sfpreadconfig_lv:
    case rvtt_insn_data::sfpreadlreg:
      return true;
    default:
      return false;
    }
}

/* Typed CC writers that are unconditionally CC writes (independent of a
   mod operand).  The word-exact all-lanes SFPENCC is handled separately
   (SC_ENCC_ALL).  */

static bool
cc_writer_id_p (rvtt_insn_data::insn_id id)
{
  switch (id)
    {
    case rvtt_insn_data::sfpsetcc_i:
    case rvtt_insn_data::sfpsetcc_v:
    case rvtt_insn_data::sfpencc:
    case rvtt_insn_data::sfpcompc:
    case rvtt_insn_data::sfppushc:
    case rvtt_insn_data::sfppopc:
    case rvtt_insn_data::sfpxvif:
    case rvtt_insn_data::sfpxbool:
    case rvtt_insn_data::sfpxcondb:
    case rvtt_insn_data::sfpxcondi:
    case rvtt_insn_data::sfpxicmps:
    case rvtt_insn_data::sfpxicmpv:
    case rvtt_insn_data::sfpxfcmps:
    case rvtt_insn_data::sfpxfcmpv:
    case rvtt_insn_data::sfpgt:
    case rvtt_insn_data::sfpgt_lv:
    case rvtt_insn_data::sfple:
    case rvtt_insn_data::sfple_lv:
      return true;
    default:
      return false;
    }
}

static bool
const_uarg (gcall *call, unsigned argno, unsigned *out)
{
  if (gimple_call_num_args (call) <= argno)
    return false;
  tree arg = gimple_call_arg (call, argno);
  if (TREE_CODE (arg) != INTEGER_CST || !tree_fits_uhwi_p (arg))
    return false;
  *out = (unsigned) tree_to_uhwi (arg);
  return true;
}

/* Word-exact all-lanes SFPENCC call?  Proven against the capability
   table's architectural encoding, mirroring the RTL
   cc_write_all_lanes derivation (rvtt-effects.cc): the builtin's
   argument order is (mod1, imm12), the emission's operand roles.  */

static bool
encc_all_lanes_call_p (gcall *call, const rvtt_insn_data *insnd)
{
  if (insnd->id != rvtt_insn_data::sfpencc)
    return false;
  unsigned mod1, imm12;
  if (!const_uarg (call, 0, &mod1) || !const_uarg (call, 1, &imm12))
    return false;
  uint32_t word;
  return rvtt_macro::sfpencc_encode (imm12, mod1, &word)
	 && word == rvtt_macro::sfpencc_all_lanes_word ();
}

/* Parse a typed constant-address Dst access.  sfpload args:
   (instrn_ptr, addr, var, id, mod0, addr_mode); sfpstore args:
   (instrn_ptr, value, addr, var, id, mod0, addr_mode).  Only the fully
   static shape (null instruction pointer, zero var/id synth operands,
   constant fields) is admitted.  */

static bool
parse_dst_access (gcall *call, const rvtt_insn_data *insnd, dst_access *acc)
{
  bool store = insnd->id == rvtt_insn_data::sfpstore;
  unsigned base = store ? 1 : 0;
  tree ptr = gimple_call_arg (call, 0);
  if (!integer_zerop (ptr))
    return false;
  unsigned var, id;
  if (!const_uarg (call, base + 1, &acc->addr)
      || !const_uarg (call, base + 2, &var) || var != 0
      || !const_uarg (call, base + 3, &id) || id != 0
      || !const_uarg (call, base + 4, &acc->mod0)
      || !const_uarg (call, base + 5, &acc->addr_mode))
    return false;
  acc->value = store ? gimple_call_arg (call, 1) : NULL_TREE;
  return true;
}

struct classified
{
  stmt_class cls;
  dst_access acc;		/* SC_DST_READ / SC_DST_WRITE  */
  unsigned gather_addr[4];	/* SC_GATHER		       */
  unsigned gather_mod0;
};

static classified
classify_stmt (gimple *stmt)
{
  classified c;
  c.cls = SC_BARRIER;

  if (is_gimple_debug (stmt) || gimple_code (stmt) == GIMPLE_LABEL
      || gimple_code (stmt) == GIMPLE_NOP || gimple_code (stmt) == GIMPLE_PREDICT)
    {
      c.cls = SC_SKIP;
      return c;
    }

  if (gasm *a = dyn_cast <gasm *> (stmt))
    {
      /* An empty template emits nothing (pure barrier: the compiler
	 memory-clobber idiom).  Every real asm keeps the refusing
	 default: raw words may carry hidden LREG/CC/Dst effects the
	 forwarding windows must not cross.  */
      const char *s = gimple_asm_string (a);
      while (s && (*s == ' ' || *s == '\t'))
	++s;
      if (s && !*s)
	c.cls = SC_SAFE;
      return c;
    }

  if (gimple_code (stmt) == GIMPLE_COND || gimple_code (stmt) == GIMPLE_PHI)
    {
      c.cls = SC_SAFE;		/* scalar control flow: no Tensix word */
      return c;
    }

  gcall *call = dyn_cast <gcall *> (stmt);
  if (!call)
    {
      /* Plain gimple: scalar/SSA data flow.  It cannot reach the SFPU
	 register file, lane state, RWC, or Dst (precedent:
	 gimple-rvtt-prgm-const.cc's TU scan, which likewise confines
	 the audit to asm and calls).  */
      c.cls = SC_SAFE;
      return c;
    }

  const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
  if (!insnd)
    {
      if (gimple_call_internal_p (call))
	{
	  c.cls = SC_SAFE;
	  return c;
	}
      tree fndecl = gimple_call_fndecl (call);
      if (fndecl && fndecl_built_in_p (fndecl))
	{
	  c.cls = SC_SAFE;	/* scalar compiler builtin */
	  return c;
	}
      return c;			/* real call: barrier */
    }

  if (insnd->id == rvtt_insn_data::sfptransp_gather)
    {
      gcall *g = call;
      unsigned mode;
      bool ok = true;
      for (unsigned i = 0; i != 4; ++i)
	ok &= const_uarg (g, i, &c.gather_addr[i]);
      ok &= const_uarg (g, 4, &c.gather_mod0);
      ok &= const_uarg (g, 5, &mode);
      c.cls = ok ? SC_GATHER : SC_BARRIER;
      return c;
    }

  if (encc_all_lanes_call_p (call, insnd))
    {
      c.cls = SC_ENCC_ALL;
      return c;
    }

  if (cc_writer_id_p (insnd->id) || insnd->sets_cc (call))
    {
      c.cls = SC_CC_WRITE;
      return c;
    }

  if (insnd->id == rvtt_insn_data::sfpload
      || insnd->id == rvtt_insn_data::sfpstore)
    {
      if (parse_dst_access (call, insnd, &c.acc))
	c.cls = insnd->id == rvtt_insn_data::sfpstore
		? SC_DST_WRITE : SC_DST_READ;
      return c;
    }

  if (safe_compute_id_p (insnd->id))
    {
      c.cls = SC_SAFE;
      return c;
    }

  return c;			/* refusing default */
}

/* ------------------------------------------------------------------ */
/* Audited Dst physical-row model (BH/WH).

   From the simulator's lane loops (TENSIX_EXECUTE_SFPLOAD,
   sfpstore_values): an access at constant address A touches lane rows
   r in [A & ~3, (A & ~3) + 3].  The 32-bit format class maps lane row
   r to physical rows adj32(r) and adj32(r)+8 with
   adj32(r) = ((r & 0x1F8) << 1) | (r & 0x207) (dst32b_adjust_row; the
   BH remap/swizzle configuration bits are debug features the SFPI
   platform contract leaves clear).  The 16-bit class maps r to itself
   (dst16b_adjust_row) OR through the 32-bit path when the 32-bit
   layout is configured -- layout-configuration dependent, so its row
   set is modeled as the UNION of both.  Formats whose class is itself
   configuration-resolved (mod0 0) also take the union.  The result is
   a conservative superset used ONLY for disjointness proofs.  */

static void
access_rows (const dst_access &acc, std::vector<unsigned> *rows)
{
  unsigned r0 = acc.addr & ~3u;
  bool m32 = acc.mod0 == 3 || acc.mod0 == 4 || acc.mod0 == 7
	     || acc.mod0 == 9 || acc.mod0 == 12 || acc.mod0 == 10;
  bool m16 = !m32;		/* incl. mod0 0: config-resolved, union */
  if (acc.mod0 == 0)
    m32 = true;
  for (unsigned r = r0; r != r0 + 4; ++r)
    {
      unsigned lane_row = r & 1023;
      if (m32 || m16)
	{
	  unsigned adj = ((lane_row & 0x1F8) << 1) | (lane_row & 0x207);
	  if (m32)
	    {
	      rows->push_back (adj & 1023);
	      rows->push_back ((adj + 8) & 1023);
	    }
	  if (m16)
	    {
	      rows->push_back (lane_row);    /* 16-bit layout */
	      rows->push_back (adj & 1023);  /* 32-bit layout via dst16->32 */
	      rows->push_back ((adj + 8) & 1023);
	    }
	}
    }
}

static bool
rows_disjoint (const dst_access &a, const dst_access &b)
{
  std::vector<unsigned> ra, rb;
  access_rows (a, &ra);
  access_rows (b, &rb);
  for (unsigned x : ra)
    if (std::find (rb.begin (), rb.end (), x) != rb.end ())
      return false;
  return true;
}

/* ------------------------------------------------------------------ */
/* Bit-exact park-format audit.

   mod0 == 4 (INT32 class): the store writes encode_fp32(value) and the
   load returns decode_fp32(stored) -- exact inverse bit permutations
   (craq-sim encode_fp32/decode_fp32; "the FP32 encoding is also used
   for INT32"), so every 32-bit value round-trips.

   mod0 == 3 (FP32): same codec pair, but the store arm flushes nonzero
   denormals first (denormals_as_zeros on BH, TT_VERSION >= 1); the
   round-trip is exact iff the value is provably never a nonzero
   denormal.  The proof is applied on WH too (conservative: WH's store
   arm does not flush, so this only refuses, never miscompiles).

   Every other format is partial, lossy, or layout-dependent: refuse.  */

static bool
park_format_bit_exact_p (unsigned mod0, bool *needs_denormal_proof)
{
  if (mod0 == 4)
    {
      *needs_denormal_proof = false;
      return true;
    }
  if (mod0 == 3)
    {
      *needs_denormal_proof = true;
      return true;
    }
  return false;
}

/* Is VAL provably never a nonzero fp32 denormal?  Refusing default.
   Audited producer classes:
   - FMA-family arithmetic: craq-sim fma_model_bh / fma_model_wh flush
     denormal inputs and outputs;
   - fixed constant registers 8/9/10 (0x3F56594B / 0 / 0x3F800000, the
     simulator reset state; the programmable constants 11..14 refuse);
   - literal materializations whose fp32 image is not a nonzero
     denormal.  */

static bool
value_never_denormal_p (tree val, unsigned depth = 0)
{
  if (depth > 4 || TREE_CODE (val) != SSA_NAME)
    return false;
  gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (val));
  if (!def)
    return false;
  const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
  if (!insnd)
    return false;
  switch (insnd->id)
    {
    case rvtt_insn_data::sfpmad:
    case rvtt_insn_data::sfpmad_lv:
    case rvtt_insn_data::sfpadd:
    case rvtt_insn_data::sfpadd_lv:
    case rvtt_insn_data::sfpmul:
    case rvtt_insn_data::sfpmul_lv:
    case rvtt_insn_data::sfpmuli:
    case rvtt_insn_data::sfpmuli_lv:
    case rvtt_insn_data::sfpaddi:
    case rvtt_insn_data::sfpaddi_lv:
      return true;
    case rvtt_insn_data::sfpreadlreg:
      {
	unsigned reg;
	return const_uarg (def, 0, &reg) && (reg == 8 || reg == 9 || reg == 10);
      }
    case rvtt_insn_data::sfpassign:
    case rvtt_insn_data::sfpassign_lv:
      /* Pass-through of its (last) source.  */
      return value_never_denormal_p
	(gimple_call_arg (def, gimple_call_num_args (def) - 1), depth + 1);
    default:
      return false;
    }
}

/* ------------------------------------------------------------------ */

struct candidate
{
  gcall *transp;		/* the sfptransp call		*/
  gcall *defs[4];		/* the four sfpload defs	*/
  unsigned addr[4];
  unsigned mod0, addr_mode;
  gimple *group_head;		/* earliest stmt of the contiguous
				   lane-safe run before the transp */
  bool needs_encc;
};

/* Uid order helpers: uids are assigned once, insertions reuse the
   neighbor's uid region so ordering stays consistent enough for the
   same-BB comparisons used here (we only ever compare pre-existing
   statements).  */

/* Match one transpose call against the involution shape.  */

static bool
match_candidate (gcall *transp, candidate *cand, const char **why)
{
  *why = "transp-involution-operand-shape";
  if (gimple_call_num_args (transp) != 4)
    return false;
  int noinc = rvtt_no_increment_address_mode ();
  if (noinc < 0)
    {
      *why = "transp-involution-unsupported-target";
      return false;
    }
  basic_block bb = gimple_bb (transp);
  for (unsigned i = 0; i != 4; ++i)
    {
      tree arg = gimple_call_arg (transp, i);
      if (TREE_CODE (arg) != SSA_NAME || !has_single_use (arg))
	return false;
      gcall *def = dyn_cast <gcall *> (SSA_NAME_DEF_STMT (arg));
      if (!def || gimple_bb (def) != bb)
	return false;
      const rvtt_insn_data *insnd = rvtt_get_insn_data (def);
      if (!insnd || insnd->id != rvtt_insn_data::sfpload)
	return false;
      dst_access acc;
      if (!parse_dst_access (def, insnd, &acc))
	return false;
      if ((int) acc.addr_mode != noinc)
	return false;
      cand->defs[i] = def;
      cand->addr[i] = acc.addr;
      if (i == 0)
	cand->mod0 = acc.mod0;
      else if (acc.mod0 != cand->mod0)
	return false;
    }
  cand->addr_mode = (unsigned) noinc;

  /* The loads sink to the transpose: everything strictly between the
     earliest def and the transpose must be a sibling def or a proven
     lane-, Dst-, RWC-, config- and raw-free statement.  */
  gimple *first = cand->defs[0];
  for (unsigned i = 1; i != 4; ++i)
    if (gimple_uid (cand->defs[i]) < gimple_uid (first))
      first = cand->defs[i];
  *why = "transp-involution-window";
  for (gimple_stmt_iterator gsi = gsi_for_stmt (first);
       gsi_stmt (gsi) != (gimple *) transp; gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == cand->defs[0] || stmt == cand->defs[1]
	  || stmt == cand->defs[2] || stmt == cand->defs[3])
	continue;
      classified c = classify_stmt (stmt);
      if (c.cls != SC_SKIP && c.cls != SC_SAFE)
	return false;
    }

  /* Group head: the earliest point of the contiguous run of proven
     lane-state-preserving statements before the first def -- where a
     forced all-lanes enable would cover the whole formation group,
     parked stores included.  */
  gimple *head = first;
  for (gimple_stmt_iterator gsi = gsi_for_stmt (first);;)
    {
      if (gsi_stmt (gsi) == gsi_start_bb (bb).ptr)
	break;
      gsi_prev (&gsi);
      classified c = classify_stmt (gsi_stmt (gsi));
      if (c.cls == SC_SKIP || c.cls == SC_SAFE || c.cls == SC_DST_READ
	  || c.cls == SC_DST_WRITE || c.cls == SC_GATHER)
	head = gsi_stmt (gsi);
      else
	break;
    }
  cand->group_head = head;
  cand->transp = transp;
  *why = nullptr;
  return true;
}

/* ------------------------------------------------------------------ */

class involution_transform
{
public:
  involution_transform (function *fn) : m_fn (fn) {}
  bool run ();

private:
  void find_candidates ();
  void compute_lane_states ();
  gcall *rewrite_candidate (candidate &cand);
  void forward_parks (gcall *bundle, basic_block bb);
  void dse_parks (basic_block bb);
  unsigned live_across_count (gimple *site);

  function *m_fn;
  std::vector<candidate> m_cands;
  /* Lane state CLEAN at BB entry?  */
  auto_bitmap m_clean_in;
};

/* Find all formable transposes.  */

void
involution_transform::find_candidates ()
{
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gcall *call = dyn_cast <gcall *> (gsi_stmt (gsi));
	if (!call)
	  continue;
	const rvtt_insn_data *insnd = rvtt_get_insn_data (call);
	if (!insnd || insnd->id != rvtt_insn_data::sfptransp)
	  continue;
	candidate cand;
	const char *why;
	if (match_candidate (call, &cand, &why))
	  {
	    DUMP ("transp-involution: forming bundle at uid %u "
		  "(addrs %u %u %u %u, mod0 %u)\n",
		  gimple_uid (call), cand.addr[0], cand.addr[1],
		  cand.addr[2], cand.addr[3], cand.mod0);
	    m_cands.push_back (cand);
	  }
	else
	  DUMP ("transp-involution: refusing transpose at uid %u (%s)\n",
		gimple_uid (call), why);
      }
}

/* Forward lane-state dataflow: CLEAN = all-lanes proven since a
   word-exact all-lanes SFPENCC (typed or one this pass will place),
   through proven lane-state-preserving statements only.  Entry is
   DIRTY: no reaching-state axiom is taken for the hardware claim.
   Candidate sites force CLEAN (either they inherit it or the rewrite
   materializes the enable at their group head).  */

void
involution_transform::compute_lane_states ()
{
  bool changed = true;
  while (changed)
    {
      changed = false;
      basic_block bb;
      FOR_EACH_BB_FN (bb, m_fn)
	{
	  bool in_clean = false;
	  if (bb != ENTRY_BLOCK_PTR_FOR_FN (m_fn)
	      && EDGE_COUNT (bb->preds) > 0)
	    {
	      in_clean = true;
	      edge e;
	      edge_iterator ei;
	      FOR_EACH_EDGE (e, ei, bb->preds)
		if (e->src == ENTRY_BLOCK_PTR_FOR_FN (m_fn)
		    || !bitmap_bit_p (m_clean_in, e->src->index))
		  {
		    /* Predecessor's OUT: recompute below; we store OUT
		       in the same bitmap keyed by src, updated when the
		       src block is walked.  */
		    in_clean = false;
		    break;
		  }
	    }
	  bool state = in_clean;
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    {
	      gimple *stmt = gsi_stmt (gsi);
	      bool is_site = false;
	      for (const candidate &cand : m_cands)
		if (cand.transp == stmt)
		  {
		    is_site = true;
		    break;
		  }
	      if (is_site)
		{
		  state = true;	/* forced or inherited */
		  continue;
		}
	      classified c = classify_stmt (stmt);
	      switch (c.cls)
		{
		case SC_SKIP: case SC_SAFE: case SC_DST_READ:
		case SC_DST_WRITE: case SC_GATHER:
		  break;
		case SC_ENCC_ALL:
		  state = true;
		  break;
		default:
		  state = false;
		  break;
		}
	    }
	  if (state != bitmap_bit_p (m_clean_in, bb->index))
	    {
	      if (state)
		bitmap_set_bit (m_clean_in, bb->index);
	      else
		bitmap_clear_bit (m_clean_in, bb->index);
	      changed = true;
	    }
	}
    }
  /* m_clean_in now holds each block's OUT state; IN is recomputed from
     preds when consumed (walk_state_to below).  */
}

/* Lane state immediately before STMT in BB (using the fixpoint OUT
   states of the predecessors).  */

static bool
in_state_of_bb (function *fn, basic_block bb, bitmap outs)
{
  if (bb == ENTRY_BLOCK_PTR_FOR_FN (fn) || EDGE_COUNT (bb->preds) == 0)
    return false;
  edge e;
  edge_iterator ei;
  FOR_EACH_EDGE (e, ei, bb->preds)
    if (e->src == ENTRY_BLOCK_PTR_FOR_FN (fn)
	|| !bitmap_bit_p (outs, e->src->index))
      return false;
  return true;
}

/* Count 32-lane vector SSA values live across SITE (def before, some
   use after or in another block).  The companion bank holds four.  */

unsigned
involution_transform::live_across_count (gimple *site)
{
  basic_block bb = gimple_bb (site);
  unsigned site_uid = gimple_uid (site);
  unsigned count = 0;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (gimple_uid (stmt) >= site_uid)
	break;
      tree lhs = gimple_get_lhs (stmt);
      if (!lhs || TREE_CODE (lhs) != SSA_NAME)
	continue;
      if (TYPE_MODE (TREE_TYPE (lhs)) != XTT32SImode)
	continue;
      imm_use_iterator it;
      gimple *use;
      bool live = false;
      FOR_EACH_IMM_USE_STMT (use, it, lhs)
	if (gimple_bb (use) != bb || gimple_uid (use) > site_uid)
	  {
	    live = true;
	    break;
	  }
      count += live;
    }
  return count;
}

/* Replace the matched transpose with the fused bundle; delete the
   consumed loads; materialize the all-lanes enable at the group head
   when the reaching lane state is unproven.  */

gcall *
involution_transform::rewrite_candidate (candidate &cand)
{
  if (cand.needs_encc)
    {
      const rvtt_insn_data *encc = rvtt_get_insn_data (rvtt_insn_data::sfpencc);
      gcall *e = gimple_build_call
	(encc->decl, 2,
	 build_int_cst (unsigned_type_node, SFPENCC_MOD1_EI_RI),
	 build_int_cst (unsigned_type_node, SFPENCC_IMM12_BOTH));
      gimple_set_location (e, gimple_location (cand.transp));
      gimple_stmt_iterator hgsi = gsi_for_stmt (cand.group_head);
      gsi_insert_before (&hgsi, e, GSI_SAME_STMT);
      gimple_set_uid (e, gimple_uid (cand.group_head));
      update_stmt (e);
      DUMP ("transp-involution: materialized all-lanes enable before "
	    "uid %u\n", gimple_uid (cand.group_head));
    }

  const rvtt_insn_data *gather
    = rvtt_get_insn_data (rvtt_insn_data::sfptransp_gather);
  gcall *bundle = gimple_build_call
    (gather->decl, 6,
     build_int_cst (unsigned_type_node, cand.addr[0]),
     build_int_cst (unsigned_type_node, cand.addr[1]),
     build_int_cst (unsigned_type_node, cand.addr[2]),
     build_int_cst (unsigned_type_node, cand.addr[3]),
     build_int_cst (unsigned_type_node, cand.mod0),
     build_int_cst (unsigned_type_node, cand.addr_mode));
  gimple_call_set_lhs (bundle, gimple_call_lhs (cand.transp));
  gimple_set_location (bundle, gimple_location (cand.transp));

  gimple_stmt_iterator gsi = gsi_for_stmt (cand.transp);
  unsigned uid = gimple_uid (cand.transp);
  gsi_replace (&gsi, bundle, false);
  gimple_set_uid (bundle, uid);
  update_stmt (bundle);

  for (unsigned i = 0; i != 4; ++i)
    {
      gimple_stmt_iterator dgsi = gsi_for_stmt (cand.defs[i]);
      rvtt_prep_stmt_for_deletion (cand.defs[i]);
      gsi_remove (&dgsi, true);
    }
  return bundle;
}

/* Park forwarding around one formed bundle.  */

void
involution_transform::forward_parks (gcall *bundle, basic_block bb)
{
  unsigned site_uid = gimple_uid (bundle);

  /* Lane state must be proven at the stores and loads themselves; by
     construction the group head enable (or inherited CLEAN state)
     covers the whole contiguous group, so verify the reaching state at
     the group region once: walk back from the bundle collecting park
     stores while every crossed statement stays lane-state preserving;
     an SC_ENCC_ALL met on the way marks everything after it proven.  */

  struct pending { dst_access acc; gimple *stmt; };
  std::vector<pending> stores;

  bool state_proven_here = in_state_of_bb (m_fn, bb, m_clean_in);
  /* Statement-level: walk the block from the top tracking the state up
     to the bundle, collecting candidate park stores in the contiguous
     safe run before it.  */
  std::vector<pending> run_stores;
  bool state = state_proven_here;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      if (stmt == (gimple *) bundle)
	break;
      classified c = classify_stmt (stmt);
      switch (c.cls)
	{
	case SC_ENCC_ALL:
	  state = true;
	  run_stores.clear ();
	  break;
	case SC_SKIP:
	case SC_SAFE:
	  break;
	case SC_DST_READ:
	  break;
	case SC_GATHER:
	  break;
	case SC_DST_WRITE:
	  if (state)
	    run_stores.push_back ({ c.acc, stmt });
	  break;
	default:
	  state = false;
	  run_stores.clear ();
	  break;
	}
    }
  if (!state)
    {
      DUMP ("transp-park: no proven all-lanes state before bundle uid %u "
	    "(transp-park-lane-state)\n", site_uid);
      return;
    }
  stores = run_stores;
  if (stores.empty ())
    return;

  /* Backward window validity: between each collected store and the
     bundle there must be no Dst write overlapping the store's rows and
     no barrier -- the walk above only admitted reads, safe statements
     and other stores; verify write-overlap now.  */
  auto overlap_write = [&] (const dst_access &park, gimple *from)
  {
    for (gimple_stmt_iterator gsi = gsi_for_stmt (from);; )
      {
	gsi_next (&gsi);
	if (gsi_end_p (gsi) || gsi_stmt (gsi) == (gimple *) bundle)
	  return false;
	classified c = classify_stmt (gsi_stmt (gsi));
	if (c.cls == SC_DST_WRITE && !rows_disjoint (c.acc, park))
	  return true;
      }
  };

  /* Pressure guard: values made live across the bundle must fit the
     four-register companion bank together with everything already live
     across it.  */
  unsigned base_live = live_across_count (bundle);

  unsigned forwarded = 0;
  for (pending &p : stores)
    {
      bool needs_denorm;
      if (!park_format_bit_exact_p (p.acc.mod0, &needs_denorm))
	{
	  DUMP ("transp-park: store uid %u addr %u refused "
		"(transp-park-format mod0 %u)\n",
		gimple_uid (p.stmt), p.acc.addr, p.acc.mod0);
	  continue;
	}
      if (needs_denorm && !value_never_denormal_p (p.acc.value))
	{
	  DUMP ("transp-park: store uid %u addr %u refused "
		"(transp-park-denormal-unproven)\n",
		gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}
      if (overlap_write (p.acc, p.stmt))
	{
	  DUMP ("transp-park: store uid %u addr %u refused "
		"(transp-park-window: overlapping write)\n",
		gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}
      if (base_live + forwarded + 1 > 4)
	{
	  DUMP ("transp-park: store uid %u addr %u refused "
		"(transp-park-pressure: %u live + %u forwarded)\n",
		gimple_uid (p.stmt), p.acc.addr, base_live, forwarded);
	  continue;
	}

      /* Find the matching unpark load after the bundle: same address,
	 same format, no-increment mode, with only proven-safe or
	 disjoint statements between the bundle and the load.  */
      gcall *load = nullptr;
      bool blocked = false;
      for (gimple_stmt_iterator gsi = gsi_for_stmt ((gimple *) bundle);
	   !blocked && !load; )
	{
	  gsi_next (&gsi);
	  if (gsi_end_p (gsi))
	    break;
	  gimple *stmt = gsi_stmt (gsi);
	  classified c = classify_stmt (stmt);
	  switch (c.cls)
	    {
	    case SC_SKIP: case SC_SAFE:
	      break;
	    case SC_ENCC_ALL:
	      break;		/* re-forcing all-lanes is state-preserving
				   for a full-register read */
	    case SC_DST_READ:
	      if (c.acc.addr == p.acc.addr && c.acc.mod0 == p.acc.mod0
		  && c.acc.addr_mode == p.acc.addr_mode)
		load = as_a <gcall *> (stmt);
	      break;
	    case SC_DST_WRITE:
	      if (!rows_disjoint (c.acc, p.acc))
		blocked = true;
	      break;
	    case SC_GATHER:
	      {
		/* Reads only; reads never block forwarding.  */
		break;
	      }
	    default:
	      blocked = true;
	      break;
	    }
	}
      if (!load || !gimple_call_lhs (load))
	{
	  DUMP ("transp-park: store uid %u addr %u kept "
		"(transp-park-window: no matching unpark load)\n",
		gimple_uid (p.stmt), p.acc.addr);
	  continue;
	}

      tree lhs = gimple_call_lhs (load);
      DUMP ("transp-park: forwarding store uid %u -> load uid %u "
	    "(addr %u mod0 %u)\n", gimple_uid (p.stmt), gimple_uid (load),
	    p.acc.addr, p.acc.mod0);
      replace_uses_by (lhs, p.acc.value);
      gimple_stmt_iterator lgsi = gsi_for_stmt (load);
      rvtt_prep_stmt_for_deletion (load);
      gsi_remove (&lgsi, true);
      forwarded++;
    }
}

/* Bounded same-block dead-park-store elimination: a constant-address
   store dies when a later store of the same address and format
   overwrites it with no potentially-overlapping Dst read, RWC or
   lane-state event, or barrier in between.  The killer must sit at a
   proven all-lanes point (a partially-enabled killer would not fully
   overwrite).  */

void
involution_transform::dse_parks (basic_block bb)
{
  bool state = in_state_of_bb (m_fn, bb, m_clean_in);
  /* Collect (stmt, class) linearly with running lane state at each
     store.  */
  struct entry { gimple *stmt; classified c; bool state_after; };
  std::vector<entry> line;
  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
       gsi_next (&gsi))
    {
      gimple *stmt = gsi_stmt (gsi);
      classified c = classify_stmt (stmt);
      switch (c.cls)
	{
	case SC_ENCC_ALL: case SC_GATHER:
	  state = c.cls == SC_ENCC_ALL ? true : state;
	  break;
	case SC_SKIP: case SC_SAFE: case SC_DST_READ: case SC_DST_WRITE:
	  break;
	default:
	  state = false;
	  break;
	}
      line.push_back ({ stmt, c, state });
    }

  for (unsigned i = 0; i != line.size (); ++i)
    {
      if (line[i].c.cls != SC_DST_WRITE)
	continue;
      const dst_access &acc = line[i].c.acc;
      bool needs_denorm;
      if (!park_format_bit_exact_p (acc.mod0, &needs_denorm))
	continue;		/* only audited 32-bit-class parks */
      for (unsigned j = i + 1; j != line.size (); ++j)
	{
	  const classified &c = line[j].c;
	  if (c.cls == SC_SKIP || c.cls == SC_SAFE || c.cls == SC_ENCC_ALL)
	    continue;
	  if (c.cls == SC_DST_READ)
	    {
	      if (rows_disjoint (c.acc, acc))
		continue;
	      break;		/* potentially reads the parked rows */
	    }
	  if (c.cls == SC_GATHER)
	    {
	      bool over = false;
	      for (unsigned k = 0; k != 4 && !over; ++k)
		{
		  dst_access g { c.gather_addr[k], c.gather_mod0, 0, NULL_TREE };
		  over = !rows_disjoint (g, acc);
		}
	      if (over)
		break;
	      continue;
	    }
	  if (c.cls == SC_DST_WRITE)
	    {
	      if (c.acc.addr == acc.addr && c.acc.mod0 == acc.mod0
		  && c.acc.addr_mode == acc.addr_mode
		  && line[j].state_after)
		{
		  DUMP ("transp-park: dead park store uid %u addr %u "
			"killed by store uid %u\n",
			gimple_uid (line[i].stmt), acc.addr,
			gimple_uid (line[j].stmt));
		  gimple_stmt_iterator sgsi = gsi_for_stmt (line[i].stmt);
		  rvtt_prep_stmt_for_deletion (line[i].stmt);
		  gsi_remove (&sgsi, true);
		  line[i].c.cls = SC_SKIP;
		  break;
		}
	      continue;		/* other writes never block a kill */
	    }
	  break;		/* barrier */
	}
    }
}

bool
involution_transform::run ()
{
  /* Conservative v1 function gate: any typed CC writer other than the
     word-exact all-lanes enable refuses the whole function (the
     structured-CC regions it implies are not modeled here).  */
  basic_block bb;
  FOR_EACH_BB_FN (bb, m_fn)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      if (classify_stmt (gsi_stmt (gsi)).cls == SC_CC_WRITE)
	{
	  DUMP ("transp-involution: function refused "
		"(transp-involution-cc-region at uid %u)\n",
		gimple_uid (gsi_stmt (gsi)));
	  return false;
	}

  find_candidates ();
  if (m_cands.empty ())
    return false;

  compute_lane_states ();

  /* Decide encc placement per candidate in program order per block.  */
  FOR_EACH_BB_FN (bb, m_fn)
    {
      bool state = in_state_of_bb (m_fn, bb, m_clean_in);
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);
	  candidate *cand = nullptr;
	  for (candidate &c : m_cands)
	    if (c.transp == stmt)
	      {
		cand = &c;
		break;
	      }
	  if (cand)
	    {
	      cand->needs_encc = !state;
	      state = true;
	      continue;
	    }
	  classified c = classify_stmt (stmt);
	  if (c.cls == SC_ENCC_ALL)
	    state = true;
	  else if (c.cls != SC_SKIP && c.cls != SC_SAFE
		   && c.cls != SC_DST_READ && c.cls != SC_DST_WRITE
		   && c.cls != SC_GATHER)
	    state = false;
	}
    }

  /* Rewrite + local elision.  */
  std::vector<std::pair<gcall *, basic_block>> bundles;
  for (candidate &cand : m_cands)
    {
      basic_block cbb = gimple_bb (cand.transp);
      gcall *bundle = rewrite_candidate (cand);
      bundles.push_back ({ bundle, cbb });
    }
  for (auto &b : bundles)
    forward_parks (b.first, b.second);

  /* Dead park stores, per block containing a bundle.  */
  hash_set<basic_block> seen;
  for (auto &b : bundles)
    if (!seen.add (b.second))
      dse_parks (b.second);

  return true;
}

const pass_data pass_data_rvtt_transp_involution =
{
  GIMPLE_PASS, /* type */
  "rvtt_transp_involution", /* name */
  OPTGROUP_OTHER, /* optinfo_flags */
  TV_NONE, /* tv_id */
  PROP_ssa, /* properties_required */
  0, /* properties_provided */
  0, /* properties_destroyed */
  0, /* todo_flags_start */
  TODO_update_ssa, /* todo_flags_finish */
};

class pass_rvtt_transp_involution : public gimple_opt_pass
{
public:
  pass_rvtt_transp_involution (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_rvtt_transp_involution, ctxt)
  {}

  bool gate (function *) final override
  {
    return TARGET_XTT_TENSIX
	   && (TARGET_XTT_TENSIX_BH || TARGET_XTT_TENSIX_WH)
	   && riscv_tt_opt_transp_involution > 0;
  }

  unsigned int execute (function *fn) final override
  {
    renumber_gimple_stmt_uids (fn);
    involution_transform xf (fn);
    if (xf.run ())
      return TODO_update_ssa;
    return 0;
  }
}; // class pass_rvtt_transp_involution

} // anon namespace

gimple_opt_pass *
make_pass_rvtt_transp_involution (gcc::context *ctxt)
{
  return new pass_rvtt_transp_involution (ctxt);
}
