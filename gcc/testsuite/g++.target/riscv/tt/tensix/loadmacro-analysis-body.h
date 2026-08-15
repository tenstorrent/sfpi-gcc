void load_mul_store ()
{
  auto a = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto b = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 0);
  auto result = __builtin_rvtt_sfpmul (a, b, 0);
  __builtin_rvtt_sfpstore (nullptr, result, 0, 0, 0, 0, 0);
}

