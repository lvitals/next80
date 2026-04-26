# Intel 8080 CPU Support — Reference Guide

This document provides a comprehensive overview of the Intel 8080 support in `next80`. The 8080 is the direct ancestor of the Z80; while the Z80 is binary-compatible with the 8080, it uses a different assembly syntax (mnemonics). `next80` supports the original 8080 mnemonics for full compatibility with legacy CP/M-era source code.

---

## 1. Enabling 8080 Mode

To assemble code using Intel 8080 mnemonics, you must explicitly switch the CPU mode. This can be done in two ways:

### The `.8080` Directive
The most common way, compatible with Microsoft MACRO-80:
```asm
    .8080       ; Switch to 8080 mnemonics
```

### The `.cpu` Directive
A more modern approach:
```asm
    .cpu 8080   ; Switch to 8080 mnemonics
```

---

## 2. Special Registers and Tokens

In 8080 mode, `next80` recognizes specific tokens that have different meanings or syntax compared to Z80 mode:

| Token | Meaning | Notes |
|-------|---------|-------|
| `M`   | `(HL)` | The "Memory" pseudo-register. Used in `MOV`, `MVI`, `INR`, `DCR`, `ANA`, `ORA`, `XRA`, `CMP`. |
| `PSW` | `AF`   | "Program Status Word". Used exclusively in `PUSH` and `POP`. |
| `RST n`| Vector | Operands are vectors `0-7` (encoded as `0xC7 | (n << 3)`). |

---

## 3. Mnemonic Conflict Resolution

Several mnemonics appear in both Z80 and 8080 syntaxes but represent **entirely different instructions**. `next80` automatically branches its logic based on the `current_cpu` state:

| Mnemonic | Z80 Meaning | 8080 Meaning | Opcode (8080) |
|----------|-------------|--------------|---------------|
| `JP nn`  | Unconditional Jump | Jump if Positive (Flag S=0) | `0xF2` |
| `CP nn`  | Compare A with nn | Call if Positive (Flag S=0) | `0xF4` |
| `RLC`    | Rotate Left Circular | Rotate Accumulator Left (`RLCA`) | `0x07` |
| `RRC`    | Rotate Right Circular | Rotate Accumulator Right (`RRCA`) | `0x0F` |
| `ADD r`  | `ADD A,r` (explicit A) | `ADD r` (A is implicit) | `0x80-0x87` |
| `IN n`   | `IN A,(n)` | `IN A,(n)` only | `0xDB` |
| `OUT n`  | `OUT (n),A` | `OUT (n),A` only | `0xD3` |

> **Note:** For an unconditional jump in 8080 mode, use `JMP`. For an unconditional call, use `CALL`.

---

## 4. Instruction Set Reference

### Data Transfer
| Mnemonic | Opcode | Description |
|----------|--------|-------------|
| `MOV r1,r2` | `01 DDD SSS` | Move register to register (M=110) |
| `MVI r,n`   | `00 DDD 110` | Move immediate byte to register |
| `LXI rp,nn` | `00 RP 0001` | Load register pair immediate (B, D, H, SP) |
| `LDA nn`    | `3A` | Load Accumulator direct from address |
| `STA nn`    | `32` | Store Accumulator direct to address |
| `LHLD nn`   | `2A` | Load HL direct from address |
| `SHLD nn`   | `22` | Store HL direct to address |
| `LDAX rp`   | `0A/1A` | Load A indirect via BC or DE |
| `STAX rp`   | `02/12` | Store A indirect via BC or DE |
| `XCHG`      | `EB` | Exchange DE with HL |

### Arithmetic & Logical
| Mnemonic | Opcode | Description |
|----------|--------|-------------|
| `ADD r`  | `80-87` | Add register to A |
| `ADI n`  | `C6` | Add immediate to A |
| `ADC r`  | `88-8F` | Add register to A with Carry |
| `ACI n`  | `CE` | Add immediate to A with Carry |
| `SUB r`  | `90-97` | Subtract register from A |
| `SUI n`  | `D6` | Subtract immediate from A |
| `SBB r`  | `98-9F` | Subtract register from A with Borrow |
| `SBI n`  | `DE` | Subtract immediate from A with Borrow |
| `INR r`  | `00 DDD 100` | Increment register or M |
| `DCR r`  | `00 DDD 101` | Decrement register or M |
| `INX rp` | `03/13...` | Increment register pair |
| `DCX rp` | `0B/1B...` | Decrement register pair |
| `DAD rp` | `09/19...` | Add register pair to HL |
| `ANA r`  | `A0-A7` | AND register with A |
| `ANI n`  | `E6` | AND immediate with A |
| `ORA r`  | `B0-B7` | OR register with A |
| `ORI n`  | `F6` | OR immediate with A |
| `XRA r`  | `A8-AF` | XOR register with A |
| `XRI n`  | `EE` | XOR immediate with A |
| `CMP r`  | `B8-BF` | Compare register with A |
| `CPI n`  | `FE` | Compare immediate with A |

### Branching (Conditional)
The 8080 uses specific mnemonics for conditional Jumps, Calls, and Returns:

| Condition | Jump | Call | Return |
|-----------|------|------|--------|
| Zero (Z=1) | `JZ` | `CZ` | `RZ` |
| Not Zero (Z=0) | `JNZ` | `CNZ` | `RNZ` |
| Carry (C=1) | `JC` | `CC` | `RC` |
| No Carry (C=0) | `JNC` | `CNC` | `RNC` |
| Positive (S=0) | `JP` | `CP` | `RP` |
| Minus (S=1) | `JM` | `CM` | `RM` |
| Parity Even (P=1) | `JPE` | `CPE` | `RPE` |
| Parity Odd (P=0) | `JPO` | `CPO` | `RPO` |

### Control & Miscellaneous
| Mnemonic | Opcode | Z80 Equivalent |
|----------|--------|----------------|
| `CMA`    | `2F`   | `CPL` |
| `CMC`    | `3F`   | `CCF` |
| `STC`    | `37`   | `SCF` |
| `RAL`    | `17`   | `RLA` |
| `RAR`    | `1F`   | `RRA` |
| `PCHL`   | `E9`   | `JP (HL)` |
| `SPHL`   | `F9`   | `LD SP,HL` |
| `XTHL`   | `E3`   | `EX (SP),HL` |
| `HLT`    | `76`   | `HALT` |

---

## 5. Specific Implementation Details

### Register Encoding
The 8080 uses a uniform 3-bit encoding for registers in opcodes:
`B=000, C=001, D=010, E=011, H=100, L=101, M=110, A=111`

### Register Pair Encoding
For instructions like `LXI`, `DAD`, `PUSH`, `POP`:
`BC=00, DE=01, HL=10, SP/PSW=11`

### `MOV M,M` vs `HLT`
In the 8080 architecture, the opcode `0x76` is defined as `HLT`. Following the `MOV dst,src` encoding rule (`01 DDD SSS`), `MOV M,M` would result in `01 110 110`, which is exactly `0x76`. `next80` supports `MOV M,M` as a valid alias for `HLT` for compatibility with specific legacy coding styles.

### `RST` Semantics
While the Z80 allows specifying the absolute address (e.g., `RST 38h`), the 8080 traditionally uses the vector number. `next80` is flexible and accepts both:
- `RST 7` (Vector 7 → 0x38)
- `RST 38h` (Absolute address → 0x38)
Both will result in the opcode `0xFF`.
