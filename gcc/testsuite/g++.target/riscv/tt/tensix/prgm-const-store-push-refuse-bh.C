// Volatile-store near misses, each refusing by name and blocking every
// allocation in the TU.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "unaudited raw opcode" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "raw SFPCONFIG writes LaneConfig" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "pc-buf-write-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "debug-regs-write-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "instruction-buffer offset unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-store-alias-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];
extern volatile unsigned int some_external_block[];

// 1. A raw SFPU-class word pushed by store evades nothing.
void push_unaudited_word ()
{
  __instrn_buffer[0] = 0x81000000;
}

// 2. A LaneConfig-writing SFPCONFIG pushed by store to the constant
// FIFO aperture.
void push_laneconfig_write ()
{
  *(vu32 *) 0xFFE40000 = 0x910100F1;
}

// 3. A PC_BUF word with no recorded semantics.
void pcbuf_unproven ()
{
  *(vu32 *) 0xFFE8000C = 0;
}

// 4. The debug-register block (documented instruction-injection
// interface).
void debug_regs_store ()
{
  *(vu32 *) 0xFFB12080 = 1;
}

// 5. A non-zero constant instruction-buffer offset is not the
// architected aperture.
void instrn_offset_unproven ()
{
  *(vu32 *) 0xFFE40004 = 0x02000000;
}

// 6. A volatile store through an unrecognized external anchor: the
// FIFO-alias question is undischargeable.
void external_anchor_store (unsigned ix)
{
  some_external_block[ix] = 0;
}

// The candidate loop that must not allocate.
void wants_a_constant ()
{
  auto x = __builtin_rvtt_sfpreadlreg (0);
  auto s = __builtin_rvtt_sfpreadlreg (1);
  for (unsigned ix = 0; ix != 32; ++ix)
    {
      auto prod = __builtin_rvtt_sfpmul (x, s, 0);
      x = __builtin_rvtt_sfpaddi (nullptr, prod, 0x42fe, 0, 0, 0);
    }
  __builtin_rvtt_sfpwritelreg (x, 0);
}
