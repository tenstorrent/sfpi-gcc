// { dg-options "-mcpu=tt-wh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Check the -mtt-tensix-dump-effects effect sets on Wormhole (mode 3 is rwc=none, other modes rwc=unknown).
// { dg-final { scan-assembler-times {# xtt-effects: subunit=load latency=0 lreg-read=0x0 lreg-write=0x1 port=own cc=r config=0x0 rwc=none dst=r encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=mad latency=1 lreg-read=0x1 lreg-write=0x1 port=own cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=store latency=0 lreg-read=0x1 lreg-write=0x0 port=none cc=r config=0x0 rwc=unknown dst=w encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=sync latency=-1 lreg-read=0x0 lreg-write=0x0 port=none cc=none config=0x0 rwc=face:d=16 dst=none encodable=no} 1 } }

__attribute__((noinline)) void effects_probe_wh ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 3);
  auto m = __builtin_rvtt_sfpmad (a, a, a, 0);
  __builtin_rvtt_sfpstore (nullptr, m, 128, 0, 0, 0, 2);
  __builtin_rvtt_ttdstface ();
}
