; Test eZ80 JR wrap-around in ADL mode
    .ez80
    .adl

    org 0xFFFFFE
start:
    jr  target      ; JR to 0x000003 from 0xFFFFFE. PC+2 = 0x000000. Offset should be 3.
    nop             ; 0x000000
    nop             ; 0x000001
    nop             ; 0x000002
target:
    jr  start       ; JR to 0xFFFFFE from 0x000003. PC+2 = 0x000005. Offset should be -7.
    
    end
