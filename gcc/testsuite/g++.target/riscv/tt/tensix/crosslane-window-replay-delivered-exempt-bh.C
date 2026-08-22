// Acceptance twin of the replay-delivery violation (lane FR): the same
// record-outside/launch-inside composition whose delivered companion
// writes are all TEN-2932-EXEMPT opcodes (SFPLOAD into LReg5/6/7) must
// keep compiling silently -- the checker audits the delivered words, it
// does not refuse the delivery machinery.  The scans prove the former
// actually fired (the composition is present, not vacuously absent).
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane" }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 1, 1" 1 } }
// { dg-final { scan-assembler-times "TTREPLAY\t0, 4, 0, 0" 3 } }

using vec_t = __xtt_vector;

#define ROW do {                                                 \
    vec_t xa = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);  \
    __builtin_rvtt_sfpwritelreg (xa, 5);                         \
    vec_t xb = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);  \
    __builtin_rvtt_sfpwritelreg (xb, 6);                         \
    vec_t xc = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);  \
    __builtin_rvtt_sfpwritelreg (xc, 7);                         \
    __builtin_rvtt_sfpstore (nullptr, xc, 0, 0, 0, 0, 7);        \
  } while (0)

void delivered_exempt ()
{
  ROW;
  __builtin_rvtt_sfpconfig_i (0x4, 15, 1);
  ROW; ROW; ROW;
  __builtin_rvtt_sfpconfig_i (0x0, 15, 1);
}
