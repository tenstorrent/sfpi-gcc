// Immediate-form SFPCONFIG builtin on Wormhole (shared functional model:
// BlackholeA0 SFPCONFIG.md defers verbatim to the WormholeB0 tree).
// { dg-options "-mcpu=tt-wh-tensix -fno-exceptions -fno-rtti -O2" }

void window_open ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
}

void window_close ()
{
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}

void varied ()
{
  __builtin_rvtt_sfpconfig_i (0x2104, 15, 1);
}

// { dg-final { scan-assembler {SFPCONFIG\t15, 4, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 0, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 8452, 1} } }
// { dg-final { scan-assembler-not {SFPLOADI} } }
// { dg-final { scan-assembler-not {SFPMOV} } }
