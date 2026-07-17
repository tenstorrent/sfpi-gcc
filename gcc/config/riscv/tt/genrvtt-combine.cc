/* Generate gimple combine machinery
   Copyright (C) 2026 Tenstorrent Inc.
   Originated Nathan Sidwell (nsidwell@tenstorrent.com, nathan@acm.org).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 3, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#define INCLUDE_ALGORITHM
#define INCLUDE_VECTOR
#define INCLUDE_STRING
#include "bconfig.h"
#include "system.h"

#include <cstdio>
#include <string_view>
#include <unordered_map>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

/* WARNING, BNF may have bit rotted.  The lexer strips white space including
   C/C++ comments (both line and block forms)

  combine (TARGET) {
    { enable } [opt]
    var = bltin (args, ...);
    ...
    { pred }
    { init } [opt]
    var = bltin (args, ...);
    ...
    { fini } [opt]
  }

  file : def*

  def : combine

  code: '{' brace-balanced-chars '}'

  ident : [a-zA-Z_][a-zA-Z0-9_]*

  expr : [A-Z0-9(] paren-balanced-chars until ) or ,

  combine : 'combine' '(' ident ')' '{' enable?
	shape+ pred setup? shape+ finalize? '}'

  shape: var modifiers? '=' ident '(' args ')' ';'

  setup: code

  finalize: code

  var: ident

  modifiers : ident
	    | modifiers '|' ident

  args: arg
       | args ',' arg

  arg: '%'? ident
       | expr
*/

namespace {

class Lexer;
class Stream;

struct Var {
  std::string_view name;
  bool is_lhs;
  bool is_pat;
  bool is_used;
  unsigned remap;
};
using Vars = std::vector<Var>;

class Ref {
public:
  unsigned slot = 0;

public:
  bool lookup (Lexer &lexer, Vars &vars, std::string_view name, bool lhs, bool is_pattern);
};

struct Arg {
  Ref var;
  std::string_view expr;
  bool commutes = false;

public:
  bool is_var () const { return expr.empty (); }
  bool parse (Lexer &, Vars &, bool, bool);
};

class Shape {
public:
  Ref lhs;
  unsigned used_by_mask = 0; // Which patterns use our LHS
  std::string_view modifiers;
  std::string_view func;
  std::vector<Arg> args;

public:
  bool parse (Lexer &, Vars &, std::string_view var, unsigned &, unsigned &, bool);
  void emit (Stream &, std::vector<unsigned> const &) const;

private:
  bool parse_args (Lexer &, Vars &, unsigned &, bool);
};
using Shapes = std::vector<Shape>;

struct Code {
  std::string_view code;
  unsigned lineno = 0;

  operator bool () const { return lineno; }
  void rewind (unsigned count) {
    code = std::string_view (code.data () - count, code.size () + count);
  };
};

class Lexer {
  // WARNING: Embedded NUL chars will be treated as EOF

public:
  unsigned lineno = 1;

private:
  const char *name;
  char const *ptr = nullptr;

public:
  Lexer (char const *n)
    : name (n) {}

  void buffer (char *b, size_t len)
  {
    ptr = b;
    b[len] = 0;
  }

public:
  const char *pos () const { return ptr; }
  const char *next ();

public:
  void advance (char const *p) { ptr = p; }
  void advance (std::size_t l) { ptr += l; }

public:
  void error (char const *fmt, ...);

public:
  bool consume_ident (std::string_view &, bool = false);
  bool consume_expr (std::string_view &, bool = false);
  bool consume_code (Code &, bool = false);

  bool consume (char c, bool = false);
  bool consume (std::string_view ident, bool = false);
};

struct Stream {
  FILE *fd;
  char const *out = nullptr;
  char const *src = nullptr;
  unsigned lineno = 1;

public:
  Stream (char const *out, char const *src)
    : fd (fopen (out, "w")), out (out), src (src) {}

public:
  void push (unsigned);
  void pop ();

  template<typename Arg>
  void print (Arg arg);

