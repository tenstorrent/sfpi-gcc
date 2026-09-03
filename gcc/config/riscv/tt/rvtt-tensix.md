;; Machine description for Tenstorrent SFPU Intrinsics -- Tensix scalar-side words.
;; Copyright (C) 2022-2026 Tenstorrent Inc.
;; Originated by Paul Keller (pkeller@tenstorrent.com)
;; Rewritten by Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

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

;; Tensix scalar-side words: RWC setup and increment, Dst face
;; selection, SETC16, the D/A/B register moves, source-B
;; transpose, stall-wait, the read-modify-write CIB word, the MOP
;; and MOP-config launches, and TTREPLAY.
(define_expand "rvtt_ttsetrwc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     (match_operand:SI 5 "const_int_operand")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      /* QSR has one RWC value field rather than separate D/B/A fields, and it
         does not implement WH/BH's C-to-CR mode (CR bit 3).  D can therefore
         map to QSR's unified value only when the mask selects D and/or
         fidelity clear, CR is either disabled or D-only, and CR_D is paired
         with a D update.  Conservatively refuse every other form.  */
      HOST_WIDE_INT cr = INTVAL (operands[1]);
      HOST_WIDE_INT mask = INTVAL (operands[5]);
      if ((mask & ~HOST_WIDE_INT (0xc)) != 0
          || (cr != 0 && cr != 4)
          || ((cr & 4) != 0 && (mask & 4) == 0))
        {
          error ("QSR TTSETRWC cannot represent this CR/mask combination");
          DONE;
        }
      emit_insn (gen_rvtt_ttsetrwc_qsr (operands[0], operands[1],
                                        operands[2], operands[5]));
    }
  else
    emit_insn (gen_rvtt_ttsetrwc_wh_bh (operands[0], operands[1],
                                        operands[2], operands[3],
                                        operands[4], operands[5]));
  DONE;
})

/* These volatile patterns are the compiler-visible architectural RWC/Dst
   state boundary.  They must remain replay barriers and may not be treated as
   ordinary arithmetic or decoded later from opaque instruction words.  */
(define_insn "rvtt_ttsetrwc_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     (match_operand:SI 2 "const_int_operand" "n")
     (match_operand:SI 3 "const_int_operand" "n")
     (match_operand:SI 4 "const_int_operand" "n")
     (match_operand:SI 5 "const_int_operand" "n")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETRWC\t%0, %1, %2, %3, %4, %5"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "set")])

(define_insn "rvtt_ttsetrwc_qsr"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n")
     (match_operand:SI 1 "const_int_operand" "n")
     (match_operand:SI 2 "const_int_operand" "n")
     (match_operand:SI 3 "const_int_operand" "n")
     ] UNSPECV_TTSETRWC)]
  "TARGET_XTT_TENSIX_QSR"
  "TTSETRWC\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "set")])

(define_insn "rvtt_ttincrwc"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     (match_operand:SI    2 "const_int_operand" "n")
     (match_operand:SI    3 "const_int_operand" "n")
     ] UNSPECV_TTINCRWC)]
  "TARGET_XTT_TENSIX"
  "TTINCRWC\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "inc")
   ;; Latency audit: INCRWC updates the RWC counters and
   ;; nothing else -- no LREG result exists.  WH INCRWC.md's functional
   ;; model is the pure counter update with no next-cycle rule; the BH
   ;; tree carries no INCRWC page (a documentation gap); the reference
   ;; simulator's TENSIX_EXECUTE_INCRWC applies
   ;; the counter deltas at issue (sim proof archived); every
   ;; hardware-proven counted production row issues TTINCRWC ->
   ;; SFPLOAD back-to-back at the row boundary, consuming the stepped
   ;; counter in the next slot.  Result latency 0, BH/WH only.
   (set (attr "xtt_result_latency")
	(if_then_else (match_test "TARGET_XTT_TENSIX_BH
				   || TARGET_XTT_TENSIX_WH")
		      (const_int 1) (const_int 0)))])

