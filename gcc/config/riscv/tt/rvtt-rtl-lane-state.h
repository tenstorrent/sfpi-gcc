/* Independent RTL must-facts for column flags and LaneConfig ROW_MASK.
   Copyright (C) 2026 Tenstorrent Inc.
   This file is part of GCC, distributed under the GNU General Public
   License version 3 or later.  See COPYING3.  */
#ifndef RVTT_RTL_LANE_STATE_H
#define RVTT_RTL_LANE_STATE_H

enum rvtt_rtl_lane_effect
{
  LANES_KEPT, LANES_KEEP_ALL, LANES_ALL, LANES_FLAGS_ANY,
  LANES_ROW_MASK_CLEAR, LANES_ANY
};
enum
{
  RVTT_RTL_FLAGS_ALL = 1, RVTT_RTL_ROW_MASK_CLEAR = 2,
  RVTT_RTL_ALL_LANES = 3
};

extern rvtt_rtl_lane_effect rvtt_rtl_lane_effect_of (rtx_insn *);
extern bool rvtt_rtl_raw_insn_fields (rtx_insn *, uint32_t *);
extern unsigned rvtt_rtl_lanes_step (rtx_insn *, unsigned, bool);

/* A snapshot: callers must rebuild after mutating lane-affecting RTL.
   No function-entry ABI axiom is assumed.  Joins intersect both facts.  */
class rvtt_rtl_lane_facts
{
  friend class rvtt_rtl_lane_snapshot_scope;
  function *m_fn;
  const rvtt_rtl_lane_facts *m_original;
  auto_vec<unsigned> m_before, m_after, m_in, m_out;
public:
  explicit rvtt_rtl_lane_facts (function *);
  unsigned before (rtx_insn *i) const
  { if (m_original) return m_original->before (i);
    return unsigned (INSN_UID (i)) < m_before.length ()
      ? m_before[INSN_UID (i)] : 0; }
  unsigned after (rtx_insn *i) const
  { if (m_original) return m_original->after (i);
    return unsigned (INSN_UID (i)) < m_after.length ()
      ? m_after[INSN_UID (i)] : 0; }
  unsigned block_in (basic_block b) const
  { if (m_original) return m_original->block_in (b);
    return unsigned (b->index) < m_in.length () ? m_in[b->index] : 0; }
  unsigned block_out (basic_block b) const
  { if (m_original) return m_original->block_out (b);
    return unsigned (b->index) < m_out.length () ? m_out[b->index] : 0; }
};

/* Reuse original-point facts only within a transformation that proves
   preservation of lane state.  New instruction/block IDs stay unknown;
   this is not a cache for arbitrary mutating passes.  */
class rvtt_rtl_lane_snapshot_scope
{
  const rvtt_rtl_lane_facts *m_saved;
public:
  explicit rvtt_rtl_lane_snapshot_scope (const rvtt_rtl_lane_facts &);
  ~rvtt_rtl_lane_snapshot_scope ();
};

#endif