  template<typename Arg, typename ...Args>
  void print (Arg arg, Args ...args) {
    print (arg);
    print (args...);
  }
};

}

template<>
void
Stream::print (std::string_view str)
{
  fwrite (str.data (), 1, str.size (), fd);
  for (size_t lf = 0; (lf = str.find ('\n', lf)) != str.npos; lf++)
    lineno++;
}

template<>
void
Stream::print (char const *str)
{
  print (std::string_view (str));
}

template<>
void
Stream::print (bool n) {
  fprintf (fd, "%s", n ? "true" : "false");
}

template<>
void
Stream::print (unsigned n) {
  fprintf (fd, "%d", n);
}

template<>
void
Stream::print (std::size_t n) {
  fprintf (fd, "%zu", n);
}

void __attribute__((format (printf, 2, 3)))
Lexer::error (char const *fmt, ...)
{
  fprintf (stderr, "%s:%d: ", name, lineno);

  va_list args;
  va_start (args, fmt);
  vfprintf (stderr, fmt, args);
  va_end (args);

  fprintf (stderr, "\n");
}

bool
Lexer::consume (char e, bool optional) {
  char c = *next ();
  if (c == e)
    {
      advance (1);
      return true;
    }

  if (!optional)
    error ("Expected '%c' found '%c'", e, c);
  return false;
}

bool
Lexer::consume (std::string_view e, bool optional) {
  std::string_view found;
  auto p = next ();
  if (consume_ident (found, true))
    {
      if (found == e)
	return true;
      ptr = p;
    }
  else
    found = std::string_view (pos (), 1);

  if (!optional)
      error ("Expected '%.*s' found '%.*s'",
	     int (e.size ()), e.data (),
	     int (found.size ()), found.data ());

  return false;
}

const char *
Lexer::next () {
  for (;;) {
    switch (*ptr)
      {
      default:
	return ptr;

      case '\n':
	lineno++;
	[[fallthrough]];

      case ' ':
      case '\t':
      case '\v':
	ptr++;
	continue;

      case '/':
	{
	  switch (ptr[1])
	    {
	    default:
	      return ptr;

	    case '/':
	      // Line comment
	      while (*++ptr)
		if (*ptr == '\n')
		  break;
	      continue;

	    case '*':
	      // Block comment
	      while (*++ptr)
		if (*ptr == '\n')
		  lineno++;
		else if (*ptr == '*' && ptr[1] == '/')
		  {
		    ptr += 2;
		    break;
		  }
	      continue;
	    }
	}
      }
  }
}

bool
Lexer::consume_ident (std::string_view &res, bool optional)
{
  auto start = pos ();

  if ((*start >= 'a' && *start <= 'z')
      || (*start >= 'A' && *start <= 'Z')
      || *start == '_')
    {
      auto p = start + 1;
      while ((*p >= 'a' && *p <= 'z')
	     || (*p >= 'A' && *p <= 'Z')
	     || (*p >= '0' && *p <= '9')
	     || *p == '_')
	p++;
      advance (p);
      res = std::string_view (start, p - start);
      return true;
    }
  if (!optional)
    error ("expected identifier, found '%c'", *pos ());
  return false;
}

bool
Lexer::consume_expr (std::string_view &res, bool optional)
{
  auto start = pos ();
  if ((*start >= '0' && *start <= '9')
      || (*start >= 'A' && *start <= 'Z')
      || *start == '(')
    {
      // Consume balanced parens until , or )
      unsigned depth = 0;
      while (advance (1), true)
	{
	  switch (*next ())
	    {
	    case 0:
	      goto done;
	    case ')':
	      if (!depth)
		goto done;
	      depth--;
	      break;
	    case ',':
	      if (!depth)
		goto done;
	      break;
	    case '(':
	      depth++;
	      break;
	    }
	}
    done:
      res = std::string_view (start, pos () - start);
      return true;
    }

  if (!optional)
    error ("expected expression, found '%c'", *start);

  return false;
}

