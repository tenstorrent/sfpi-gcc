// { dg-do compile }
// { dg-add-options using-deprecated }

// std::random_shuffle was deprecated in C++17 and removed in C++17.

#include <algorithm>

std::ptrdiff_t rando(std::ptrdiff_t n);

void
test_depr(int* first, int* last)
{
  std::random_shuffle(first, last);
  // { dg-warning "deprecated" "" { target c++14 } 13 }

  std::random_shuffle(first, last, rando);
  // { dg-warning "deprecated" "" { target c++14 } 16 }
}
