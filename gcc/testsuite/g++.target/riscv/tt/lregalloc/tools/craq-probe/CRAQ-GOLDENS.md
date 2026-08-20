# Lane DS — CRAQ golden verification (2026-08-20)

## Verdict
The hand-spilled integer pressure-ladder twins (N = 9, 10, 12, 16;
arsenal ladder-spilled-body.h) and the 8-live control rung
(ladder-body.h, LADDER_N=8) all produce, ON THE PINNED BLACKHOLE SIM,
output BIT-IDENTICAL to the host-computed goldens
(tools/ladder_golden.py == goldens/ladder-goldens.txt, cross-checked
value-for-value):

    LADDER_GOLDEN mode=3 N=9 : 288 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=4 N=10: 320 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=5 N=12: 384 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=6 N=16: 512 lanes bit-exact vs host golden
    LADDER_GOLDEN mode=7 N=8 : 256 lanes bit-exact vs host golden

(full log: probe-run2.log; one pytest node, PASS, 4.89s)

## Pins
- sim: craq-sim clone ~/sfpi-uplift/craq-sim-laneDS @ 9f324140;
  bh libttsim.so sha256 32489dda4fd6... (== pinned oracle; see
  toolchain-cc1plus.sha)
- toolchain: the installed pin (tests/sfpi -> ~/sfpi-uplift/sfpi/build/sfpi),
  cc1plus sha 01aed0d8d58d... (toolchain-cc1plus.sha; the currently installed reviewed pin -- matches sweep-2x2 REVIEW_RECORD-01aed0d8d58d.md, i.e. NEWER than pin-13 8e87fba0)
- harness: tt-metal-pin14 via shim ~/sfpi-uplift/shim-laneDS
  (read-only farm untouched); private RUNNER_TEMP.

## Method
Probe source sfpu_lreg_ladder_probe.cpp (copy here) + test
test_lreg_ladder_probe.py (copy here): UInt32 end-to-end
(torch.int64 carrier; full 32-bit patterns round-trip bit-exactly),
DestAccumulation.Yes, unpack_to_dest, single input tile, 4 result
tiles.  The tensor<->dst-row mapping was derived EMPIRICALLY by three
calibration kernels (rowtag / lanetag=vConstTileId / identity), not
assumed from tile geometry (mapping-calibration.txt).  Stimulus per
the arsenal contract: dst row i lane l = splitmix32(i*37+l).

## Facts learned (for the arsenal record / future probes)
1. TestConfig template parameters materialize as `constexpr` variables
   in build.h, NOT preprocessor macros: `#if PROBE_MODE` chains
   silently select branch 0 for every variant.  Dispatch must be
   if-constexpr.  (Cost: every early "inert stores" symptom was one
   identity kernel running under all mode labels.)
2. In fp32-dest-acc mode a PACKED RESULT TILE spans 64 sixteen-bit dst
   rows: pack arg t returns SFPU byte addresses [64t .. 64t+62].  The
   arsenal ladder layout (inputs 0..30, scratch 160..178, outputs
   192..222) therefore comes back in result tiles 0 / 2 / 3.
3. A raw-builtin SFPU body run outside the eltwise wrappers needs
   (a) math::reset_counters(p_setrwc::SET_ABD_F) after datacopy and
   (b) an explicit __builtin_rvtt_sfpencc_all_lanes() (no predication
   => the compiler emits no SFPENCC; ambient CC is not guaranteed).
4. The sfpi headers macro-wrap __builtin_rvtt_sfpload/sfpstore/
   sfpxloadi with arity-reducing wrappers; #undef them to use the raw
   dg-test arity in harness kernels (the iptr is a compile-time token;
   codegen emits direct Tensix mnemonics).
5. TTSIM_TRACE_NG_TYPECAST=1 makes the pinned sim print per-SFPSTORE
   dst_row/cc/values lines (TRACE_SFPSTORE[..]) — the decisive
   debugging tool here.

## What this proves for the allocator gate
The rungs' goldens are now sim-validated, not just host-asserted: an
allocator-compiled ladder rung (same DAG, exact Dst spills) must
reproduce these exact bits on this exact sim.  The 8-live control also
executes bit-exactly, closing the no-op rung's semantic leg (its
byte-identity leg is in the gate script).
