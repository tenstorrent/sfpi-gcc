// Delivered-marker state tracking (lane FR, lane FP's marker-in-payload
// composition row): an ENABLE_DEST_INDEX OPEN marker captured inside a
// no-exec payload does NOT open the window at the record site (the word
// is swallowed) -- it opens it where the launch PLAYS it back.  The
// LReg5 write after the launch executes under the delivered-OPEN state
// and is a hard error the positional model could never see.
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }

using vec_t = __xtt_vector;

void delivered_marker_reopens ()
{
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 1);  // no-exec capture
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);              // OPEN marker: recorded, not executed
  vec_t x = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t z = __builtin_rvtt_sfpmul (x, x, 0);
  __builtin_rvtt_sfpstore (nullptr, z, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttreplay (nullptr, 4, 0, 0, 0, 0, 0);  // launch: window OPENS here
  vec_t a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  vec_t b = __builtin_rvtt_sfpand (a, a);
  __builtin_rvtt_sfpwritelreg (b, 5);                   // executes under delivered-OPEN
  __builtin_rvtt_sfpstore (nullptr, b, 0, 0, 0, 0, 7);
}

// { dg-error "dest-index-window-violation" "" { target *-*-* } 0 }
