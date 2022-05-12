#define INCLUDE_STRING
#include <map>
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "regs.h"
#include "insn-config.h"
#include "insn-attr.h"
#include "recog.h"
#include "output.h"
#include "alias.h"
#include "stringpool.h"
#include "attribs.h"
#include "varasm.h"
#include "stor-layout.h"
#include "calls.h"
#include "function.h"
#include "explow.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "reload.h"
#include "tm_p.h"
#include "target.h"
#include "target-def.h"
#include "basic-block.h"
#include "expr.h"
#include "optabs.h"
#include "bitmap.h"
#include "df.h"
#include "diagnostic.h"
#include "builtins.h"
#include "predict.h"
#include "tree-pass.h"
#include "ssa.h"
#include "tree-ssa.h"
#include "sfpu-protos.h"
#include "sfpu.h"

struct cmp_str
{
  bool operator()(const char *a, const char *b) const
  {
     return std::strcmp(a, b) < 0;
  }
};

unsigned int riscv_sfpu_cmp_ex_to_setcc_mod1_map[] = {
  0,
  SFPSETCC_MOD1_LREG_LT0,
  SFPSETCC_MOD1_LREG_EQ0,
  SFPSETCC_MOD1_LREG_GTE0,
  SFPSETCC_MOD1_LREG_NE0,
};

static std::map<const char*, riscv_sfpu_insn_data&, cmp_str> insn_map;
static const int NUMBER_OF_ARCHES = 2;
static const int NUMBER_OF_INTRINSICS = 73;
static riscv_sfpu_insn_data sfpu_insn_data_target[NUMBER_OF_ARCHES][NUMBER_OF_INTRINSICS] = {
  {
#define SFPU_GS_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) { riscv_sfpu_insn_data::id, #id, nullptr, cc, lv, hho, dap, mp },
#define SFPU_GS_NO_TGT_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) { riscv_sfpu_insn_data::id, #id, nullptr, cc, lv, hho, dap, mp },
#define SFPU_GS_PAD_BUILTIN(id) { riscv_sfpu_insn_data::id, #id, nullptr, 0, 0, 0, 0, 0 },
#define SFPU_GS_PAD_NO_TGT_BUILTIN(id) { riscv_sfpu_insn_data::id, #id, nullptr, 0, 0, 0, 0, 0},
#include "sfpu-insn.h"
    { riscv_sfpu_insn_data::nonsfpu, "nonsfpu", nullptr, 0, 0, 0, 0, 0 }
  },
  {
#define SFPU_WH_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) { riscv_sfpu_insn_data::id, #id, nullptr, cc, lv, hho, dap, mp },
#define SFPU_WH_NO_TGT_BUILTIN(id, fmt, en, cc, lv, hho, dap, mp) { riscv_sfpu_insn_data::id, #id, nullptr, cc, lv, hho, dap, mp },
#include "sfpu-insn.h"
    { riscv_sfpu_insn_data::nonsfpu, "nonsfpu", nullptr, 0, 0, 0, 0, 0 }
  }
};

static riscv_sfpu_insn_data *sfpu_insn_data = sfpu_insn_data_target[0];
static const char* riscv_sfpu_builtin_name_stub;
void
riscv_sfpu_insert_insn(int idx, const char* name, tree decl)
{
  // Due to PADding, there will be gaps in the builtin table, so the name we
  // are looking for occurs after idx.  Assume that no ARCH is so small that
  // the math below doesn't work.

  int arch = idx / NUMBER_OF_INTRINSICS;
  int offset = idx % NUMBER_OF_INTRINSICS;

  while (offset < NUMBER_OF_INTRINSICS * NUMBER_OF_ARCHES)
    {
      // string is __rvtt_builtin_XX_<name>
      if (strcmp(sfpu_insn_data_target[arch][offset].name, &name[18]) == 0)
	{
	  sfpu_insn_data_target[arch][offset].decl = decl;
	  insn_map.insert(std::pair<const char*, riscv_sfpu_insn_data&>(name, sfpu_insn_data_target[arch][offset]));
	  return;
	}
      offset++;
      if (offset == NUMBER_OF_INTRINSICS)
	{
	  offset = 0;
	  arch++;
	}
    }

  fprintf(stderr, "Failed to match insn %d named %s to builtin for arch index %d starting at builtin %d\n",
	  idx, name, arch, offset);
  gcc_assert(0);
}

