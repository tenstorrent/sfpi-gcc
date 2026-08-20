/* Offline template-word vocabulary enumerator (macro planner Layer 4
   tooling).
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

/* WHAT THIS IS.  Every recent formation lane found ONE missing derived
   template class by hand (the WP12 generic cast/iadd/shift classes,
   Misc.StoreMod0, lane CI's commuted SFPMUL24).  This tool inverts the
   discovery direction: it enumerates the ENCODABLE InstructionTemplate
   word space from the typed capability tables and a typed per-opcode
   semantic model (both provenance-cited), derives which (opcode, mod1,
   operand-binding) classes the SFPLOADMACRO realization can host
   EXACTLY, and emits candidate derive-core admission rows ranked by
   corpus hosting value -- so vocabulary holes are found by
   enumeration, not by lanes tripping over them.  This is the
   enumerate->fingerprint->validate loop of Ruler (OOPSLA'21) /
   Enumo, with VeGen's InstSema (ASPLOS'21) as the per-instruction
   semantic-model precedent, applied to our template-word space
   (laneCO PROPOSALS.md P5).

   THE REALIZATION MODEL (what "exact" means).  A hosted value event
   executes the stored 32-bit template word with these overrides
   (SFPLOADMACRO.md; CRAQ build_dispatch, sfploadmacro_events.h:384-424
   at craq-sim 9f324140 -- mirrored by rvtt-macro-tables.cc
   opcode_route_class):
     1. The VD field (word bits 7:4) is UNCONDITIONALLY replaced by the
	launch VD (or LReg16 under the VD16 sequence bit, which escapes
	word rewriting entirely).
     2. Route bit SET: the VB subfield (bits 15:12) is replaced by the
	launch VD -- only for the VB+VC opcode class (0x84-0x86, 0x8e,
	0x98 = RC_VB_VC).
     3. Route bit CLEAR: the VC field (bits 11:8) is replaced by the
	launch VD -- for RC_VB_VC, RC_SHFT2 (0x94) and the RC_VC class
	(0x79-0x83, 0x89, 0x90, 0x97, 0x99).
     4. Every other field survives VERBATIM from the template word.
   A class is EXACT when, for the claimed operand binding, the realized
   word is bit-identical to the explicit instruction word AND every
   overridden or zero-packed field is architecturally unread in that
   mod (the typed op model below).  Anything short of exact keeps the
   fail-closed descriptor-program-unproven refusal.

   THE OP MODEL.  Per (opcode, mod1): which encoding fields the
   execution reads, what it writes, CC effects, and hidden state.
   Every row cites its provenance, grounded FIRST in the owner ISA
   functional models (tt-isa-documentation
   BlackholeA0/TensixTile/TensixCoprocessor .md files -- executable
   pseudocode; WH-shared documents resolve into the WormholeB0 tree),
   cross-checked against the CRAQ executor (craq-sim src/tensix.cpp at
   9f324140, BH decode tables data/bh/tensix_isa.json), which is the
   differential ORACLE for admissions.  Where doc and sim DISAGREE the
   tool takes the intersection (fail-closed) and reports the
   disagreement -- see the doc/sim divergence section of the report;
   a divergence is a finding, never something to silently pick a side
   on.  The sim's reserved-bit facts matter: the generated decoder
   REFUSES set bits outside the declared fields (tensix_gen_decode.py
   TTSIM_VERIFY), so "field unread" also means "field must pack
   zero".

   OUTPUT.  --report (default): enumeration statistics, the admitted
   set (self-check against the derive-core classes of
   rvtt-macro-desc.cc, which this tool mirrors -- keep the two in sync
   commit by commit), candidate admissions ranked by hosting value,
   and named structural walls.  --diff-vectors: machine-readable
   DIFFVEC lines (explicit word vs template word + route/vd) consumed
   by the out-of-tree differential validator that runs both paths
   through the pinned CRAQ simulator from identical architectural
   state (the descriptor-vs-decomposition discipline of craq-sim
   tests/diff-fuzz/SFPLOADMACRO).

   Build:
     g++ -std=c++17 -Wall -Wextra -Werror -I. \
	 rvtt-macro-vocab-enum.cc rvtt-macro-tables.cc -o <out> && <out>
*/

