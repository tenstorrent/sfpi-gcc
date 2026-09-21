# Tensix flag defaults — what actually ships

Generated from `gcc/config/riscv/riscv.opt` and the sweep harness's reviewed
ON set. Regenerate with `scripts/gen-flag-defaults.py`.

## The short version

| set | n | meaning |
|---|---|---|
| `Init(1)` | 6 | ON in every compile, including production |
| reviewed ON | 39 | what the campaign board is measured with |
| reviewed ON but `Init(0)` | 36 | **measured but NOT shipped** — the harness passes these explicitly |
| `Init(0)`, not reviewed ON | 59 | opt-in knobs |

tt-metal's `jit_build/build.cpp` passes **no** `-mtt-tensix-*` flag, so a
production kernel gets exactly the `Init(1)` column. Anything measured with
the reviewed ON set is not what users compile with until it is promoted.

## ON in production (`Init(1)`)

- `mtt-tensix-optimize-cc` — line 574
- `mtt-tensix-optimize-dce` — line 582
- `mtt-tensix-optimize-replay` — line 754
- `mtt-tensix-optimize-dst-ownership` — line 862
- `mtt-tensix-optimize-lut-select` — line 866
- `mtt-tensix-optimize-setexp-fold` — line 964

## Reviewed ON but default-off — the promotion backlog

These are measured by the board and absent from production.

- `mtt-tensix-macro-planner`
- `mtt-tensix-macro-planner-replay`
- `mtt-tensix-macro-planner-residency`
- `mtt-tensix-optimize-capture-rotation`
- `mtt-tensix-optimize-ccmask`
- `mtt-tensix-optimize-const-remat`
- `mtt-tensix-optimize-const-residency`
- `mtt-tensix-optimize-counted-row-formation`
- `mtt-tensix-optimize-crosscall-hoist`
- `mtt-tensix-optimize-crossloop-hoist`
- `mtt-tensix-optimize-crossrow-pairing`
- `mtt-tensix-optimize-drain-schedule`
- `mtt-tensix-optimize-dst-autoincr`
- `mtt-tensix-optimize-dst-iteration-fusion`
- `mtt-tensix-optimize-ims`
- `mtt-tensix-optimize-init-hoist`
- `mtt-tensix-optimize-interlock-schedule`
- `mtt-tensix-optimize-invariant-loadi`
- `mtt-tensix-optimize-latency-schedule`
- `mtt-tensix-optimize-lreg-alloc`
- `mtt-tensix-optimize-lut-select-fp16`
- `mtt-tensix-optimize-mop-form`
- `mtt-tensix-optimize-mve-expand`
- `mtt-tensix-optimize-native-compare`
- `mtt-tensix-optimize-park-ordering`
- `mtt-tensix-optimize-pressure-park`
- `mtt-tensix-optimize-prgm-const`
- `mtt-tensix-optimize-priced-placement`
- `mtt-tensix-optimize-record-hoist-peel`
- `mtt-tensix-optimize-replay-exec-record`
- `mtt-tensix-optimize-replay-hoist`
- `mtt-tensix-optimize-replay-record-hoist`
- `mtt-tensix-optimize-store-source-tier`
- `mtt-tensix-optimize-transp-involution`
- `mtt-tensix-optimize-window-pairing`
- `mtt-tensix-optimize-window-pairing-stride`

## Numerics-licensed knobs that can never be a production default

tt-metal compiles with `-fno-associative-math` and `-fno-finite-math-only`.
Knobs requiring the opposite are opt-in by construction, whatever `Init()`
says: `reassoc-mad-restructure` (`-fassociative-math`), `lut-select-leaf-ext`
(`-ffinite-math-only`). Wins booked on them are unavailable to users.