void
riscv_sfpu_init_builtins()
{
  if (flag_grayskull)
    {
      riscv_sfpu_builtin_name_stub = "__builtin_rvtt_gs";
      sfpu_insn_data = sfpu_insn_data_target[0];
    }
  if (flag_wormhole)
    {
      riscv_sfpu_builtin_name_stub = "__builtin_rvtt_wh";
      sfpu_insn_data = sfpu_insn_data_target[1];
    }

  // If these asserts fire, the sfpu-insn.h instruction tables are out of sync
  // across architectures
  for (int i = 1; i < NUMBER_OF_ARCHES; i++)
    {
      for (int j = 0; j < NUMBER_OF_INTRINSICS; j++)
	{
	  if (sfpu_insn_data_target[0][j].id != sfpu_insn_data_target[i][j].id)
	    {
	      fprintf(stderr, "SFPU intrinsic table element (%d, %d) does not match (%d != %d)!\n",
		      i, j, sfpu_insn_data_target[0][j].id, sfpu_insn_data_target[i][j].id);
	      gcc_assert(0);
	    }
	}

      gcc_assert(sfpu_insn_data_target[i][NUMBER_OF_INTRINSICS - 1].id == riscv_sfpu_insn_data::nonsfpu);
    }
}

const char *
riscv_sfpu_get_builtin_name_stub()
{
  return riscv_sfpu_builtin_name_stub;
}

const riscv_sfpu_insn_data*
riscv_sfpu_get_insn_data(const riscv_sfpu_insn_data::insn_id id)
{
  return &sfpu_insn_data[id];
}

const riscv_sfpu_insn_data*
riscv_sfpu_get_insn_data(const char *name)
{
  auto match = insn_map.find(name);
  if (match == insn_map.end())
    {
      return &sfpu_insn_data[riscv_sfpu_insn_data::nonsfpu];
    }
  else
    {
      return &match->second;
    }
}

const riscv_sfpu_insn_data *
riscv_sfpu_get_insn_data(const gcall *stmt)
{
  tree fn_ptr = gimple_call_fn (stmt);

  if (fn_ptr)
    {
      return riscv_sfpu_get_insn_data(IDENTIFIER_POINTER (DECL_NAME (TREE_OPERAND (fn_ptr, 0))));
    }
  else
    {
      return nullptr;
    }
}

bool
riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple *g)
{
  bool found = false;

  if (g == nullptr || g->code != GIMPLE_CALL) return false;

  *stmt = dyn_cast<gcall *> (g);
  tree fn_ptr = gimple_call_fn (*stmt);

  if (fn_ptr && TREE_CODE (fn_ptr) == ADDR_EXPR)
    {
      tree fn_decl = TREE_OPERAND (fn_ptr, 0);
      *insnd = riscv_sfpu_get_insn_data(IDENTIFIER_POINTER (DECL_NAME (fn_decl)));
      found = (*insnd != nullptr && (*insnd)->id != riscv_sfpu_insn_data::nonsfpu);
    }

  return found;
}

bool
riscv_sfpu_p(const riscv_sfpu_insn_data **insnd, gcall **stmt, gimple_stmt_iterator gsi)
{
  bool found = false;
  gimple *g = gsi_stmt (gsi);

  if (g != nullptr && g->code == GIMPLE_CALL)
    {
      found = riscv_sfpu_p(insnd, stmt, g);
    }

  return found;
}

