# eZ80 Processor Support in next80

The **next80** toolchain provides comprehensive support for the Zilog eZ80 processor, including its 24-bit ADL (Address and Data Long) mode.

## Enabling eZ80 Support

By default, **n80** targets the Z80 CPU. To enable eZ80 instructions and features, use the `.ez80` or `.adl` directives.

### Directives

* `.ez80`: Sets the target CPU to eZ80 but remains in the current ADL mode (defaults to Z80 mode/SIS if not specified).
* `.adl [val]`: Sets the target CPU to eZ80 and toggles ADL mode. If `val` is non-zero (or omitted), ADL mode (24-bit) is enabled. If `val` is zero, Z80 mode (16-bit) is enabled.
* `.z80`: Sets the target CPU back to Z80 and disables ADL mode.

## ADL Mode vs. Z80 Mode

The eZ80 has two primary operating modes:

1.  **Z80 Mode (SIS - Short Instruction, Short Data):**
    *   Addresses are 16-bit.
    *   Stack is 16-bit.
    *   Default behavior is compatible with the standard Z80.
2.  **ADL Mode (LIL - Long Instruction, Long Data):**
    *   Addresses are 24-bit.
    *   Stack is 24-bit (SPL).
    *   Registers like `HL`, `BC`, `DE`, `IX`, `IY` are 24-bit.

## Instruction Suffixes

You can override the current ADL mode for a single instruction using suffixes. **n80** supports the following:

*   `.s`: SIS (Short Instruction, Short Data).
*   `.l`: LIL (Long Instruction, Long Data).
*   `.is`: LIS (Long Instruction, Short Data).
*   `.il`: SIL (Short Instruction, Long Data).

Example:
```asm
    .adl 1      ; Enable ADL mode (24-bit)
    ld hl, 123456h
    ld.s hl, 1234h ; Force 16-bit load even in ADL mode
```

## Relocatable Output and Linking

When assembling for eZ80 in ADL mode, the generated `.REL` files (Microsoft REL or SDCC XL3/XL4) use extensions to support 24-bit addresses.

### Microsoft REL Extensions
*   A new control item `0x44` (EXT_SET_ADL) is used to inform the linker when to switch between 16-bit and 24-bit address parsing.
*   External chains in ADL mode use 24-bit (3-byte) pointers.
*   The RPN operator `24` (`OP_STORE_AS_24BIT`) is used to store 24-bit results from complex expressions.

### Linker (`lk80`)
The `lk80` linker supports 24-bit linear addressing (up to 16MB). Use the `--output-file` with a `.bin` extension to generate large flat binaries.

## 24-bit Range Validation

The assembler automatically validates that relative jumps (`JR`, `DJNZ`) fit within the signed 8-bit range (-128 to 127). In ADL mode, this calculation correctly handles the 24-bit address wrap-around.

## New Instructions

All standard eZ80 instructions are supported, including:
*   `LEA`
*   `PEA`
*   `MLT`
*   `STMIX` / `RSMIX`
*   `TIB` / `TBU` (Z180 compatible)
*   etc.
