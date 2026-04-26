# next80

**next80** is a [Z80](https://en.wikipedia.org/wiki/Zilog_Z80), [R800](https://en.wikipedia.org/wiki/R800_(CPU)) and [Z280](https://en.wikipedia.org/wiki/Zilog_Z280) assembler toolchain written in C, almost fully compatible with the [Microsoft MACRO-80](https://en.wikipedia.org/wiki/Microsoft_MACRO-80) assembler. It is a C port of the original [Nestor80](https://github.com/Konamiman/Nestor80) project (.NET/C#).

The toolchain includes three tools:

| Tool | Description |
|------|-------------|
| **n80** | Assembler (replaces MACRO-80 / N80) |
| **lk80** | Linker (replaces LINK-80 / Linkstor80) |
| **lb80** | Library manager (replaces LIB-80 / Libstor80) |

`lk80` and `lb80` are relevant when [writing relocatable code](WritingRelocatableCode.md).


## Features

* **Native binary** — no runtime required, runs on any POSIX-compliant system.
* Almost fully **compatible with [Microsoft MACRO-80](https://en.wikipedia.org/wiki/Microsoft_MACRO-80)** for Z80 code.
* Produces **absolute and relocatable binary files**. Relocatable files can conform to the MACRO-80/LINK-80 format, or to the SDCC format (XL3) used by [SDCC](https://sdcc.sourceforge.net/).
* Supports **Z80 undocumented instructions**.
* Full **Z280 and R800** CPU instruction support.
* **UTF-8 symbol names** — labels and module names may contain Unicode characters.
* **Modern string handling** — choose the encoding for string literals, use C-style escape sequences.
* **Expression interpolation** in user messages (`{expr:radix}`).
* Over 50 **command-line arguments** for fine-grained control.


## Installation

Build all tools and install to `/usr/local`:

```
make
sudo make install
```

To install to a custom prefix:

```
make PREFIX=/opt/next80
sudo make PREFIX=/opt/next80 install
```

To uninstall:

```
sudo make uninstall
```

See [Building](#building) for more options.


## Quick start

1. Write a Z80 source file, for example `hello.asm`:

```asm
CHGET: equ 009Fh
CHPUT: equ 00A2h

    org 4000h

    db 41h,42h
    dw START
    ds 4010h-$

START:
    call PRINT
    call CHGET
    ret

PRINT:
    ld hl,HELLO
loop:
    ld a,(hl)
    or a
    ret z
    call CHPUT
    inc hl
    jr loop

HELLO:
    db "Hello, world!\r\n"
    db "Press any key...\0"

    ds 8000h-$
```

2. Assemble it:

```
n80 hello.asm
```

This produces `hello.bin`. To specify the output file name:

```
n80 hello.asm hello.rom
```

3. For help on all available arguments:

```
n80 --help
```


## Building

Requirements: a C99-compatible compiler (GCC or Clang), `make`.

```
# Build all tools
make

# Build individual tools
make -C n80
make -C lk80
make -C lb80

# Run all tests
make test
```


## Documentation

* [**Language reference**](LanguageReference.md) — assembly language syntax and all pseudo-operators.
* [**Writing relocatable code**](WritingRelocatableCode.md) — how to write and link multi-file projects.
* [**Relocatable file format**](RelocatableFileFormat.md) — detailed format of the LINK-80 `.rel` files.
* [**Z280 support**](Z280Support.md) — Z280-specific features and addressing modes.
* [**SDCC file format support**](SdccFileFormatSupport.md) — how to produce SDCC-compatible relocatable files.
* [**Intel 8080 support**](Intel8080Support.md) — implementation analysis and roadmap for `.8080` / `.cpu 8080` mode.


## Compatibility notes

next80 is a port of Nestor80. Relocatable files produced by next80 are binary-compatible with those produced by Nestor80 and LINK-80. The extended relocatable file format (with the `LNKSTOR` header) is supported by both `lk80` and the original Linkstor80.

SDCC XL3 format files can be consumed by both `lk80` and SDLD.


## License

See the [LICENSE](../Nestor80/LICENSE) file.