// Relies on live instructions being next in sequence in the insn table
const riscv_sfpu_insn_data *
riscv_sfpu_get_live_version(const riscv_sfpu_insn_data *insnd)
{
  const riscv_sfpu_insn_data *out = nullptr;

  if (insnd->id < riscv_sfpu_insn_data::nonsfpu)
    {
      if (sfpu_insn_data[insnd->id + 1].live)
	{
	  out = &sfpu_insn_data[insnd->id + 1];
	}
    }

  return out;
}

const riscv_sfpu_insn_data *
riscv_sfpu_get_notlive_version(const riscv_sfpu_insn_data *insnd)
{
  const riscv_sfpu_insn_data *out = insnd;

  if (insnd->live)
    {
      out = &sfpu_insn_data[insnd->id - 1];
    }

  return out;
}

static long int
get_int_arg(gcall *stmt, unsigned int arg)
{
  tree decl = gimple_call_arg(stmt, arg);
  if (decl)
  {
    gcc_assert(TREE_CODE(decl) == INTEGER_CST);
    return *(decl->int_cst.val);
  }
  return -1;
}

bool
riscv_sfpu_sets_cc(const riscv_sfpu_insn_data *insnd, gcall *stmt)
{
  bool sets_cc = false;

  if (insnd->can_set_cc)
    {
      long int arg = (insnd->mod_pos != -1) ? get_int_arg (stmt, insnd->mod_pos) : 0;
      if (insnd->id == riscv_sfpu_insn_data::sfpiadd_i)
	{
	  if (arg == 0 || arg == 1 || arg == 2 || arg == 8 || arg == 9 || arg == 10 || arg == 12 || arg == 13 || arg == 14)
	    sets_cc = true;
	}
      else if (insnd->id == riscv_sfpu_insn_data::sfpiadd_i_ex)
	{
	  if (arg & SFPCMP_EX_MOD1_CC_MASK)
	    sets_cc = true;
	}
      else if (insnd->id == riscv_sfpu_insn_data::sfpiadd_v)
	{
	  if (arg == 0 || arg == 1 || arg == 2 || arg == 8 || arg == 9 || arg == 10 || arg == 12 || arg == 13 || arg == 14)
	    sets_cc = true;
	}
      else if (insnd->id == riscv_sfpu_insn_data::sfpiadd_v_ex)
	{
	  if (arg & SFPCMP_EX_MOD1_CC_MASK)
	    sets_cc = true;
	}
      else if (insnd->id == riscv_sfpu_insn_data::sfpexexp)
	{
	  if (arg == 2 || arg == 3 || arg == 8 || arg == 9 || arg == 10 || arg == 11)
	    sets_cc = true;
	}
      else if (insnd->id == riscv_sfpu_insn_data::sfplz)
	{
	  if (arg == 2 || arg == 8 || arg == 10)
	    sets_cc = true;
	}
      else
	{
	  sets_cc = true;
	}
    }

  return sets_cc;
}

bool riscv_sfpu_permutable_operands(const riscv_sfpu_insn_data *insnd, gcall *stmt)
{
  return
      insnd->id == riscv_sfpu_insn_data::sfpand ||

      insnd->id == riscv_sfpu_insn_data::sfpor ||

      (insnd->id == riscv_sfpu_insn_data::sfpiadd_v &&
       (get_int_arg (stmt, 2) & SFPIADD_MOD1_ARG_2SCOMP_LREG_DST) == 0) ||

      (insnd->id == riscv_sfpu_insn_data::sfpiadd_v_ex &&
       (get_int_arg (stmt, 2) & SFPIADD_EX_MOD1_IS_SUB) == 0);
}


rtx riscv_sfpu_clamp_signed(rtx v, unsigned int mask)
{
  int i = INTVAL(v);
  int out = i & mask;

  if (i & (mask + 1)) {
    out |= ~mask;
  }

  return GEN_INT(out);
}

rtx riscv_sfpu_clamp_unsigned(rtx v, unsigned int mask)
{
  int i = INTVAL(v);
  int out = i & mask;

  return GEN_INT(out);
}

