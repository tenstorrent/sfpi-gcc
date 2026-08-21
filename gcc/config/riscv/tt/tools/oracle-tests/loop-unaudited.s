# RecMII fixture 3: a body member with UNAUDITED latency (latency=-1).
# The oracle must keep every count a lower bound and refuse exactness by
# name: RecMII/MII/achieved-II all print with ">=" and the REFUSED-EXACT
# line lists the class.
kernel_unaudited:
.L4:
	sfpmad	L1, L0, L7, L1
	# xtt-effects: subunit=mad latency=1 lreg-read=0x1 lreg-write=0x2 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpshft2	L2, L1
	# xtt-effects: subunit=shft2 latency=-1 lreg-read=0x2 lreg-write=0x4 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	sfpmad	L0, L2, L7, L0
	# xtt-effects: subunit=mad latency=1 lreg-read=0x4 lreg-write=0x1 port=a cc=none config=0x0 rwc=none dst=none encodable=yes
	addi	a5,a5,-1
	bne	a5,zero,.L4
	ret