bool
Lexer::consume_code (Code &res, bool optional)
{
  if (consume ('{', true))
    {
      unsigned line = lineno;
      auto start = pos () - 1;
      unsigned depth = 0;
      bool has_contents = false;
      for (;; advance (1))
	{
	  auto c = *next ();
	  if (!c)
	    break;
	  if (c == '{')
	    depth++;
	  else if (c == '}')
	    {
	      if (!depth)
		{
		  advance (1);
		  break;
		}
	      depth--;
	    }
	  else if (c == '"' || c == '\'')
	    {
	      // skip string
	      auto p = pos () + 1;
	      for (; *p && *p != c; p++)
		if (*p == '\\'
		    && !*++p)
		  break;
	      advance (p);
	      if (!*p)
		break;
	    }
	  has_contents = true;
	}
      if (!has_contents)
	line = 0;
      while (start[-1] == ' ' || start[-1] == '\t')
	start--;
      res = Code{std::string_view (start, pos () - start), line};
      return true;
    }

  if (!optional)
    error ("expected {...} block, found '%c'", *pos ());
  return false;
}

namespace {
struct Combine {
  enum Hooks
    {
      H_Enable,
      H_Pred,
      H_Init,
      H_Fini,
      H_HWM
    };

public:
  unsigned lineno = 0;
  Vars vars;
  std::vector<unsigned> remap;
  std::string_view target;
  Shapes pats;
  Shapes reps;
  Code hooks[H_HWM];
  unsigned max_args = 0;

  unsigned rep_lhs_hwm = 0;
  unsigned pat_var_hwm = 0;

  unsigned replace_mask = 0;
  unsigned rep_use_mask = 0;
  unsigned commute_mask = 0;

public:
  bool parse (Lexer &);
  void emit_hook (Stream &, Hooks) const;
  void emit_hook_name (Stream &, Hooks) const;

private:
  bool parse_patterns (Lexer &, bool is_pattern = false);
};
using Combines = std::vector<Combine>;
using Helpers = std::vector<Code>;
}

bool
Ref::lookup (Lexer &lexer, Vars &vars,
	     std::string_view name, bool lhs, bool pattern)
{
  // Yeah, O(N), but N is small and we don't care about speed here anyway
  for (unsigned ix = vars.size (); ix--;)
    if (vars[ix].name == name)
      {
	slot = ix;
	if (lhs && (pattern || !vars[ix].is_lhs))
	  {
	    lexer.error (pattern ? "'%.*s' already defined"
			 : "'%.*s' not a LHS",
			 int (name.size ()), name.data ());
	    return false;
	  }
	vars[ix].is_used = true;
	return true;
      }

  slot = unsigned (vars.size ());
  vars.push_back ({name, lhs, pattern, !lhs, slot});

  return true;
}

bool
Arg::parse (Lexer &lexer, Vars &vars, bool need_var, bool is_pattern)
{
  lexer.next ();
  if (!need_var && lexer.consume_expr (expr, true))
    return true;

  std::string_view ident;
  if (!lexer.consume_ident (ident))
    return false;

  if (!var.lookup (lexer, vars, ident, false, is_pattern))
    return false;

  return true;
}

bool
Shape::parse_args (Lexer &lexer, Vars &vars, unsigned &commute_mask, bool is_pattern)
{
  for (unsigned argno = 0; ; argno++)
    {
      args.emplace_back (Arg ());

      bool commutes = is_pattern && lexer.consume ('%', true);
      auto &arg = args.back ();
      if (!arg.parse (lexer, vars, commutes, is_pattern))
	return false;
      if (commutes)
	{
	  if (commute_mask & (1 << arg.var.slot))
	    {
	      lexer.error ("Var already used as commutable arg");
	      return false;
	    }
	  commute_mask |= 1 << arg.var.slot;
	  arg.commutes = true;
	}

      if (!lexer.consume (',', true))
	break;
    }
  return true;
}

