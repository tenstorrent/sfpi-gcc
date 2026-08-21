// QSR has no audited latency facts (the simulator refuses these opcode
// semantics): the whole target refuses by name before any region work.
// { dg-do compile }
// { dg-options "-mcpu=tt-qsr32-tensix -O2 -mtt-tensix-optimize-pressure-schedule-prera -fdump-rtl-rvtt_lp_schedule_prera" }
// { dg-final { scan-rtl-dump "Prera-pressure-schedule refused: no audited latency facts for this target" "rvtt_lp_schedule_prera" } }

int scalar_only (int x) { return x + 1; }
