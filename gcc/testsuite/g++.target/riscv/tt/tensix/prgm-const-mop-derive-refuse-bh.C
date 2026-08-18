// MOP effect derivation near misses: every template-slot write that
// the audited table cannot pin refuses the MOP admission by name, and
// no allocation happens anywhere in the TU.  One near-miss class per
// function; all reasons appear in the dump (the first is the recorded
// TU refusal).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "mop-template-word-unproven: unaudited raw opcode .template slot 5, word 0x9f000000." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-template-replay-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-template-nested-unproven" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-template-word-unproven: unclassifiable composed word" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "mop-template-word-unproven: composed word of a field-sensitive or unaudited class" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump "refused .opaque-region-undeclared.: mop-template" "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

typedef volatile unsigned int vu32;
extern volatile unsigned int __instrn_buffer[];

// 1. An unaudited raw opcode in an instruction slot.
void slot_word_unproven ()
{
  ((vu32 *) 0xFFB80000)[5] = 0x9f000000;
  asm volatile (".ttinsn %0" :: "i" (0x01800000));
}

// 2. A REPLAY word in a slot: the recorded-content audit is a later
// increment.
void slot_replay_unproven ()
{
  ((vu32 *) 0xFFB80000)[3] = 0x04000040;
}

// 3. A nested MOP word in a slot: expander re-dispatch unproven.
void slot_nested_mop ()
{
  ((vu32 *) 0xFFB80000)[4] = 0x01800000;
}

// 4. A runtime slot value with no constant opcode base.
void slot_opaque_value (unsigned w)
{
  ((vu32 *) 0xFFB80000)[6] = w;
}

// 5. A runtime composition over a field-sensitive class (SFPLOADI:
// the destination field could hide in the runtime operand).
void slot_field_sensitive (unsigned imm)
{
  ((vu32 *) 0xFFB80000)[7] = 0x71000000 + (imm & 0xffff);
}

// The candidate loop that must NOT allocate while any slot is
// unproven and a MOP is delivered.
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