bool
Shape::parse (Lexer &lexer, Vars &vars,
		std::string_view name, unsigned &commute_mask, unsigned &max_args, bool is_pattern)
{
  char const *start = nullptr;
  char const *end = nullptr;
  while (lexer.consume ('|', true))
    {
      if (!start)
	start = lexer.pos () - 1;
      std::string_view ident;
      if (!lexer.consume_ident (ident))
	return false;
      end = lexer.pos ();
    }
  modifiers = std::string_view (start, end - start);

  if (!lexer.consume ('='))
    return false;

  lexer.next ();
  if (!lexer.consume_ident (func))
    return false;

  if (!lexer.consume ('('))
    return false;

  parse_args (lexer, vars, commute_mask, is_pattern);
  if (args.size () > max_args)
    max_args = args.size ();

  if (!lexer.consume (')'))
    return false;
  if (!lhs.lookup (lexer, vars, name, true, is_pattern))
    return false;
  if (!lexer.consume (';'))
    return false;

  return true;
}

void
Shape::emit (Stream &out, std::vector<unsigned> const &remap) const
{
  out.print ("  {rvtt_insn_data::", func);
  out.print (", ", remap[lhs.slot]);
  out.print (", 0", modifiers);

  out.print (", ", unsigned (args.size ()));
  out.print (", ", used_by_mask);
  out.print (", {");
  bool first = true;
  for (auto const &arg : args)
    {
      if (!first)
	out.print (", ");
      first = false;

      out.print ("{");
      out.print (arg.is_var ());
      out.print (", ", arg.commutes);
      if (arg.is_var ())
	out.print (", ", remap[arg.var.slot]);
      else
	out.print (", ", arg.expr);
      out.print ("}");
    }
  out.print ("}},\n");
}

bool
Combine::parse_patterns (Lexer &lexer, bool is_pattern)
{
  auto &slot = is_pattern ? pats : reps;
  for (;;)
    {
      lexer.next ();
      std::string_view name;
      if (!lexer.consume_ident (name, true))
	break;

      slot.emplace_back (Shape ());
      if (!slot.back ().parse (lexer, vars, name, commute_mask, max_args, is_pattern))
	return false;
    }

  vars[slot.back ().lhs.slot].is_used = true;
  for (auto &def : vars)
    if (!def.is_used)
      {
	lexer.error ("LHS '%.*s' is never used", int (def.name.size ()), def.name.data ());
	return false;
      }

  return true;
}

bool
Combine::parse (Lexer &lexer)
{
  lineno = lexer.lineno;

  if (!lexer.consume ('('))
    return false;

  lexer.next ();
  lexer.consume_ident (target, true);
  if (!lexer.consume (')'))
    return false;

  if (!lexer.consume ('{'))
    return false;

  lexer.consume_code (hooks[H_Enable], true);
  if (!parse_patterns (lexer, true))
    return false;

  if (!lexer.consume_code (hooks[H_Pred]))
    return false;
  lexer.consume_code (hooks[H_Init], true);
  if (!parse_patterns (lexer, false))
    return false;
  lexer.consume_code (hooks[H_Fini], true);
  if (!lexer.consume ('}'))
    return false;

  // Remap Vars so that they are ordered as
  // pat/lhs, rep/lhs, pat/arg, rep/arg
  std::sort (vars.begin (), vars.end (),
	     [] (Var const &a, Var const & b) {
	       if (a.is_lhs != b.is_lhs) return a.is_lhs;
	       if (a.is_pat != b.is_pat) return a.is_pat;
	       return a.remap < b.remap;
	     });
  rep_lhs_hwm = pats.size ();
  while (rep_lhs_hwm < vars.size () && vars[rep_lhs_hwm].is_lhs)
    rep_lhs_hwm++;
  pat_var_hwm = vars.size ();
  while (pat_var_hwm && !vars[pat_var_hwm - 1].is_pat)
    pat_var_hwm--;

  remap.insert (remap.begin (), vars.size (), 0);
  for (unsigned ix = vars.size (); --ix;)
      remap[vars[ix].remap] = ix;

  if (unsigned commute = commute_mask)
    {
      commute_mask = 0;
      for (unsigned ix = vars.size (); --ix;)
	if (commute & (1 << ix))
	  commute_mask |= 1 << remap[ix];
    }

  // Compute the patterns' used_by masks
  for (unsigned ix = pats.size (); ix--;)
    {
      unsigned use = 0;
      for (unsigned jx = pats.size (); --jx != ix;)
	{
	  auto const &pat = pats[jx];
	  for (auto const &arg : pat.args)
	    if (arg.is_var ()
		&& arg.var.slot == pats[ix].lhs.slot)
	      use |= 1 << jx;
	}
      pats[ix].used_by_mask = use;
    }

  // Compute replace & rep_use masks;
  for (unsigned ix = reps.size (); ix--;)
    {
      auto &rep = reps[ix];
      auto var = remap[rep.lhs.slot];
      if (var < pats.size ())
	replace_mask |= 1 << var;

      for (auto &arg : rep.args)
	if (arg.is_var ())
	  {
	    auto use = remap[arg.var.slot];
	    if (use < pats.size ())
	      rep_use_mask |= 1 << use;
	  }
    }

  return true;
}

