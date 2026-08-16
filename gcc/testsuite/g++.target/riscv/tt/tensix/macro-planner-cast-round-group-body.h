/* Cast-round group shape (the typecast face body): one all-lanes
   proof, eight adjacent load/cast/round/store rows with typed Dst
   increments, the frozen pass's proven envelope (load mode 6, round
   instr_mod1 1 with zero imm8, store mode 2).  */
#if __riscv_xtttensixwh
constexpr unsigned no_increment = 3;
#else
constexpr unsigned no_increment = 7;
#endif

#ifndef CAST_ROUND_MOD1
#define CAST_ROUND_MOD1 1
#endif
#ifndef CAST_ROUND_IMM8
#define CAST_ROUND_IMM8 0
#endif

#define ROW()                                                                 \
  do                                                                          \
    {                                                                         \
      auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6,              \
					    no_increment);                    \
      auto cast = __builtin_rvtt_sfpcast (loaded, 0);                         \
      auto rounded                                                            \
	= __builtin_rvtt_sfpstochrnd_i (nullptr, cast, CAST_ROUND_IMM8,       \
					0, 0, CAST_ROUND_MOD1, 0);            \
      __builtin_rvtt_sfpstore (nullptr, rounded, 0, 0, 0, 2,                  \
			       no_increment);                                 \
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);                                   \
    }                                                                         \
  while (0)

__attribute__((noinline)) void cast_round_rows ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW (); ROW ();
}
