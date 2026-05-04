; Test Z80 JR wrap-around in 16-bit mode
    .z80

    org 0xFFFE
start:
    jr  target      ; JR to 0x0003 from 0xFFFE. PC+2 = 0x0000. Offset should be 3.
    nop             ; 0x0000
    nop             ; 0x0001
    nop             ; 0x0002
target:
    jr  start       ; JR to 0xFFFE from 0x0003. PC+2 = 0x0005. Offset should be -7 (0xF9).
    
    end
