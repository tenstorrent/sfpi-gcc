// { dg-options "-mcpu=tt-qsr32-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// QSR counterpart of the Layer-1 effect-set golden: the typed TTSETRWC
// resolves to a set effect with its typed mask; QSR has no audited
// no-increment address-mode capability, so loads resolve to rwc=unknown.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=load latency=-1 lreg-read=0x0 lreg-write=0x1 port=own cc=r config=0x0 rwc=unknown dst=r encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=mad latency=1 lreg-read=0x1 lreg-write=0x1 port=own cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=sync latency=-1 lreg-read=0x0 lreg-write=0x0 port=none cc=none config=0x0 rwc=set:mask=0x4 dst=none encodable=no} 1 } }

__attribute__((noinline)) void effects_probe_qsr ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto m = __builtin_rvtt_sfpmad (a, a, a, 0);
  __builtin_rvtt_sfpwritelreg (m, 0);
  __builtin_rvtt_ttsetrwc (0, 4, 8, 0, 0, 4);
}
