// Copyright (C) 2016-2022 Free Software Foundation, Inc.
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

// { dg-do run { target c++17 } }

#include <string>
#include <functional>
#include <unordered_map>
#include <algorithm>
#include <testsuite_hooks.h>

using test_type = std::unordered_map<int, int>;

template<typename T>
  struct xhash
  {
    auto operator()(const T& i) const noexcept
    { return ~std::hash<T>()(i); }
  };


namespace std
{
  template<typename T>
    struct __is_fast_hash<xhash<T>> : __is_fast_hash<std::hash<T>>
    { };
}

struct equal : std::equal_to<> { };

template<typename C1, typename C2>
bool equal_elements(const C1& c1, const C2& c2)
{
  if (c2.size() != c1.size())
    return false;
  for (auto& i : c1)
    if (c2.count(i.first) != c1.count(i.first))
      return false;
  return true;
}

void
test01()
{
  const test_type c0{ {1, 10}, {2, 20}, {3, 30} };
  test_type c1 = c0, c2 = c0;

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2 == c0 );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(std::move(c1));
  VERIFY( c1.empty() );
  VERIFY( c2 == c0 );
}

void
test02()
{
  const test_type c0{ {1, 10}, {2, 20}, {3, 30} };
  test_type c1 = c0;
  std::unordered_map<int, int, xhash<int>, equal> c2( c0.begin(), c0.end() );

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( equal_elements(c2, c0) );

  c1.merge(std::move(c2));
  VERIFY( c2.empty() );
  VERIFY( c1 == c0 );
}

void
test03()
{
  const test_type c0{ {1, 10}, {2, 20}, {3, 30} };
  test_type c1 = c0;
  std::unordered_multimap<int, int, xhash<int>, equal> c2( c0.begin(), c0.end() );
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( equal_elements(c2, c0) );

  c1 = c0;
  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( c2.size() == (2 * c0.size()) );
  VERIFY( c2.count(1) == 2 );
  VERIFY( c2.count(2) == 2 );
  VERIFY( c2.count(3) == 2 );

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );
}

void
test04()
{
  const std::unordered_map<std::string, int> c0
    { {"one", 10}, {"two", 20}, {"three", 30} };

  std::unordered_map<std::string, int> c1 = c0;
  std::unordered_multimap<std::string, int> c2( c0.begin(), c0.end() );
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( equal_elements(c2, c0) );

  c1 = c0;
  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( c2.size() == (2 * c0.size()) );
  VERIFY( c2.count("one") == 2 );
  VERIFY( c2.count("two") == 2 );
  VERIFY( c2.count("three") == 2 );

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );
}

void
test05()
{
  const std::unordered_map<std::string, int> c0
    { {"one", 10}, {"two", 20}, {"three", 30} };

  std::unordered_map<std::string, int> c1 = c0;
  std::unordered_multimap<std::string, int, xhash<std::string>, equal> c2( c0.begin(), c0.end() );
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( equal_elements(c2, c0) );

  c1 = c0;
  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( c2.size() == (2 * c0.size()) );
  VERIFY( c2.count("one") == 2 );
  VERIFY( c2.count("two") == 2 );
  VERIFY( c2.count("three") == 2 );

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );
}

template<typename T>
  using hash_f =
    std::function<std::size_t(const T&)>;

std::size_t
hash_func(const std::string& str)
{ return std::hash<std::string>{}(str);  }

std::size_t
xhash_func(const std::string& str)
{ return xhash<std::string>{}(str); }

namespace std
{
  template<typename T>
    struct __is_fast_hash<hash_f<T>> : __is_fast_hash<std::hash<T>>
    { };
}

void
test06()
{
  const std::unordered_map<std::string, int, hash_f<std::string>, equal>
    c0({ {"one", 10}, {"two", 20}, {"three", 30} }, 3, &hash_func);

  std::unordered_map<std::string, int, hash_f<std::string>, equal>
    c1(3, &hash_func);
  c1 = c0;
  std::unordered_multimap<std::string, int, hash_f<std::string>, equal>
    c2(c0.begin(), c0.end(), 3, &xhash_func);
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );

  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( equal_elements(c2, c0) );

  c1 = c0;
  c2.merge(c1);
  VERIFY( c1.empty() );
  VERIFY( c2.size() == (2 * c0.size()) );
  VERIFY( c2.count("one") == 2 );
  VERIFY( c2.count("two") == 2 );
  VERIFY( c2.count("three") == 2 );

  c1.merge(c2);
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( equal_elements(c2, c0) );

  c1.clear();
  c1.merge(std::move(c2));
  VERIFY( c1 == c0 );
  VERIFY( c2.empty() );
}

int
main()
{
  test01();
  test02();
  test03();
  test04();
  test05();
  test06();
}
