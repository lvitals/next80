	.cpu ez80
	.adl 1
	aseg
	org 0x100000
start:
	nop
	jr start
	
	cseg
	org 0x1000
start2:
	nop
	jr start2