static bool
parse (Combines &combines, Helpers &helpers, char const *name)
{
  Lexer lexer (name);
  int handle = open (name, O_CLOEXEC);
  struct stat stat;
  char *buf;
  if (handle < 0
      || fstat (handle, &stat) < 0
      || (buf = (char *)mmap (nullptr, stat.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE,
			      handle, 0)) == MAP_FAILED)
    {
      lexer.error ("cannot read: %m");
      if (handle >= 0)
	close (handle);
      return false;
    }

  lexer.buffer (buf, stat.st_size);
  close (handle);

  for (;;)
    {
      if (lexer.consume ("combine", true))
	{
	  combines.emplace_back (Combine ());
	  if (!combines.back ().parse (lexer))
	    return false;
	}
      else if (lexer.consume ("namespace", true))
	{
	  if (!lexer.consume_code (helpers.emplace_back ()))
	    return false;

	  helpers.back ().rewind (9);
	}
      else
	break;
    }

  if (*lexer.pos ())
    {
      lexer.error ("garbage found");
      return false;
    }

  return true;
}

void
Stream::push (unsigned lineno)
{
  print ("#line ", lineno, " \"", src, "\"\n");
}

void
Stream::pop ()
{
  print ("#line ", lineno + 1, " \"", out, "\"\n");
}

void
Combine::emit_hook_name (Stream &out, Hooks hook) const
{
  static char const *const tags[H_HWM] = {"_enable", "_pred", "_init", "_fini"};
  out.print ("combiner_", lineno, tags[hook]);
}

void
Combine::emit_hook (Stream &out, Hooks hook) const
{
  out.print ("static ", hook == H_Pred || hook == H_Enable ? "bool" : "void", " ");
  emit_hook_name (out, hook);
  out.print (" (");
  if (hook != H_Enable)
    {
      out.print ("gcall *calls[], tree vars[]");
      if (hook != H_Fini)
	out.print (", unsigned mask ATTRIBUTE_UNUSED");
    }
  out.print (")\n{\n");

  if (hook != H_Enable)
    {
      auto const &slot = hook == H_Fini ? reps : pats;
      for (unsigned call = 0; call != slot.size (); call++)
	{
	  auto ix = remap[slot[call].lhs.slot];
	  out.print ("  auto &", vars[ix].name, "_call"
		     " ATTRIBUTE_UNUSED = calls[", ix, "];\n");
	}
      out.print ("\n");

      for (unsigned op = 0; op != vars.size (); op++)
	if (hook != H_Fini && op >= pats.size () && op < rep_lhs_hwm)
	  continue;
	else if (hook == H_Pred && op >= pat_var_hwm)
	  continue;
	else
	  out.print ("  auto &", vars[op].name, " ATTRIBUTE_UNUSED = vars[", op, "];\n");
      out.print ("\n");

      if (hook != H_Fini && commute_mask)
	{
	  for (unsigned mask = commute_mask, bit; mask; mask ^= 1 << bit)
	    {
	      bit = __builtin_ctz (mask);
	      out.print ("  auto ", vars[bit].name, "_commuted ATTRIBUTE_UNUSED = mask & (1 << ", bit, ");\n");
	    }
	  out.print ("\n");
	}
    }
  else if (!target.empty ())
    out.print ("  if (!combiner_enable_", target, " ())\n",
	       "    return false;\n");

  out.push (hooks[hook].lineno);
  out.print (hooks[hook].code, "\n");
  out.pop ();

  if (hook == H_Pred || hook == H_Enable)
    out.print ("  return true;\n");
  out.print ("}\n\n");
}