#include "rvtt-macro-tables.h"

#include <stdio.h>
#include <string.h>

using namespace rvtt_macro;

/* ------------------------------------------------------------------ */
/* Typed per-(opcode, mod1) semantic model (InstSema-style).	      */
/* ------------------------------------------------------------------ */

enum field_use : unsigned
{
  F_NONE = 0,
  F_READS_VD = 1 << 0,		/* reads LReg[VD-field] as data	       */
  F_READS_VC = 1 << 1,		/* reads LReg[VC-field]		       */
  F_READS_VB = 1 << 2,		/* reads LReg[VB-subfield] (bits 15:12)*/
  F_READS_VA = 1 << 3,		/* reads LReg[VA-subfield] (bits 19:16)*/
  F_READS_IMM = 1 << 4,		/* consumes immediate payload bits     */
  F_WRITES_VC = 1 << 5,		/* also writes LReg[VC] (SFPSWAP)      */
  F_CC_WRITE = 1 << 6,		/* writes lane flags		       */
  F_HIDDEN = 1 << 7,		/* PRNG / LUT regs / LReg[7] indirect /
				   lane-config-dependent semantics     */
  F_VC_PIN9 = 1 << 8,		/* VC field must encode L9	       */
};

struct mod_sema
{
  bool legal;			/* encodable + simulator-accepted      */
  unsigned use;			/* field_use mask		       */
  const char *provenance;
};

/* Op model rows.  Provenance keys:
   [doc:X] = tt-isa-documentation BlackholeA0/TensixTile/
	     TensixCoprocessor/X.md functional model (WH-shared docs
	     resolve to the WormholeB0 tree file of the same name);
   [sim:N] = craq-sim src/tensix.cpp line N at 9f324140 (BH build);
   [md:N] = rvtt.md audited effect attributes near line N (the
   in-tree per-mod audit the RTL patterns already carry).
   Every row is doc-grounded and sim-intersected: a mod the doc
   defines but the sim refuses is NOT legal here (fail-closed) and is
   listed in the divergence report.  */

