; Vertical blank interrupt server: counts video frames.

	xdef	_VblServer
	xdef	_vbl_count

	section	code,code

_VblServer:
	addq.l	#1,_vbl_count
	moveq	#0,d0		; Z=1: the server chain carries on
	rts

	section	bss,bss

_vbl_count:
	ds.l	1
