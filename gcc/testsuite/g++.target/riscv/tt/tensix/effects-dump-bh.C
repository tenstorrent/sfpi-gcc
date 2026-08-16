// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-dump-effects" }
// Golden self-check for the generated Layer-1 effect sets (macro-planner).
// Each audited instruction's full effect line is pinned; unaudited
// instructions (SFPENCC, SFPMOV) and raw asm must report opaque.
// { dg-final { scan-assembler-times {# xtt-effects: subunit=load latency=-1 lreg-read=0x0 lreg-write=0x1 port=own cc=r config=0x0 rwc=none dst=r encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=load latency=-1 lreg-read=0x0 lreg-write=0x2 port=own cc=r config=0x0 rwc=none dst=r encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=mad latency=1 lreg-read=0x3 lreg-write=0x1 port=own cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=simple latency=-1 lreg-read=0x6 lreg-write=0x6 port=borrows_mad cc=r config=0x0 rwc=none dst=none encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=round latency=-1 lreg-read=0x5 lreg-write=0x1 port=shared_simple_round cc=r config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=store latency=-1 lreg-read=0x1 lreg-write=0x0 port=none cc=r config=0x0 rwc=none dst=w encodable=yes} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=sync latency=-1 lreg-read=0x0 lreg-write=0x0 port=none cc=none config=0x0 rwc=inc:d=2,cr=0 dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=sync latency=-1 lreg-read=0x0 lreg-write=0x0 port=none cc=none config=0x0 rwc=face:d=16 dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: subunit=none latency=0 lreg-read=0x0 lreg-write=0x0 port=none cc=none config=0x0 rwc=none dst=none encodable=no} 1 } }
// { dg-final { scan-assembler-times {# xtt-effects: opaque} 3 } }

__attribute__((noinline)) void effects_probe ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto b = __builtin_rvtt_sfpload (nullptr, 64, 0, 0, 0, 7);
  auto m = __builtin_rvtt_sfpmad (a, b, a, 0);
  auto pair = __builtin_rvtt_sfpswap (m, b, 1);
  auto r = __builtin_rvtt_sfpselect2 (pair, 0);
  auto rr = __builtin_rvtt_sfpstochrnd_v (r, m, 4, 1);
  __builtin_rvtt_sfpstore (nullptr, rr, 128, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  __builtin_rvtt_ttdstface ();
  __builtin_rvtt_sfpnop ();
  asm volatile (".ttinsn %0" :: "n" (0x37120004));
}