static mod_sema
op_sema (uint8_t opcode, unsigned mod1)
{
  mod_sema r = { false, F_NONE, "no-model" };
  switch (opcode)
    {
    case 0x76:			/* SFPDIVP2 [sim:8819] [spec:SFPDIVP2] */
      if (mod1 <= 1)
	r = { true, F_READS_VC | F_READS_IMM,
	      "[doc:SFPDIVP2][sim:8819-8843] exp replace/add, VD unread" };
      break;
    case 0x77:			/* SFPEXEXP [sim:8845] */
      if (mod1 <= 2 || mod1 == 10)
	r = { true,
	      (unsigned) (F_READS_VC
			  | ((mod1 == 2 || mod1 == 10) ? (unsigned) F_CC_WRITE : 0u)),
	      "[doc:SFPEXEXP][sim:8845-8875][md:~2000] exp extract" };
      break;
    case 0x78:			/* SFPEXMAN [sim:8877] */
      if (mod1 <= 1)
	r = { true, F_READS_VC,
	      "[doc:SFPEXMAN][sim:8877-8892][md:~2000] man extract" };
      break;
    case 0x79:			/* SFPIADD [sim:8894] */
      if (mod1 <= 10 && (mod1 & 3) <= 2)
	{
	  bool imm = (mod1 & 1) != 0;
	  bool cc = !((mod1 & 12) == 4);	/* CC_NONE w/o GTE0    */
	  r = { true,
		(unsigned) ((imm ? F_READS_IMM : F_READS_VD) | F_READS_VC
			    | (cc ? (unsigned) F_CC_WRITE : 0u)),
		"[doc:SFPIADD][sim:8894-8929][md:1770] add/sub/imm" };
	}
      break;
    case 0x7a:			/* SFPSHFT [sim:8931] BH mods {0,1,2,3,5,7} */
      if (mod1 <= 3 || mod1 == 5 || mod1 == 7)
	{
	  bool imm = (mod1 & 1) != 0;
	  bool src_vc = (mod1 & 4) != 0;	/* BH bit2: source=VC  */
	  r = { true,
		(unsigned) ((imm ? F_READS_IMM : F_READS_VC)
			    | (src_vc ? F_READS_VC : F_READS_VD)),
		"[doc:SFPSHFT (BH)][sim:8931-8967][md:2483] shift; "
		"reg-amount form requires imm12 == 0 (sim TTSIM_VERIFY; "
		"the doc model simply never reads it)" };
	}
      break;
    case 0x7b:			/* SFPSETCC: predicate write, no lreg
				   write -- select-program territory    */
      r = { true, F_CC_WRITE, "[sim:8969-9000] predicate def" };
      break;
    case 0x7c:			/* SFPMOV [sim:9002] mods {0,1,2,8}    */
      if (mod1 <= 2)
	r = { true, F_READS_VC,
	      "[doc:SFPMOV][sim:9002-9028][md:~1900] copy/negate; mod 2 "
	      "all-lanes; imm12 must be 0" };
      else if (mod1 == 8)
	r = { true, F_READS_VC | F_HIDDEN,
	      "[sim:9013-9015] PRNG source: per-lane hidden state" };
      break;
    case 0x7d:			/* SFPABS [sim:9030] */
      if (mod1 <= 1)
	r = { true, F_READS_VC,
	      "[doc:SFPABS][sim:9030-9053][md:~1900] abs, VD unread" };
      break;
    case 0x7e: case 0x7f:	/* SFPAND / SFPOR [sim:9068/9085]      */
      if (mod1 == 0)
	r = { true, F_READS_VD | F_READS_VC,
	      "[doc:SFPAND/SFPOR (BH)][sim:9068-9100][md:2384] in-place "
	      "VD op= VC; imm12/VB must be 0" };
      else if (mod1 == 1)
	r = { true, F_READS_VB | F_READS_VC,
	      "[doc:SFPAND/SFPOR (BH) MOD1_USE_VB][sim:9074-9076]"
	      "[md:2384 _lv_bh] VD write-only, sources = VB-subfield "
	      "reg and VC" };
      break;
    case 0x80:			/* SFPNOT [sim:9102]: VD = ~VC; RTL
				   pattern carries NO effect audit
				   (rvtt.md rvtt_sfpnot_lv) -- hosting
				   needs the audit first.	       */
      if (mod1 == 0)
	r = { true, F_READS_VC,
	      "[sim:9102-9104] ~VC; PATTERN-EFFECTS-UNAUDITED" };
      break;
    case 0x81:			/* SFPLZ [sim:9106] sim mods {0,2,4}   */
      if (mod1 == 0 || mod1 == 2 || mod1 == 4)
	r = { true,
	      (unsigned) (F_READS_VC | (mod1 == 2 ? (unsigned) F_CC_WRITE : 0u)),
	      "[doc:SFPLZ][sim:9106-9131][md:~1960] clz, VD unread; "
	      "imm12 must be 0; DOC/SIM DIVERGENCE on mods 6+ (below)" };
      break;
    case 0x82:			/* SFPSETEXP [sim:9133] */
      if (mod1 <= 2)
	r = { true,
	      (unsigned) (F_READS_VC
			  | (mod1 == 1 ? F_READS_IMM
			     : F_READS_VD)),	/* mods 0/2 read VD    */
	      "[doc:SFPSETEXP][sim:9133-9159] set exponent" };
      break;
    case 0x83:			/* SFPSETMAN [sim:9161]: sim forces
				   imm12 == 0 (TTSIM_VERIFY :9165)     */
      if (mod1 == 0)
	r = { true, F_READS_VC | F_READS_VD,
	      "[doc:SFPSETMAN][sim:9161-9181] mantissa from VD, sign/exp "
	      "from VC; DOC/SIM DIVERGENCE on the imm mode (below)" };
      break;
    case 0x89:			/* SFPSETSGN [sim:9412] */
      if (mod1 <= 1)
	r = { true,
	      (unsigned) (F_READS_VC
			  | (mod1 == 1 ? F_READS_IMM : F_READS_VD)),
	      "[doc:SFPSETSGN][sim:9412-9435] set sign" };
      break;
    case 0x8d:			/* SFPXOR [sim:9504]: no mod1 field    */
      if (mod1 == 0)
	r = { true, F_READS_VD | F_READS_VC,
	      "[doc:SFPXOR][sim:9504-9506][md:2384] in-place VD ^= VC; "
	      "mod1/imm12 bits reserved-zero" };
      break;
    case 0x8e:			/* SFPSTOCHRND [sim:9508]: PRNG unless
				   rnd_mode 0; VB read only in modes
				   4/5 -- keep whole-word/proven-shape
				   territory		       */
      r = { true, F_READS_VC | F_HIDDEN,
	    "[sim:9508-9595] stoch-rnd: PRNG state, mode-dependent VB" };
      break;
    case 0x90:			/* SFPCAST [sim:9601] mods {0,3}       */
      if (mod1 == 0 || mod1 == 3)
	r = { true, F_READS_VC,
	      "[doc:SFPCAST (BH)][sim:9601-9641] int->fp32 RTNE / "
	      "sign-mag; VB subfield reserved-zero" };
      break;
    case 0x92:			/* SFPSWAP [sim:9753]: writes BOTH VD
				   and VC; lane_config bits 8/11 invert
				   the decision per lane; lane_config
				   bit 2 selects an aliased-bank variant
				   touching four lregs	       */
      if (mod1 <= 9)
	r = { true,
	      F_READS_VD | F_READS_VC | F_WRITES_VC | F_HIDDEN,
	      "[sim:9753-9806] swap/minmax: dual write + lane-config-"
	      "dependent semantics" };
      break;
    case 0x98:			/* SFPMUL24 (BH) [sim:10262] */
      if (mod1 <= 1)
	r = { true, F_READS_VA | F_READS_VB | F_VC_PIN9,
	      "[doc:SFPMUL24 (BH)][sim:10262-10280, 8372-8379] 24x24 mul, "
	      "VC pinned L9 (contents unused)" };
      break;
    case 0x84:			/* SFPMAD [sim:9183]: mods 4/8 indirect
				   through LReg[7] (hidden)	       */
      r = { true,
	    (unsigned) (F_READS_VA | F_READS_VB | F_READS_VC
			| ((mod1 & 12) ? (unsigned) F_HIDDEN : 0u)),
	    "[doc:SFPMAD][sim:9183-9233] fma; mods&12 indirect LReg[7]" };
      break;
    case 0x85:			/* SFPADD [sim:9235]: one of VA/VB must
				   be L10 (+1.0), then unread	       */
      if (mod1 <= 3)
	r = { true, F_READS_VB | F_READS_VC,
	      "[doc:SFPADD][sim:9235-9267] add via mul-by-1.0; VA/VB "
	      "one must encode L10" };
      break;
    case 0x86:			/* SFPMUL [sim:9269]: VC pinned L9     */
      r = { true,
	    (unsigned) (F_READS_VA | F_READS_VB | F_VC_PIN9
			| ((mod1 & 12) ? (unsigned) F_HIDDEN : 0u)),
	    "[doc:SFPMUL][sim:9269-9335] mul; VC must encode L9" };
      break;
    case 0x74: case 0x75:	/* SFPMULI / SFPADDI [sim:8785/8802]:
				   imm16 occupies bits 23:8 = the imm12
				   AND VC fields; strictly in-place    */
      if (mod1 == 0)
	r = { true, F_READS_VD | F_READS_IMM,
	      "[doc:SFPMULI/SFPADDI][sim:8785-8817] in-place fp "
	      "scale/bias; imm16 overlaps the VC field (bits 23:8)" };
      break;
    default:
      break;
    }
  return r;
}

