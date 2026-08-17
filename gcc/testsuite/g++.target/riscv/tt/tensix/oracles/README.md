# WP8 byte-parity oracle store (HANDOFF section 6b, steps 2/3)

`wp8-oracle-manifest.txt` freezes, per shape and CPU, the SHA-256 over
the disassembled instruction words of:

1. `off` -- the explicit bytes with every macro flag off (corpus
   byte-identity reference);
2. `emit-loadmacro` -- the QUARANTINED exact-calendar pass's emission
   (`-mtt-tensix-emit-loadmacro`).  These are the frozen parity oracles
   minted BEFORE the pass's WP8 deletion; the pass source is unchanged
   since branch tip 4d0a22bb97ee, where minting was performed.
3. `planner` -- the generic macro planner (`-mtt-tensix-macro-planner`)
   at the WP8 series state.

Re-mint with `mint-wp8-oracles.sh <xg++> <gcc-build-dir>
<binutils-bindir> <outdir>`.  After the quarantined pass is deleted the
`emit-loadmacro` column reports `quarantined-pass-deleted`; the
committed manifest is the permanent record.

## Parity verdicts (WP8)

| shape | verdict |
|---|---|
| staged-loop (BH) | PARITY: planner == quarantined oracle byte for byte (preheader SFPENCC + owned SETC16 + 4 config words; 1 launch/row; drain 3) |
| cast-round (BH) | PARITY: planner == quarantined oracle byte for byte (single config; 8 alternating-VD launches; drain 3) |
| staged-loop, cast-round (WH) | CORRECTED DIVERGENCE (2026-08-17): the quarantined oracle's WH prefix carried the DUAL-SLOT SETC16 program, whose base-0 bank words clobber LLK's live ADDR_MOD_2 and corrupt the next tile's datacopy (adjudicated compiler bug, sfpi-gcc 2a0ba1e6602; laneAJ-evidence-20260817).  The planner emits the corrected single-slot Base=1 program ({19,29,54}); the WH planner hashes were re-minted with the correction and now differ from the frozen emit-loadmacro column by exactly the three dropped base-0 words. |
| staged, staged-successor, staged-boundary | DIVERGENCE BY DESIGN: the quarantined pass formed these single straight-line rows unconditionally; the planner's derived Layer-6 profitability refuses them (config prefix + drain can never amortize against one explicit row) and keeps the bytes identical to `off`.  The archived signbit silicon win (-7.48%) is the LOOP shape, which the planner reproduces exactly. |
| staged-fixed-asm, staged-refuse, staged-loop-refuse | REFUSAL IDENTITY: all three columns byte-identical (refusals never mutate). |

The cast-round oracle body `cast-round-rows.C` reproduces the
quarantined matcher's proven envelope exactly (load mode 6, round
instr_mod1 1 with zero imm8, store mode 2, Dst += 2 rows); the
quarantined pass had no in-tree cast-round test of its own.