rtx riscv_sfpu_gen_const0_vector()
{
    int i;
    rtx vec[64];

    for (i = 0; i < 64; i++) {
      vec[i] = const_double_from_real_value(dconst0, SFmode);
    }

    return gen_rtx_CONST_VECTOR(V64SFmode, gen_rtvec_v(64, vec));
}

const char* riscv_sfpu_lv_regno_str(char *str, rtx operand)
{
  if (GET_CODE(operand) == CONST_VECTOR) {
    sprintf(str, "-");
  } else {
    sprintf(str, "lv(%d) ", REGNO(operand) - SFPU_REG_FIRST);
  }

  return str;
}

// If a stmt's single use args aren't tracked back to their
// defs and deleted prior to deleting the stmt, errors occur w/
// flag_checking=1
// There has to be an internal version of this...
void riscv_sfpu_prep_stmt_for_deletion(gimple *stmt)
{
  for (unsigned int i = 0; i < gimple_call_num_args (stmt); i++)
    {
      tree arg = gimple_call_arg(stmt, i);

      if (TREE_CODE(arg) == SSA_NAME && num_imm_uses (arg) == 1)
	{
	  gimple *def_g = SSA_NAME_DEF_STMT (arg);

	  if (def_g->code == GIMPLE_PHI)
	    {
	      // XXXX handle phi
	      // this seems to work fine and SSA checks are ok w/ doing nothing
	    }
	  else if (def_g->code == GIMPLE_CALL)
	    {
	      tree lhs_name = gimple_call_lhs (def_g);
	      gimple_call_set_lhs(def_g, NULL_TREE);
	      release_ssa_name(lhs_name);
	      update_stmt (def_g);
	    }
	  else if (def_g->code == GIMPLE_ASSIGN)
	    {
	      unlink_stmt_vdef(def_g);
	      gimple_stmt_iterator gsi = gsi_for_stmt(def_g);
	      gsi_remove(&gsi, true);
	      release_defs(def_g);
	    }
	}
    }
}

void riscv_sfpu_emit_sfpassignlr(rtx dst, rtx lr)
{
  int lregnum = INTVAL(lr);
  SET_REGNO(dst, SFPU_REG_FIRST + lregnum);
  emit_insn(gen_riscv_sfpassignlr_int(dst));
}

void riscv_sfpu_emit_nonimm_dst(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx imm,
				int base, int lshft, int rshft, int dst_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_dst(dst, buf_addr, GEN_INT(nnops), dst_lv, GEN_INT(base), GEN_INT(dst_shft), insn));
}

void riscv_sfpu_emit_nonimm_dst_src(rtx buf_addr, rtx dst, int nnops, rtx dst_lv, rtx src, rtx imm, int base, int lshft, int rshft, int dst_shft, int src_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_dst_src(dst, buf_addr, GEN_INT(nnops), dst_lv, src, GEN_INT(base), GEN_INT(dst_shft), GEN_INT(src_shft), insn));
}

void riscv_sfpu_emit_nonimm_src(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft)
{
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_src(src, buf_addr, GEN_INT(nnops), GEN_INT(base), GEN_INT(src_shft), insn));
}

void riscv_sfpu_emit_nonimm_store(rtx buf_addr, rtx src, int nnops, rtx imm, int base, int lshft, int rshft, int src_shft)
{
    // This is exactly like _src, exists so peephole can handle the store-to-load nop
    rtx insn = gen_reg_rtx(SImode);
    emit_insn(gen_ashlsi3(insn, imm, GEN_INT(lshft)));
    emit_insn(gen_lshrsi3(insn, insn, GEN_INT(rshft)));
    emit_insn(gen_riscv_sfpnonimm_store(src, buf_addr, GEN_INT(nnops), GEN_INT(base), GEN_INT(src_shft), insn));
}

char const * riscv_sfpu_output_nonimm_store_and_nops(const char *sw, int nnops, rtx operands[])
{
  char const *out = sw;
  while (nnops-- > 0) {
     output_asm_insn(out, operands);
     out = "SFPNOP";
  }
  return out;
}