/* ------------------------------------------------------------------ */
/* Operand-binding arms.					      */
/* ------------------------------------------------------------------ */

enum binding
{
  B_INPLACE,	/* the VD-read (or VC-read routed) operand is the
		   launch VD; other operands named/immediate	       */
  B_NAMED,	/* every source named in surviving fields; dest is the
		   launch VD (write-only ops)			       */
  B_VB_CARRIER,	/* VB+VC class: launch VD through the VB:=VD route
		   (established MUL24 order)			       */
  B_VA_CARRIER,	/* VB+VC class: launch VD on the VA-side factor -- the
		   commuted arm (lane CI)			       */
  B_COUNT
};

static const char *binding_name[] =
  { "inplace-vd", "named-src", "vb-carrier", "va-carrier" };

/* One enumerated class.  */
struct enum_row
{
  uint8_t opcode;
  unsigned mod1;
  binding arm;
  unsigned unit_mask;
  const char *verdict;		/* "ADMITTED", "CANDIDATE", refusal    */
  const char *detail;
  const char *provenance;
};

/* Realization check for one (opcode, mod, arm): true when the class is
   exact under the override model.  OUT_DETAIL names the refusal.  */

static bool
realizes_exactly (const caps *c, uint8_t opcode, unsigned, binding arm,
		  const mod_sema &s, const char **out_detail)
{
  route_class rc = opcode_route_class (c, opcode);

  if (s.use & F_HIDDEN)
    {
      *out_detail = "hidden-state-dependent-semantics";
      return false;
    }
  if (s.use & F_WRITES_VC)
    {
      *out_detail = "dual-output-event-unhosted";
      return false;
    }
  if ((s.use & F_CC_WRITE) && !(s.use & (F_READS_VD | F_READS_VC
					 | F_READS_VB | F_READS_VA)))
    {
      *out_detail = "predicate-definition-select-territory";
      return false;
    }

  switch (arm)
    {
    case B_INPLACE:
      /* The launch VD must reach the op's read-VD (via the VD-field
	 override) or its read-VC (via the route-0 VC:=VD override).
	 Ops reading NEITHER have no in-place form.  */
      if (s.use & F_READS_VD)
	return true;		/* VD-field override supplies it       */
      if ((s.use & F_READS_VC) && rc == RC_VC
	  && !(s.use & (F_READS_VB | F_READS_VA)))
	return true;		/* route-0 VC:=VD, imm/nothing else    */
      *out_detail = "no-carrier-reachable-operand";
      return false;

    case B_NAMED:
      /* Write-only destination; sources survive verbatim.  Requires
	 route 1 for RC_VC ops (no VC override) -- always available --
	 and that the op does NOT read VD (the launch value would be
	 an unintended input).  */
      if (s.use & F_READS_VD)
	{
	  *out_detail = "vd-read-requires-inplace";
	  return false;
	}
      if (s.use & (F_READS_VC | F_READS_VB | F_READS_VA))
	return true;
      *out_detail = "no-named-source";
      return false;

    case B_VB_CARRIER:
    case B_VA_CARRIER:
    default:
      if (rc != RC_VB_VC)
	{
	  *out_detail = "not-a-vb-vc-opcode";
	  return false;
	}
      if (s.use & F_HIDDEN)
	{
	  *out_detail = "hidden-state-dependent-semantics";
	  return false;
	}
      /* The route-1 VB:=VD override supplies one factor; the other
	 survives in the VA field.  The commuted arm additionally needs
	 VA/VB symmetry -- true for both audited SFPMUL24 mods
	 (sfpmul24_result) and for SFPMUL/SFPMAD's product operands.  */
      return true;
    }
  *out_detail = "unreachable";
  return false;
}

