// Raw content inside a typed ENABLE_DEST_INDEX window cannot be
// audited against TEN-2932 and errors by name.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

void raw_in_window ()
{
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  __asm__ __volatile__ (".ttinsn 0x91000001");	// { dg-error "crosslane-window-raw-unproven" }
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}
