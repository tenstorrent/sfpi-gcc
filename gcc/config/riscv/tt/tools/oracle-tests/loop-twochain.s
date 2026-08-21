# RecMII fixture 2: two independent 3-node recurrence circuits emitted
# serially (P chain then Q chain).  Each circuit: delay 6, distance 1
# => RecMII=6.  ResMII=6 (six words).  As emitted the chains do not
# interleave, so the wrap dependence of each chain gates the next
# iteration only after both chains drained: achieved-II=10, leaving
# round-chain-stall/iter = 10 - 6 = 4 on the table (the round-chain
# interleave class this oracle extension quantifies).
kernel_twochain:
.L3:
	sfpmad	L1, L0, L7, L1
	# xtt-effects: subunit=mad latency=1 lreg-read=0x1 lreg-write=0x2 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L2, L1, L7, L2
	# xtt-effects: subunit=mad latency=1 lreg-read=0x2 lreg-write=0x4 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L0, L2, L7, L0
	# xtt-effects: subunit=mad latency=1 lreg-read=0x4 lreg-write=0x1 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L5, L4, L7, L5
	# xtt-effects: subunit=mad latency=1 lreg-read=0x10 lreg-write=0x20 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L6, L5, L7, L6
	# xtt-effects: subunit=mad latency=1 lreg-read=0x20 lreg-write=0x40 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L4, L6, L7, L4
	# xtt-effects: subunit=mad latency=1 lreg-read=0x40 lreg-write=0x10 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	addi	a5,a5,-1
	bne	a5,zero,.L3
	ret