/* ------------------------------------------------------------------ */
/* The admitted set (mirror of rvtt-macro-desc.cc's derived classes;   */
/* update IN THE SAME COMMIT as any admission).			      */
/* ------------------------------------------------------------------ */

struct admitted_class
{
  uint8_t opcode;
  unsigned mod_mask;		/* bit m = mod1 m admitted	       */
  unsigned arm_mask;		/* bit b = binding b admitted	       */
  const char *name;
};

static const admitted_class admitted[] = {
  /* SFPSWAP constant-register class (swap_cst_template_fields):
     mods 1/9, constant VC.  Listed for completeness though the
     enumerator's generic model refuses live SFPSWAP (dual write).  */
  { 0x92, (1u << 1) | (1u << 9), 1u << B_INPLACE, "swap-cst (established)" },
  /* WP12 generic classes (derived_value_template_fields).  */
  { 0x90, (1u << 0) | (1u << 3), (1u << B_INPLACE) | (1u << B_NAMED),
    "cast" },
  { 0x79, (1u << 0) | (1u << 2) | (1u << 4) | (1u << 6) | (1u << 8)
	  | (1u << 10), 1u << B_INPLACE, "iadd-reg" },
  /* SFPSHFT immediate in-place -> SHFT2 Round template (the frozen
     signbit pair; BH source mod 5, WH 1).  Modeled under 0x94.  */
  { 0x94, 1u << 6, 1u << B_INPLACE, "shft-imm->shft2 (frozen 9(e))" },
  { 0x98, (1u << 0) | (1u << 1), 1u << B_VB_CARRIER, "mul24" },
  /* Lane CZ admissions land here commit by commit:
     iadd-imm {1,5,9} inplace+named; shft-reg {0,2} inplace;
     unary {lz 0/2/4, mov 0/1/2, abs 0/1, exman 0/1, exexp 0/1/2/10}
     named(+inplace where RC_VC); logic {and/or mod1 named-2src,
     and/or mod0 + xor inplace}.  */
};

