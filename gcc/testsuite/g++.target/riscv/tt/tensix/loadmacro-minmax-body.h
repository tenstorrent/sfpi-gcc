void load_minmax_store ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto pair = __builtin_rvtt_sfpswap (a, b, 1);
  auto result = __builtin_rvtt_sfpselect2 (pair, 0);
  __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 0, 0);
}

void dynamic_load_encoding (unsigned int insn)
{
  auto a = __builtin_rvtt_sfpload (nullptr, insn, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto pair = __builtin_rvtt_sfpswap (a, b, 1);
  auto result = __builtin_rvtt_sfpselect2 (pair, 0);
  __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 0, 0);
}

void unclosed_store_dependency ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto pair = __builtin_rvtt_sfpswap (a, b, 1);
  auto unrelated = __builtin_rvtt_sfpreadlreg (9);
  __builtin_rvtt_sfpstore (nullptr, unrelated, 0, 0, 0, 0, 0);
  __builtin_rvtt_sfpwritelreg (__builtin_rvtt_sfpselect2 (pair, 0), 0);
}
