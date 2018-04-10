
 #include "inc/hw_memmap.h"
 #include "inc/hw_timer.h"
	name isr_asm
	section .text:CODE
	extern countsPerSecond
	public isr_asm_start
isr_asm_start:
	push {lr} ;
	; Calling Timer_Int_Clear
	LRD r0, TIMER1_BASE
	MOVS r1, #1
	STR r1, [r0, TIMER_O_ICR]
	
	;countsPerSecond++
	MOV32 r0, countsPerSecond
	LDR r1, [r0]
	ADDS r1, r1, #1
	STR r1, [r0]
	
	pop {pc} ;return
	end