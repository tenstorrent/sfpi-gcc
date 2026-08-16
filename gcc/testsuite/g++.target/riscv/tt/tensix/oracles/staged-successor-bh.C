
void same_row_successor ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
  __builtin_rvtt_ttincrwc (0, 2, 0, 0);
  auto successor = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, successor, 0, 0, 0, 0, 7);
}

/*
**_Z18same_row_successorv:
**	SFPENCC	3, 10
**	SFPLOADI	L0, 4294, 2
**	SFPLOADI	L0, 38142, 8	# LV:L0
**	SFPCONFIG	0, 0, 0	# R:L0 CFG:0
**	SFPLOADI	L0, 208, 2
**	SFPLOADI	L0, 36864, 8	# LV:L0
**	SFPCONFIG	1, 0, 0	# R:L0 CFG:1
**	SFPLOADI	L0, 77, 2
**	SFPLOADI	L0, 21380, 8	# LV:L0
**	SFPCONFIG	4, 0, 0	# R:L0 CFG:4
**	SFPLOADI	L0, 272, 4
**	SFPCONFIG	8, 0, 0	# R:L0 CFG:8
**	SFPNOP
**	SFPNOP
**	SFPNOP
**	SFPLOAD	L0, 0, 0, 7
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/