;; Typed architectural Dst/RWC face advance: one face is two architectural
;; Dst += 8 counter steps with no LREG, CC, or configuration effect.  Late
;; analyses recognize the run-separator effect by this typed identity.
;; Raw `.ttinsn' constant words of the same architectural class (a
;; SETRWC-class word writing only the Dst counter pair) are field-decoded
;; against the capability tables and carry the identical effect set
;; (rvtt-raw-boundary.cc); every other raw word remains opaque and
;; refuses.
(define_expand "rvtt_ttdstface"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTDSTFACE)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      /* QSR's unified-RWC model has no defined Dst face-step form; refuse
         rather than approximate (mirror of the TTSETRWC QSR refusal).  */
      error ("QSR cannot represent the Dst face advance");
      DONE;
    }
  emit_insn (gen_rvtt_ttdstface_wh_bh ());
  DONE;
})

;; Like the typed TTSETRWC above, this volatile pattern is the
;; compiler-visible architectural RWC/Dst state boundary; it must remain a
;; replay barrier.  The architectural mnemonic and its field values are
;; emission data owned by this pattern and the assembler, never decision
;; logic: the CR-mode Dst += 8 step (SETRWC CR=4, D=8, mask=4) is issued
;; twice to advance exactly one face.
(define_insn "rvtt_ttdstface_wh_bh"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTDSTFACE)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETRWC\t0, 4, 8, 0, 0, 4\;TTSETRWC\t0, 4, 8, 0, 0, 4"
  [(set_attr "type" "tensix")
   (set_attr "length" "8")
   (set_attr "xtt_issue" "sync")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_subunit" "sync")
   (set_attr "xtt_lreg_read_ops" "1")
   (set_attr "xtt_lreg_write_ops" "1")
   (set_attr "xtt_cc_effect" "none")
   (set_attr "xtt_config_effect" "none")
   (set_attr "xtt_rwc_effect" "face")])

;; Compiler-owned address-modifier programming.  Operand 0 is a SETC16
;; configuration register index taken from a per-target capability table,
;; operand 1 the 16-bit value.  Only compiler passes that have proven
;; ownership of the addressed slot emit this (see rtl-rvtt-dst-autoincr.cc);
;; the assembler owns the encoding.
(define_insn "rvtt_ttsetc16_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n")
     (match_operand:SI    1 "const_int_operand" "n")
     ] UNSPECV_TTSETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSETC16\t%0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; Canonical proven all-lanes enable (init hoist): expands to the
;; architectural all-lanes SFPENCC operands -- the exact word the
;; formation's cc_enable_all_lanes_proved_p proof compares against
;; (capability tables, rvtt_macro::sfpencc_all_lanes_word).  Zero
;; arguments by design: the word is an architectural constant, never
;; caller data.
(define_expand "rvtt_sfpencc_all_lanes"
  [(const_int 0)]
  "TARGET_XTT_TENSIX"
{
  uint32_t word;
  gcc_assert (rvtt_macro::sfpencc_encode (SFPENCC_IMM12_BOTH,
					  SFPENCC_MOD1_EI_RI, &word)
	      && word == rvtt_macro::sfpencc_all_lanes_word ());
  emit_insn (gen_rvtt_sfpencc (GEN_INT (SFPENCC_MOD1_EI_RI),
			       GEN_INT (SFPENCC_IMM12_BOTH)));
  DONE;
})

;; Gimple-spellable owned SETC16 (init hoist): the caller-side
;; materialization of a callee's owned address-modifier program, emitted
;; only by the init-hoist commit under its proven contract.  Forwards to
;; the compiler-owned pattern above; the assembler owns the encoding.
(define_expand "rvtt_ttsetc16"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand")
     (match_operand:SI    1 "const_int_operand")
     ] UNSPECV_TTSETC16)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
{
  emit_insn (gen_rvtt_ttsetc16_int (operands[0], operands[1]));
  DONE;
})

;; ---------------------------------------------------------------------
;; FPU face-transpose family.  Matrix-Unit (FPU) Dst <->
;; SrcA/SrcB row moves, the SrcB[16:32) 16x16 transpose, the wait-gate
;; stall, and the backend-config byte RMW -- the typed spellings of the
;; hand face-transpose choreography (tt_llk_blackhole
;; llk_math_transpose_dest.h / the vendored ckernel_sfpu_topk_xl.h
;; transpose_dest_face_32b).  Semantics: tt-isa-documentation WormholeB0
;; MOVD2B/MOVB2A/MOVB2D/MOVA2D/TRNSPSRCB/RMWCIB .md functional models
;; (which carry the Blackhole arms; the BlackholeA0 tree is a doc gap and
;; the pinned sim is the BH oracle).
;;
;; Deliberately effect-UNAUDITED: no xtt_* effect attributes, so
;; rvtt_insn_effects () resolves them opaque and every optimization layer
;; (scheduler, dst-autoincr, macro planner, replay formation, ...) refuses
;; around them byte-identically -- these instructions read and write Dst
;; rows and Src banks through state (ALU formats, RWC counters, bank
;; validity) no compiler layer models today.  xtt_replay barrier for the
;; same reason.  QSR encodes this family differently (gas rejects the
;; WH/BH operand ranges) and has no audited choreography: expand-time
;; refusal.
(define_expand "rvtt_ttmovd2b"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVD2B)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVD2B is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovd2b_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmovd2b_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcBRow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Move4Rows
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVD2B)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVD2B\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmovb2a"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     ] UNSPECV_TTMOVB2A)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVB2A is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovb2a_wh_bh (operands[0], operands[1], operands[2],
				      operands[3]));
  DONE;
})

(define_insn "rvtt_ttmovb2a_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; SrcARow
     (match_operand:SI 1 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 2 "const_int_operand" "n") ;; Move4Rows
     (match_operand:SI 3 "const_int_operand" "n") ;; SrcBRow
     ] UNSPECV_TTMOVB2A)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVB2A\t%0, %1, %2, %3"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmovb2d"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVB2D)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVB2D is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmovb2d_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmovb2d_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcBRow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Mode
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVB2D)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVB2D\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttmova2d"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     (match_operand:SI 4 "const_int_operand")
     ] UNSPECV_TTMOVA2D)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTMOVA2D is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttmova2d_wh_bh (operands[0], operands[1], operands[2],
				      operands[3], operands[4]));
  DONE;
})

(define_insn "rvtt_ttmova2d_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; UseDst32bLo
     (match_operand:SI 1 "const_int_operand" "n") ;; SrcARow
     (match_operand:SI 2 "const_int_operand" "n") ;; AddrMod
     (match_operand:SI 3 "const_int_operand" "n") ;; Move8Rows
     (match_operand:SI 4 "const_int_operand" "n") ;; DstRow
     ] UNSPECV_TTMOVA2D)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOVA2D\t%0, %1, %2, %3, %4"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_tttrnspsrcb"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTTRNSPSRCB)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTTRNSPSRCB is unaudited; refuse");
      DONE;
    }
  emit_insn (gen_rvtt_tttrnspsrcb_wh_bh ());
  DONE;
})

(define_insn "rvtt_tttrnspsrcb_wh_bh"
  [(unspec_volatile:XTT32SI [(const_int 0)] UNSPECV_TTTRNSPSRCB)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTTRNSPSRCB"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

(define_expand "rvtt_ttstallwait"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     ] UNSPECV_TTSTALLWAIT)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTSTALLWAIT is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttstallwait_wh_bh (operands[0], operands[1]));
  DONE;
})

(define_insn "rvtt_ttstallwait_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; StallRes
     (match_operand:SI 1 "const_int_operand" "n") ;; WaitRes
     ] UNSPECV_TTSTALLWAIT)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTSTALLWAIT\t%0, %1"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; Operand 0 selects the byte lane (RMWCIB0..3); the mnemonic choice is
;; emission data owned by this pattern and the assembler.
(define_expand "rvtt_ttrmwcib"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand")
     (match_operand:SI 1 "const_int_operand")
     (match_operand:SI 2 "const_int_operand")
     (match_operand:SI 3 "const_int_operand")
     ] UNSPECV_TTRMWCIB)]
  "TARGET_XTT_TENSIX"
{
  if (TARGET_XTT_TENSIX_QSR)
    {
      error ("QSR TTRMWCIB is unaudited (encoding differs); refuse");
      DONE;
    }
  emit_insn (gen_rvtt_ttrmwcib_wh_bh (operands[0], operands[1], operands[2],
				      operands[3]));
  DONE;
})

(define_insn "rvtt_ttrmwcib_wh_bh"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI 0 "const_int_operand" "n") ;; ByteIndex
     (match_operand:SI 1 "const_int_operand" "n") ;; Mask8
     (match_operand:SI 2 "const_int_operand" "n") ;; Data8
     (match_operand:SI 3 "const_int_operand" "n") ;; CfgAddr8
     ] UNSPECV_TTRMWCIB)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
{
  switch (INTVAL (operands[0]))
    {
    case 0: return "TTRMWCIB0\t%1, %2, %3";
    case 1: return "TTRMWCIB1\t%1, %2, %3";
    case 2: return "TTRMWCIB2\t%1, %2, %3";
    case 3: return "TTRMWCIB3\t%1, %2, %3";
    default: gcc_unreachable ();
    }
}
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")])

;; MOP loop delivery (formed only by the rvtt_mop_form pass; capability
;; facts and provenance in rvtt-mop-tables.h).  MOP (opcode 0x01) fires
;; the programmed template; the operands are the raw encoding fields
;; (mop_type, loop_count, zmask low half).  MOP_CFG (opcode 0x03) sets
;; the persistent zmask high half.  Both are frontend work like REPLAY,
;; which the reference simulator classifies as Tdma.  QSR's MOP encoding differs and is
;; not provided.
(define_insn "rvtt_ttmop_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n") ;; mop_type
     (match_operand:SI    1 "const_int_operand" "n") ;; loop_count
     (match_operand:SI    2 "const_int_operand" "n") ;; zmask low half
     ] UNSPECV_TTMOP)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOP\t%0, %1, %2"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_issue" "tdma")])

(define_insn "rvtt_ttmopcfg_int"
  [(unspec_volatile:SI [
     (match_operand:SI    0 "const_int_operand" "n") ;; zmask high half
     ] UNSPECV_TTMOPCFG)]
  "TARGET_XTT_TENSIX_WH || TARGET_XTT_TENSIX_BH"
  "TTMOPCFG\t%0"
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "barrier")
   (set_attr "xtt_issue" "tdma")])

(define_expand "rvtt_ttreplay"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "reg_or_0_operand")
     (match_operand:SI    1 "reg_or_const_int_operand")
     (match_operand:SI    2 "reg_or_0_operand")
     (match_operand:SI    3 "const_int_operand")
     (match_operand:SI    4 "const_int_operand")
     (match_operand:SI    5 "const_int_operand")
     (match_operand:SI    6 "const_int_operand")
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
{
  auto mem = const0_rtx;
  auto opc = const0_rtx;
  auto enc = const0_rtx;
  auto imm = operands[1];
  if (!CONST_INT_P (imm))
    {
      mem = gen_rtx_MEM (SImode, operands[0]);
      int op
        = TARGET_XTT_TENSIX_WH  ? TT_OP_WH_REPLAY (INTVAL (operands[4]),
	                 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_BH  ? TT_OP_BH_REPLAY (INTVAL (operands[4]),
	                 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : TARGET_XTT_TENSIX_QSR ? TT_OP_QSR_REPLAY (INTVAL (operands[4]),
	                 0, 0, 0, INTVAL (operands[5]), INTVAL (operands[6]))
        : (gcc_unreachable (), 0);
      opc = GEN_INT (op);
      enc = GEN_INT (rvtt_synth (UINTVAL (operands[3])));
      imm = operands[2];
    }

  emit_insn (gen_rvtt_ttreplay_int
    (mem, opc, enc, imm,
     rvtt_gen_rtx_noval (XTT32SImode),
     operands[4], operands[5], operands[6]));
  DONE;
})

(define_insn "rvtt_ttreplay_int"
  [(unspec_volatile:XTT32SI [
     (match_operand:SI    0 "mem_or_0_operand" "J,m")
     (match_operand:SI    1 "const_int_operand" "J,n") ;; opcode
     (match_operand:SI    2 "const_int_operand" "J,n") ;; id, src & dst shifts
     (match_operand:SI    3 "reg_or_const_int_operand" "n,r") ;; imm or insn
     (match_operand:XTT32SI 4 "noval_operand" "xn,xn") ;; src (none)
     (match_operand:SI    5 "const_int_operand"  "n,n")
     (match_operand:SI    6 "const_int_operand"  "n,n") ;; exec-while-load
     (match_operand:SI    7 "const_int_operand"  "n,n") ;; load
     ] UNSPECV_TTREPLAY)]
  "TARGET_XTT_TENSIX"
  {
    return rvtt_synth::pattern (which_alternative,
      TARGET_XTT_TENSIX_QSR ? "TTREPLAY\t%5, %3, 0, 0, %6, %7"
      : "TTREPLAY\t%5, %3, %6, %7",
      operands, false, -1);
  }
  [(set_attr "type" "tensix")
   (set_attr "xtt_replay" "owner")
   ;; REPLAY is frontend work (opcode 0x04), which the reference simulator classifies as
   ;; Tdma rather than as the SFPU work it may later expand into.
   (set_attr "xtt_issue" "tdma")])
