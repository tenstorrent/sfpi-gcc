# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Lane DS LREG-allocator arsenal: CRAQ golden probe.

Verifies bit-exactly, on the pinned blackhole sim, that the hand-spilled
integer pressure-ladder twins (N = 9/10/12/16) and the 8-live control rung
produce the host-computed goldens (splitmix32 stimulus, XOR-ring recurrence).
The tensor<->dst-row mapping is derived EMPIRICALLY from three calibration
kernels (rowtag, lanetag, identity) rather than assumed from tile geometry.
"""

from dataclasses import dataclass

import torch
from helpers.format_config import DataFormat, InputOutputFormat
from helpers.llk_params import DestAccumulation
from helpers.stimuli_config import StimuliConfig
from helpers.test_config import TestConfig
from helpers.test_variant_parameters import TemplateParameter

M32 = 0xFFFFFFFF
ELEMS = 1024  # one 32x32 tile
ROWS = 16     # dst rows the probe reads/writes
LANES = 32


@dataclass
class UIntTemplate(TemplateParameter):
    name: str
    value: int

    def convert_to_cpp(self) -> str:
        return f"constexpr std::uint32_t {self.name} = {self.value}u;"


def splitmix32(x):
    x = (x + 0x9E3779B9) & M32
    z = x
    z = ((z ^ (z >> 16)) * 0x85EBCA6B) & M32
    z = ((z ^ (z >> 13)) * 0xC2B2AE35) & M32
    return (z ^ (z >> 16)) & M32


def stim(i, lane):
    return splitmix32(i * 37 + lane)


def golden_rows(n, trips=8):
    """Final state vector per lane: golden_rows(n)[i][lane]."""
    out = [[0] * LANES for _ in range(n)]
    for lane in range(LANES):
        a = [stim(i, lane) for i in range(n)]
        for _ in range(trips):
            for i in range(n - 1):
                a[i] ^= a[i + 1]
            a[n - 1] ^= a[0]
        for i in range(n):
            out[i][lane] = a[i]
    return out


def run_probe(mode, input_vec, fmt=DataFormat.UInt32,
              dest_acc=DestAccumulation.Yes, unpack_to_dest=True):
    formats = InputOutputFormat(fmt, fmt)
    if fmt == DataFormat.UInt32:
        src = torch.tensor(input_vec, dtype=torch.int64)
    else:
        src = torch.tensor(input_vec, dtype=torch.bfloat16)
    config = TestConfig(
        "sources/sfpu_lreg_ladder_probe.cpp",
        formats,
        templates=[UIntTemplate("PROBE_MODE", mode)],
        runtimes=[],
        variant_stimuli=StimuliConfig(
            src,
            formats.input_format,
            torch.zeros_like(src),
            formats.input_format,
            formats.output_format,
            tile_count_A=1,
            tile_count_B=1,
            tile_count_res=4,
        ),
        dest_acc=dest_acc,
        unpack_to_dest=unpack_to_dest,
        disable_format_inference=True,
        compile_time_formats=True,
    )
    res = config.run().result
    assert len(res) == 4 * ELEMS, f"expected 4 tiles back, got {len(res)} elems"
    return [int(v) & M32 for v in res]


def test_lreg_ladder_goldens():
    ramp = list(range(ELEMS))

    # --- calibration ---
    rowtag = run_probe(1, ramp)   # output row i tagged 0xA00000+i
    lanetag = run_probe(2, ramp)  # every output row = vConstTileId
    ident = run_probe(0, ramp)    # output row i = input dst row i

    # T[(i, lane)] -> result-buffer index, for output rows 0..15.
    row_positions = {}
    for pos, v in enumerate(rowtag):
        if 0x00A00000 <= v < 0x00A00000 + ROWS:
            row_positions.setdefault(v - 0x00A00000, []).append(pos)
    if sorted(row_positions.keys()) != list(range(ROWS)):
        for name, res in (("rowtag", rowtag), ("lanetag", lanetag),
                          ("ident", ident)):
            for t in range(4):
                tile = res[t * 1024:(t + 1) * 1024]
                vals = sorted(set(tile))
                print(f"DIAG {name} tile{t}: nuniq={len(vals)} "
                      f"first8={[hex(v) for v in vals[:8]]} "
                      f"last4={[hex(v) for v in vals[-4:]]}")
            print(f"DIAG {name} tile3 elems0..15: "
                  f"{[hex(v) for v in res[3 * 1024:3 * 1024 + 16]]}")
            print(f"DIAG {name} tile1 elems0..15: "
                  f"{[hex(v) for v in res[1024:1024 + 16]]}")
    assert sorted(row_positions.keys()) == list(range(ROWS)), (
        f"rowtag rows found: {sorted(row_positions.keys())}")
    for i, positions in row_positions.items():
        assert len(positions) == LANES, (
            f"row {i}: {len(positions)} tagged positions (want {LANES})")

    T = {}
    for i, positions in row_positions.items():
        tags = [lanetag[p] for p in positions]
        assert len(set(tags)) == LANES, (
            f"row {i}: lanetag values not distinct: {sorted(set(tags))}")
        order = sorted(range(LANES), key=lambda k: tags[k])
        # lane l = the position holding the l-th smallest tileid tag
        for l, k in enumerate(order):
            T[(i, l)] = positions[k]
    print(f"CAL lanetag row0 sorted tags: "
          f"{sorted(lanetag[p] for p in row_positions[0])}")

    # M1inv[(i, lane)] -> input element index (from identity + unique ramp).
    m1inv = {}
    for i in range(ROWS):
        for l in range(LANES):
            v = ident[T[(i, l)]]
            assert 0 <= v < ELEMS, f"identity value {v} out of ramp range"
            m1inv[(i, l)] = v
    assert len(set(m1inv.values())) == ROWS * LANES, "M1 not injective"
    print(f"CAL M1inv row0 lanes0..7: {[m1inv[(0, l)] for l in range(8)]}")

    # --- ladder runs ---
    cases = [(3, 9), (4, 10), (5, 12), (6, 16), (7, 8),
             # lane DP: the UNSPILLED rungs, allocator-compiled
             # (-mtt-tensix-optimize-lreg-alloc -mtt-tensix-dst-layout-32b
             # via TT_LLK_EXTRA_COMPILER_OPTIONS): same DAG, same goldens.
             (8, 9), (9, 10), (10, 12), (11, 16)]
    for mode, n in cases:
        vec = [0] * ELEMS
        for i in range(n):
            for l in range(LANES):
                vec[m1inv[(i, l)]] = stim(i, l)
        out = run_probe(mode, vec)
        want = golden_rows(n)
        mismatches = []
        for i in range(n):
            for l in range(LANES):
                got = out[T[(i, l)]]
                if got != want[i][l]:
                    mismatches.append((i, l, got, want[i][l]))
        if mismatches:
            i, l, got, w = mismatches[0]
            print(f"MISMATCH mode={mode} n={n}: {len(mismatches)} lanes; "
                  f"first row={i} lane={l} got={got:08x} want={w:08x}")
            print("  got row0 lanes0..7:  "
                  + " ".join(f"{out[T[(0, k)]]:08x}" for k in range(8)))
            print("  want row0 lanes0..7: "
                  + " ".join(f"{want[0][k]:08x}" for k in range(8)))
        assert not mismatches, f"mode {mode} (N={n}): golden mismatch"
        print(f"LADDER_GOLDEN mode={mode} N={n}: "
              f"{n * LANES} lanes bit-exact vs host golden")


def run_probe_bf16_raw(mode):
    """Mode run under a TRUE 16-bit Dst layout (Float16_b, dest_acc No):
    returns the raw float list."""
    formats = InputOutputFormat(DataFormat.Float16_b, DataFormat.Float16_b)
    src = torch.zeros(ELEMS, dtype=torch.bfloat16)
    config = TestConfig(
        "sources/sfpu_lreg_ladder_probe.cpp",
        formats,
        templates=[UIntTemplate("PROBE_MODE", mode)],
        runtimes=[],
        variant_stimuli=StimuliConfig(
            src,
            formats.input_format,
            torch.zeros_like(src),
            formats.input_format,
            formats.output_format,
            tile_count_A=1,
            tile_count_B=1,
            tile_count_res=4,
        ),
        dest_acc=DestAccumulation.No,
        unpack_to_dest=False,
        disable_format_inference=True,
        compile_time_formats=True,
    )
    return [float(v) for v in config.run().result]


def test_dp8_layout_witness():
    """DP-8 measured negative witness: the same allocator-spilled mod0-0
    kernel is bit-clean under the declared 32-bit Dst layout and
    SILENTLY CORRUPTED under a 16-bit layout (the declaration's
    documented violation mode).  Diff-row positions are located by the
    marker mode (13); the scratch rows themselves are inside the packed
    window and legitimately carry spill data in both legs."""
    zeros = [0] * ELEMS

    marker32 = run_probe(13, zeros)
    pos32 = [i for i, v in enumerate(marker32) if v == 0x5A5A0000]
    print(f"DP8_WITNESS calibration32: {len(pos32)} marker lanes")
    assert len(pos32) == 4 * LANES, f"marker32 lanes: {len(pos32)}"

    ok32 = run_probe(12, zeros)
    bad_lanes = [(i, hex(ok32[i])) for i in pos32 if ok32[i] != 0]
    print(f"DP8_WITNESS 32bit-layout: {len(bad_lanes)} nonzero diff lanes "
          f"of {len(pos32)} (expect 0: spill rows disjoint, canary "
          f"round-trips) {bad_lanes[:4]}")
    assert not bad_lanes

    marker16 = run_probe_bf16_raw(13)
    nz16m = [i for i, v in enumerate(marker16) if v != 0.0]
    print(f"DP8_WITNESS calibration16: {len(nz16m)} marker lanes")
    assert len(nz16m) == 4 * LANES, f"marker16 lanes: {len(nz16m)}"

    bad16 = run_probe_bf16_raw(12)
    corrupted = [i for i in nz16m if bad16[i] != 0.0]
    print(f"DP8_WITNESS 16bit-layout: {len(corrupted)} corrupted diff lanes "
          f"of {len(nz16m)} (measured corruption: spill geometry collides "
          f"with the identity-mapped canary row)")
    assert corrupted, "16-bit layout leg unexpectedly clean"