int
main (int argc, const char **argv)
{
  if (argc != 3)
    return 1;

  Combines combines;
  Helpers helpers;
  char const *src = argv[1];
  if (!parse (combines, helpers, src))
    return 1;

  Stream out (argv[2], src);
  out.print ("/* Gimple combiner patterns -*- mode: c++ -*-\n"
	     "   Generated automatically by ", basename (argv[0]),
	     " from ", basename (src), ".*/\n"
	     "// DO NOT EDIT\n\n");

  for (auto const &helper : helpers)
    {
      out.push (helper.lineno);
      out.print (helper.code, "\n");
      out.pop ();
      out.print ("\n");
    }

  // Emit hook functions
  unsigned max_args = 0;
  unsigned max_vars = 0;
  unsigned max_pats = 0;
  unsigned max_reps = 0;
  for (auto const &combine : combines)
    {
      if (max_args < combine.max_args)
	max_args = combine.max_args;
      if (max_vars < combine.vars.size ())
	max_vars = combine.vars.size ();
      if (max_pats < combine.pats.size ())
	max_pats = combine.pats.size ();
      if (max_reps < combine.rep_lhs_hwm)
	max_reps = combine.rep_lhs_hwm;

      for (unsigned ix = 0; ix != Combine::H_HWM; ix++)
	if (combine.hooks[ix])
	  combine.emit_hook (out, Combine::Hooks (ix));
    }

  out.print ("\n");
  out.print ("constexpr unsigned combiner_vars_hwm = ", max_vars, ";\n");
  out.print ("constexpr unsigned combiner_args_hwm = ", max_args, ";\n");
  out.print ("constexpr unsigned combiner_pats_hwm = ", max_pats, ";\n");
  out.print ("constexpr unsigned combiner_reps_hwm = ", max_reps, ";\n");
  out.print ("static_assert (args_hwm >= combiner_args_hwm, "
	     "\"increase args_hwm\");\n");
  out.print ("\n");

  // Emit shapes
  out.print ("static const Shape combiner_shapes[] = {\n");
  for (auto const &combine : combines)
    {
      if (&combine != &combines.front ())
	out.print ("\n");
      out.print ("// ", combine.lineno, "\n");
      for (auto const &pat : combine.pats)
	pat.emit (out, combine.remap);
      out.print ("\n");
      for (auto const &rep : combine.reps)
	rep.emit (out, combine.remap);
    }
  out.print ("};\n\n");

  // Emit combines
  unsigned shape_off = 0;
  out.print ("static const Combiner combiners[] = {\n");
  for (auto const &combine : combines)
    {
      out.print ("  {&combiner_shapes[", shape_off, "]",
		 ", ", combine.pats.size (),
		 ", ", combine.pats.size () + combine.reps.size (),
		 ", ", combine.rep_lhs_hwm,
		 ", ", combine.pat_var_hwm,
		 ", ", combine.vars.size (),
		 ", ", combine.replace_mask,
		 ", ", combine.rep_use_mask,
		 ", ", combine.lineno);
      for (unsigned ix = 0; ix != Combine::H_HWM; ix++)
	{
	  out.print (", ");
	  if (combine.hooks[ix])
	    combine.emit_hook_name (out, Combine::Hooks (ix));
	  else if (ix == Combine::H_Enable && !combine.target.empty ())
	    out.print ("combiner_enable_", combine.target);
	  else
	    out.print ("nullptr");
	}
      out.print ("},\n");

      shape_off += combine.pats.size () + combine.reps.size ();
    }
  out.print ("};\n");

  return 0;
}
