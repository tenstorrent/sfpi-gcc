// The modifier is not the configuration destination. A write to config 0
// must not turn an OPEN LaneConfig window into UNKNOWN merely because its
// modifier happens to be 15.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void config_modifier_is_not_destination ()
{
  auto v = __builtin_rvtt_sfpreadlreg (0);
  __builtin_rvtt_sfpconfig_i (4, 15, 1);
  __builtin_rvtt_sfpwriteconfig_v (v, 15, 0);
  auto y = __builtin_rvtt_sfpand (v, v);
  __builtin_rvtt_sfpwritelreg (y, 5);
  __builtin_rvtt_sfpconfig_i (0, 15, 1);
}

// { dg-error "dest-index-window-violation" "" { target *-*-* } 0 }
