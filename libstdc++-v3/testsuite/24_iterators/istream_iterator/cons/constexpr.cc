// { dg-do compile { target c++11 } }

// Copyright (C) 2010-2022 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 3, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

#include <iterator>
#include <testsuite_common_types.h>

int main()
{
  __gnu_test::constexpr_default_constructible test;
  test.operator()<std::istream_iterator<char>>();

  // LWG 3600. Making istream_iterator copy constructor trivial is an ABI break
  __gnu_test::constexpr_single_value_constructible test2;
  test2.operator()<std::istream_iterator<char>, std::istream_iterator<char>>();

  return 0;
}
