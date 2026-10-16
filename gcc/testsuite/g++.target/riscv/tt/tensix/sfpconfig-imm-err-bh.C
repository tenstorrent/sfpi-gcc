// Check that the immediate-form SFPCONFIG builtin rejects invalid operands.
// { dg-options "-mcpu=tt-bh-tensix -fno-exceptions -fno-rtti -O2" }

// Only LaneConfig (15) is accepted as a destination.  LoadMacroConfig
// (0-8) belongs to the macro planner, 9-10 are NonContractualBehavior and
// 11-14 are not modelled.
void dest_macro_misc ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 8, 1);	// { dg-error "sfpconfig-imm-dest-unaudited" }
}

void dest_prgm_const ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 11, 1);	// { dg-error "sfpconfig-imm-dest-unaudited" }
}

// Mod1 envelope: MOD1_IMM16_IS_VALUE (bit 0) is mandatory -- without it
// the value comes from LReg[0] and the form is not immediate; the
// lane-mask bit (8) likewise takes the value from LReg[0].
void mod_value_form ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 0);	// { dg-error "invalid mod1 value" }
}

void mod_or_without_value ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 2);	// { dg-error "invalid mod1 value" }
}

void mod_lane_mask ()
{
  __builtin_rvtt_sfpconfig_i (0x4444, 15, 8);	// { dg-error "invalid mod1 value" }
}

void mod_lane_mask_value ()
{
  __builtin_rvtt_sfpconfig_i (0x4444, 15, 9);	// { dg-error "invalid mod1 value" }
}

// Operand ranges and constant-ness.
void imm_out_of_range ()
{
  __builtin_rvtt_sfpconfig_i (0x10000, 15, 1);	// { dg-error "out of range" }
}

void non_constant_imm (unsigned x)
{
  __builtin_rvtt_sfpconfig_i (x, 15, 1);	// { dg-error "not a constant" }
}

void non_constant_dest (unsigned d)
{
  __builtin_rvtt_sfpconfig_i (0x4, d, 1);	// { dg-error "not a constant" }
}