static bool
is_admitted (uint8_t opcode, unsigned mod1, binding arm)
{
  for (const admitted_class &a : admitted)
    if (a.opcode == opcode && ((a.mod_mask >> mod1) & 1)
	&& ((a.arm_mask >> arm) & 1))
      return true;
  return false;
}

/* ------------------------------------------------------------------ */
/* Hosting-value model: corpus body shapes and the (opcode, mod-set,   */
/* arm) classes each needs.  Counts are per row iteration.	      */
/* ------------------------------------------------------------------ */

struct hosting_need
{
  const char *shape;		/* provenance-cited corpus shape       */
  uint8_t opcode;
  unsigned mod_mask;
  binding arm;
  unsigned count;
};

/* gcd-fresh (tt-metal agent/gcd-fresh-v2 fresh_cpp/gcd.h, laneCS): per
   17x round: strip = iadd(0-v, mod 6 reg: admitted) + and (v & iso)
   + lz + iadd-imm(-31) + shft-reg; sort = SFPSWAP dual (WALL);
   subtract = iadd reg mod 6 (admitted).  mulint32-fresh (laneCI V0):
   4 templates full at ii=12; every further hosted member needs a 5th
   distinct word.  */
static const hosting_need hosting[] = {
  { "gcd-round.iadd-imm(-31)", 0x79, 1u << 5, B_INPLACE, 2 },
  { "gcd-round.shft-reg",      0x7a, 1u << 0, B_INPLACE, 2 },
  { "gcd-round.lz",	       0x81, (1u << 0) | (1u << 2) | (1u << 4),
    B_NAMED, 2 },
  { "gcd-round.and",	       0x7e, (1u << 0) | (1u << 1), B_NAMED, 1 },
  { "int-bodies.iadd-imm",     0x79, (1u << 1) | (1u << 5) | (1u << 9),
    B_INPLACE, 1 },
  { "unaryshift.shft-reg",     0x7a, (1u << 0) | (1u << 2), B_INPLACE, 1 },
  { "float-bodies.muli/addi",  0x74, 1u << 0, B_INPLACE, 1 },
  { "float-bodies.muli/addi",  0x75, 1u << 0, B_INPLACE, 1 },
  { "float-bodies.mad",	       0x84, (1u << 0) | (1u << 1) | (1u << 2)
	  | (1u << 3), B_VB_CARRIER, 1 },
};

