// Parked-transpose body shared by the transp-involution tests: a
// straight-line accumulation over three gathered 4-row groups, with the
// accumulators parked in Dst scratch around every transpose (the shape a
// clean semantic source must use while the transpose companion-bank
// effect is unmodeled).  MODE is the target's no-increment address mode;
// FMT the park format (3 = FP32, 4 = INT32 class).
using sfpu_t = decltype (__builtin_rvtt_sfpreadlreg (9));

template <int MODE, int FMT, int PARK_A, int PARK_B, int B0, int B1, int B2>
struct parked
{
  template <int BASE>
  static inline void block (sfpu_t &acc0, sfpu_t &acc1)
  {
    __builtin_rvtt_sfpstore (nullptr, acc0, PARK_A, 0, 0, FMT, MODE);
    __builtin_rvtt_sfpstore (nullptr, acc1, PARK_B, 0, 0, FMT, MODE);
    auto x0 = __builtin_rvtt_sfpload (nullptr, BASE + 0, 0, 0, 0, MODE);
    auto x1 = __builtin_rvtt_sfpload (nullptr, BASE + 2, 0, 0, 0, MODE);
    auto x2 = __builtin_rvtt_sfpload (nullptr, BASE + 16, 0, 0, 0, MODE);
    auto x3 = __builtin_rvtt_sfpload (nullptr, BASE + 18, 0, 0, 0, MODE);
    auto r = __builtin_rvtt_sfptransp (x0, x1, x2, x3);
    x0 = __builtin_rvtt_sfpselect4 (r, 0);
    x1 = __builtin_rvtt_sfpselect4 (r, 1);
    x2 = __builtin_rvtt_sfpselect4 (r, 2);
    x3 = __builtin_rvtt_sfpselect4 (r, 3);
    acc0 = __builtin_rvtt_sfpload (nullptr, PARK_A, 0, 0, FMT, MODE);
    acc1 = __builtin_rvtt_sfpload (nullptr, PARK_B, 0, 0, FMT, MODE);
    auto d0 = __builtin_rvtt_sfpadd (x0, acc0, 0);
    acc0 = __builtin_rvtt_sfpmad (d0, x1, acc0, 0);
    auto d1 = __builtin_rvtt_sfpadd (x2, acc0, 0);
    acc1 = __builtin_rvtt_sfpmad (d0, d1, acc1, 0);
    (void) x3;
  }

  static void run ()
  {
    auto acc0 = __builtin_rvtt_sfpreadlreg (9);
    auto acc1 = __builtin_rvtt_sfpreadlreg (9);
    block<B0> (acc0, acc1);
    block<B1> (acc0, acc1);
    block<B2> (acc0, acc1);
    __builtin_rvtt_sfpstore (nullptr, acc0, PARK_A, 0, 0, FMT, MODE);
    __builtin_rvtt_sfpstore (nullptr, acc1, PARK_B, 0, 0, FMT, MODE);
  }
};
