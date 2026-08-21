# SPDX-FileCopyrightText: © 2026 Tenstorrent AI ULC
# SPDX-License-Identifier: Apache-2.0

"""Lane EH pre-RA pressure scheduler: CRAQ probe.

Bit-exactness of the scheduled ten-live fire shapes on the pinned
blackhole sim.  The whole probe TU is compiled under
-mtt-tensix-optimize-pressure-schedule-prera (carried by
TT_LLK_EXTRA_COMPILER_OPTIONS): the wide bodies do not compile without
it (lreg-pressure-exceeded), which is the fire twin's own control.

  - int case:  wide (mode 3) and sequential (mode 4) bodies both match
    the HOST-computed XOR golden per lane (splitmix32 stimulus) -- the
    schedule changed the issue order, never a value.
  - float case: wide (mode 5) vs sequential (mode 6) outputs are
    BIT-IDENTICAL (differential; same dataflow, mad-family ops, no
    reassociation anywhere).

The tensor<->dst-row mapping is derived empirically from the same three
calibration kernels as the lane DS ladder probe.
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
ROWS = 16     # dst rows the calibration reads/writes
LANES = 32
NIN = 5       # fire-body input rows


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


def float_stim(i, lane):
    # positive normals in [1, 2): differential hygiene (values still
    # only need determinism -- both legs share the dataflow).
    return 0x3F800000 | (stim(i, lane) & 0x007FFFFF)


def golden_int_rows():
    """Host golden for the int fire dataflow: [r0 r1 r2 r3 out][lane]."""
    out = [[0] * LANES for _ in range(5)]
    for lane in range(LANES):
        a, b, c, d, e = (stim(i, lane) for i in range(5))
        u0, u1, u2, u3 = a ^ b, b ^ c, c ^ d, d ^ e
        u4, u5, u6, u7 = e ^ a, a ^ c, b ^ d, c ^ e
        r0, r1, r2, r3 = u0 ^ u1, u2 ^ u3, u4 ^ u5, u6 ^ u7
        s0, s1 = r0 ^ r1, r2 ^ r3
        vals = [r0, r1, r2, r3, s0 ^ s1]
        for i, v in enumerate(vals):
            out[i][lane] = v
    return out


def run_probe(mode, input_vec):
    formats = InputOutputFormat(DataFormat.UInt32, DataFormat.UInt32)
    src = torch.tensor(input_vec, dtype=torch.int64)
    config = TestConfig(
        "sources/sfpu_prera_probe.cpp",
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
        dest_acc=DestAccumulation.Yes,
        unpack_to_dest=True,
        disable_format_inference=True,
        compile_time_formats=True,
    )
    res = config.run().result
    assert len(res) == 4 * ELEMS, f"expected 4 tiles back, got {len(res)} elems"
    return [int(v) & M32 for v in res]


def test_prera_fire_goldens():
    ramp = list(range(ELEMS))

    # --- calibration (identical to the lane DS ladder probe) ---
    rowtag = run_probe(1, ramp)
    lanetag = run_probe(2, ramp)
    ident = run_probe(0, ramp)

    row_positions = {}
    for pos, v in enumerate(rowtag):
        if 0x00A00000 <= v < 0x00A00000 + ROWS:
            row_positions.setdefault(v - 0x00A00000, []).append(pos)
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
        for l, k in enumerate(order):
            T[(i, l)] = positions[k]

    m1inv = {}
    for i in range(ROWS):
        for l in range(LANES):
            v = ident[T[(i, l)]]
            assert 0 <= v < ELEMS, f"identity value {v} out of ramp range"
            m1inv[(i, l)] = v
    assert len(set(m1inv.values())) == ROWS * LANES, "M1 not injective"

    # --- int fire: wide (scheduled) and sequential both vs host golden ---
    vec = [0] * ELEMS
    for i in range(NIN):
        for l in range(LANES):
            vec[m1inv[(i, l)]] = stim(i, l)
    want = golden_int_rows()
    for mode, name in ((3, "wide-int-fire"), (4, "seq-int-golden")):
        out = run_probe(mode, vec)
        mismatches = []
        for i in range(5):
            for l in range(LANES):
                got = out[T[(i, l)]]
                if got != want[i][l]:
                    mismatches.append((i, l, got, want[i][l]))
        if mismatches:
            i, l, got, w = mismatches[0]
            print(f"MISMATCH {name}: {len(mismatches)} lanes; first "
                  f"row={i} lane={l} got={got:08x} want={w:08x}")
        assert not mismatches, f"{name}: golden mismatch"
        print(f"PRERA_GOLDEN {name}: {5 * LANES} lanes bit-exact vs host")

    # --- float fire: wide (scheduled) vs sequential differential ---
    fvec = [0] * ELEMS
    for i in range(NIN):
        for l in range(LANES):
            fvec[m1inv[(i, l)]] = float_stim(i, l)
    wide = run_probe(5, fvec)
    seq = run_probe(6, fvec)
    mismatches = []
    for i in range(4):
        for l in range(LANES):
            got, ref = wide[T[(i, l)]], seq[T[(i, l)]]
            if got != ref:
                mismatches.append((i, l, got, ref))
    if mismatches:
        i, l, got, ref = mismatches[0]
        print(f"MISMATCH float-diff: {len(mismatches)} lanes; first "
              f"row={i} lane={l} wide={got:08x} seq={ref:08x}")
    assert not mismatches, "float differential: wide != seq"
    print(f"PRERA_DIFFERENTIAL float: {4 * LANES} lanes bit-identical "
          "wide-vs-seq")