static unsigned
hosting_value (uint8_t opcode, unsigned mod1, binding arm)
{
  unsigned v = 0;
  for (const hosting_need &h : hosting)
    if (h.opcode == opcode && ((h.mod_mask >> mod1) & 1) && h.arm == arm)
      v += h.count;
  return v;
}

/* ------------------------------------------------------------------ */
/* Driver.							      */
/* ------------------------------------------------------------------ */

int
main (int argc, char **argv)
{
  bool diff_vectors = false;
  for (int i = 1; i < argc; ++i)
    if (!strcmp (argv[i], "--diff-vectors"))
      diff_vectors = true;

  const caps *c = rvtt_macro_caps_for_cpu (CPU_BH);
  if (!c)
    {
      fprintf (stderr, "no BH capability table\n");
      return 1;
    }

  unsigned n_scanned = 0, n_admitted = 0, n_candidates = 0, n_refused = 0;
  unsigned n_words_encodable = 0;

  printf ("template-word vocabulary enumeration (BH)\n");
  printf ("=========================================\n\n");

  /* Word-space size note: opcode x imm12 x src_c x dest_sel x mod1 is
     2^32; the CLASS space below (opcode x mod1 x binding) is the
     quotient the derive-core actually admits from, with imm12/named
     regs as free parameters proven field-exact per class.  */

  for (unsigned op = 0; op < 256; ++op)
    {
      unsigned mask = subunit_legal_mask (c, (uint8_t) op);
      if (!mask || (mask & (1u << SEQ_UNIT_STORE)))
	continue;		/* store/case-3 territory; no template */
      for (unsigned mod1 = 0; mod1 < 16; ++mod1)
	{
	  mod_sema s = op_sema ((uint8_t) op, mod1);
	  if (!s.legal)
	    continue;
	  /* Every legal (op, mod) has 2^k field instances; count the
	     encodable template words this class covers (imm bits free,
	     named regs 0..15, dest selector fixed by the launch).  */
	  ++n_words_encodable;
	  for (unsigned a = 0; a < B_COUNT; ++a)
	    {
	      ++n_scanned;
	      const char *detail = nullptr;
	      bool exact = realizes_exactly (c, (uint8_t) op, (unsigned) mod1,
					     (binding) a, s, &detail);
	      if (!exact)
		{
		  ++n_refused;
		  continue;
		}
	      if (is_admitted ((uint8_t) op, mod1, (binding) a))
		{
		  ++n_admitted;
		  continue;
		}
	      ++n_candidates;
	      unsigned hv = hosting_value ((uint8_t) op, mod1,
					   (binding) a);
	      printf ("CANDIDATE op=0x%02x mod1=%-2u arm=%-10s "
		      "hosting=%u\n  %s\n",
		      op, mod1, binding_name[a], hv, s.provenance);
	      if (diff_vectors)
		{
		  /* One representative vector per class; the validator
		     sweeps operand values and imm strata itself.  */
		  printf ("DIFFVEC op=0x%02x mod1=%u arm=%s\n",
			  op, mod1, binding_name[a]);
		}
	    }
	}
    }

  printf ("\nstats: class-instances scanned=%u encodable-(op,mod) "
	  "classes=%u admitted=%u candidates=%u refused=%u\n",
	  n_scanned, n_words_encodable, n_admitted, n_candidates,
	  n_refused);

  /* Named walls (the enumeration-complete refusals).  */
  printf ("\nnamed walls (not admissible by ANY vocabulary row):\n"
	  " - SFPSWAP live-operand / dual-result: writes BOTH VD and VC "
	  "[sim:9793-9798]\n   and its decision inverts under lane_config "
	  "bits 8/11 [sim:8364-8366]:\n   dual-output-event-unhosted + "
	  "hidden-state-dependent-semantics.\n"
	  " - SFPLUT/SFPLUTFP32: operands are hidden LUT registers "
	  "L0-L2/L4-L6 and x=L3\n   [sim:8754-8782, 10114-10208]: "
	  "hidden-state-dependent-semantics.\n"
	  " - SFPMOV mod 8 / SFPSTOCHRND rnd!=0: per-lane PRNG "
	  "[sim:9013, 9518].\n"
	  " - SFPMAD/SFPMUL mods with bit2/bit3: indirect operands "
	  "through LReg[7]\n   [sim:9214, 9225]: "
	  "hidden-state-dependent-semantics.\n"
	  " - SFPNOT: exact in the model [sim:9102-9104] but the RTL "
	  "pattern carries no\n   effect audit (rvtt.md rvtt_sfpnot_lv): "
	  "pattern-effects-unaudited.\n");

  /* Doc/sim divergences (owner functional models vs the pinned
     differential oracle).  The tool admits only the INTERSECTION;
     each divergence is a standing finding.  */
  printf ("\ndoc/sim divergences (intersection admitted; each is a "
	  "reportable finding):\n"
	  " - SFPLZ: the functional model defines mods 6/8/10/12/14 "
	  "(CC_COMP combos,\n   doc SFPLZ.md) but the sim accepts only "
	  "{0,2,4} [sim:9108-9109] -- the sim\n   under-implements the "
	  "documented CC_COMP behavior; admitted here: {0,2,4}.\n"
	  " - SFPSETMAN: the functional model defines the immediate-"
	  "mantissa mode\n   (doc SFPSETMAN.md, Mod1==1) but the sim "
	  "forces imm12 == 0 [sim:9165] --\n   admitted here: mod 0 "
	  "only.\n"
	  " - SFPSHFT register-amount form: the doc model never reads "
	  "imm12; the sim\n   decoder additionally REQUIRES it to be 0 "
	  "[sim:8950] -- no semantic gap for\n   templates (imm12 packs "
	  "0), recorded for completeness.\n");

  /* mulint32 ceiling (enumeration-complete upgrade of lane CI's
     refusal): the V0 row's formed calendar uses ALL FOUR
     InstructionTemplate destinations (laneCI-evidence worktree-v0
     dump: 0x900000c3, 0x94fe90d6, 0x980009e0, 0x900003f3).  Template
     sharing requires BIT-IDENTICAL words (derive_row's sharing gate);
     every explicit member of the row is a different opcode byte or
     names a different factor register than all four resident words,
     so ANY additional hosted event -- under any admission this
     enumeration can ever produce, because word identity is a function
     of (opcode, mod1, src_c, imm12) only -- demands a fifth distinct
     word: template-capacity-exceeded.  The ii=11 restructure floor is
     likewise vocabulary-independent: the WP12 visibility deadline
     (derive-core issue_consumer_slot_p1) prices slots and subunit
     latencies, never words.  */
  printf ("\nmulint32 ceiling: 4 resident words are pairwise distinct "
	  "from every explicit\nrow member's class (different opcode "
	  "byte or named factor); sharing needs\nbit-identity; the "
	  "deadline floor consumes slots, not words -> the lane CI\n"
	  "refusals are enumeration-complete over this vocabulary "
	  "space.\n");

  return 0;
}
