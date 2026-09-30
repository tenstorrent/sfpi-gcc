// Immediate-form SFPCONFIG builtin: encode forms.
// The builtin takes (imm16, dest, mod1) and must assemble to the gas
// spelling "SFPCONFIG <dest>, <imm16>, <mod1>" with NO LReg staging --
// that register-free property is the builtin's contract (the value form
// stages through LReg[0]).
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

// The hand kernels' ENABLE_DEST_INDEX window open/close words
// (TTI_SFPCONFIG(0x4, 0xF, 1) / TTI_SFPCONFIG(0x0, 0xF, 1)).
void window_open ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
}

void window_close ()
{
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

// Comparator-direction flip + window (deepseek_top32_rm's 0x104 word).
void dir_flip_window ()
{
  __builtin_rvtt_sfpconfig_i (0x104, 15, 1);
}

// Renamed-equivalent with a varied constant: same shape, different
// imm16 -- the emission must be value-independent (genericity battery).
void renamed_equivalent_varied ()
{
  __builtin_rvtt_sfpconfig_i (0x1104, 15, 1);
}

// Bitwise OR / AND / XOR manipulation forms (MOD1_BITWISE_* | value).
void laneconfig_or ()
{
  __builtin_rvtt_sfpconfig_i (0x100, 15, 3);
}

void laneconfig_and ()
{
  __builtin_rvtt_sfpconfig_i (0xfeff, 15, 5);
}

void laneconfig_xor ()
{
  __builtin_rvtt_sfpconfig_i (0x100, 15, 7);
}

// { dg-final { scan-assembler {SFPCONFIG\t15, 4, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 0, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 260, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 4356, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 256, 3} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 65279, 5} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 256, 7} } }
// No LReg staging anywhere in this TU: the whole point of the imm form.
// { dg-final { scan-assembler-not {SFPLOADI} } }
// { dg-final { scan-assembler-not {SFPMOV} } }
