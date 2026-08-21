# RecMII fixture 1: pure 3-node recurrence circuit, latency 1 each.
# Circuit n1 -> n2 -> n3 -> (wrap) n1: delay (1+1)*3 = 6, distance 1.
# Expected: ResMII=3 RecMII=6 MII=6 achieved-II=6 stall/iter=0.
kernel_recurrence:
.L2:
	sfpmad	L2, L0, L1, L2
	# xtt-effects: subunit=mad latency=1 lreg-read=0x3 lreg-write=0x4 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L4, L2, L3, L4
	# xtt-effects: subunit=mad latency=1 lreg-read=0xc lreg-write=0x10 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L0, L4, L5, L0
	# xtt-effects: subunit=mad latency=1 lreg-read=0x30 lreg-write=0x1 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	addi	a5,a5,-1
	bne	a5,zero,.L2
	ret
