// Sort-network library compile gate (lane FG, X5): the generated
// bitonic networks allocate within the 8-register file -- value-8
// (register axis), value-32 (column machines, transp8 sandwiches),
// KV-16 (indexed swaps, companions pinned L4..L7, caller-owned
// ENABLE_DEST_INDEX window).  Static instruction counts are the
// network sizes (replay formation disabled to keep them literal).
// { dg-options "-mcpu=tt-bh-tensix -O2 -I [SFPI]/include -fno-exceptions -fno-rtti -mtt-tensix-optimize-crosslane -mno-tt-tensix-optimize-replay -fdump-tree-rvtt_crosslane" }

namespace ckernel { unsigned *instrn_buffer; }
#include <sfpi.h>
using namespace sfpi;

void net8_asc ()
{
  vFloat v[8] = { dst_reg[0], dst_reg[2], dst_reg[4], dst_reg[6],
		  dst_reg[8], dst_reg[10], dst_reg[12], dst_reg[14] };
  bitonic_sort8<SortOrder::Ascending> (v);
  dst_reg[16] = v[0]; dst_reg[18] = v[1]; dst_reg[20] = v[2];
  dst_reg[22] = v[3]; dst_reg[24] = v[4]; dst_reg[26] = v[5];
  dst_reg[28] = v[6]; dst_reg[30] = v[7];
}

void net32_desc ()
{
  vFloat v[8] = { dst_reg[0], dst_reg[2], dst_reg[4], dst_reg[6],
		  dst_reg[8], dst_reg[10], dst_reg[12], dst_reg[14] };
  bitonic_sort32<SortOrder::Descending> (v);
  dst_reg[16] = v[0]; dst_reg[18] = v[1]; dst_reg[20] = v[2];
  dst_reg[22] = v[3]; dst_reg[24] = v[4]; dst_reg[26] = v[5];
  dst_reg[28] = v[6]; dst_reg[30] = v[7];
}

void net16kv_asc ()
{
  vFloat k[4] = { dst_reg[0], dst_reg[2], dst_reg[4], dst_reg[6] };
  vUInt p[4] = { dst_reg[8], dst_reg[10], dst_reg[12], dst_reg[14] };
  set_dest_index_window<true> ();
  bitonic_sort16_kv<SortOrder::Ascending> (k, p);
  set_dest_index_window<false> ();
  dst_reg[16] = k[0]; dst_reg[18] = k[1]; dst_reg[20] = k[2];
  dst_reg[22] = k[3];
  dst_reg[24] = p[0]; dst_reg[26] = p[1]; dst_reg[28] = p[2];
  dst_reg[30] = p[3];
}

// 24 + 60 + 20 compare-exchanges, 0 + 4 + 4 transposes; the window
// checker accepts the KV network's window content (SWAP/TRANSP only).
// { dg-final { scan-assembler-times {SFPSWAP} 104 } }
// { dg-final { scan-assembler-times {SFPTRANSP} 8 } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 4, 1} } }
// { dg-final { scan-assembler {SFPCONFIG\t15, 0, 1} } }
// No spills: the networks fit the file.
// { dg-final { scan-assembler-not {lreg-pressure} } }
