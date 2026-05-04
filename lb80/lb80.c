#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

/*
 * lb80 — Library manager for .REL (LINK-80) relocatable files.
 */

/* ------------------------------------------------------------------ */
/* BitReader                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    FILE *f;
    unsigned char current_byte;
    int bits_left;
} BitReader;

/* MS-REL Extension Link Item Subtypes */
#define EXT_REF_EXTERNAL   0x42
#define EXT_ADDRESS        0x43
#define EXT_SET_ADL        0x44

static int read_bit(BitReader *r);
static int peek_bit(BitReader *r) {
    long pos = ftell(r->f);
    unsigned char b = r->current_byte;
    int bl = r->bits_left;
    int bit = read_bit(r);
    fseek(r->f, pos, SEEK_SET);
    r->current_byte = b;
    r->bits_left = bl;
    return bit;
}

static int read_bit(BitReader *r) {
    if (r->bits_left == 0) {
        if (fread(&r->current_byte, 1, 1, r->f) != 1)
            return -1;
        r->bits_left = 8;
    }
    int bit = (r->current_byte >> (r->bits_left - 1)) & 1;
    r->bits_left--;
    return bit;
}

static unsigned int read_bits(BitReader *r, int n) {
    unsigned int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = read_bit(r);
        if (bit == -1) return 0;
        val = (val << 1) | bit;
    }
    return val;
}

static unsigned int read_16le(BitReader *r) {
    unsigned int lo = read_bits(r, 8);
    unsigned int hi = read_bits(r, 8);
    return lo | (hi << 8);
}

static void read_symbol(BitReader *r, char *dest, int dest_size) {
    int len = (int)read_bits(r, 3);
    if (len >= 2 && len <= 5) {
        unsigned int b1 = read_bits(r, 8);
        if (b1 == 0xFF) {
            int actual_len_bytes = len - 1;
            unsigned int actual_len = 0;
            for (int j = 0; j < actual_len_bytes; j++) {
                actual_len |= (read_bits(r, 8) << (j * 8));
            }
            len = (int)actual_len;
            if (len >= 256) r->bits_left = 0;
            int i;
            for (i = 0; i < len; i++) {
                char c = (char)read_bits(r, 8);
                if (i < dest_size - 1) dest[i] = c;
            }
            dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
            return;
        } else {
            if (dest_size > 1) dest[0] = (char)b1;
            int i;
            for (i = 1; i < len; i++) {
                char c = (char)read_bits(r, 8);
                if (i < dest_size - 1) dest[i] = c;
            }
            dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
            return;
        }
    }
    int i;
    for (i = 0; i < len; i++) {
        char c = (char)read_bits(r, 8);
        if (i < dest_size - 1) dest[i] = c;
    }
    dest[(i < dest_size - 1) ? i : dest_size - 1] = '\0';
}

static const unsigned char extended_header[16] = {
    0x85, 0xD3, 0x13, 0x92, 0xD4, 0xD5, 0x13, 0xD4,
    0xA5, 0x00, 0x00, 0x13, 0x8F, 0xFF, 0xF0, 0x9E
};

static void skip_header(BitReader *r) {
    if (r->bits_left > 0) r->bits_left = 0;
    long pos = ftell(r->f);
    unsigned char head[16];
    if (fread(head, 1, 16, r->f) == 16 && memcmp(head, extended_header, 16) == 0) {
        /* skipped */
    } else {
        fseek(r->f, pos, SEEK_SET);
    }
}

/* ------------------------------------------------------------------ */
/* Module structures                                                  */
/* ------------------------------------------------------------------ */

typedef enum {
    SYM_PUBLIC = 0,
    SYM_EXTERNAL = 1,
    SYM_COMMON = 2
} SymbolType;

typedef struct SymbolInfo {
    char name[256];
    SymbolType type;
    unsigned int value;
    struct SymbolInfo *next;
} SymbolInfo;

typedef struct Module {
    char name[256];
    unsigned char *data;
    size_t size;
    unsigned int cseg_size;
    unsigned int dseg_size;
    SymbolInfo *symbols;
    struct Module *next;
} Module;

static void free_modules(Module *head) {
    while (head) {
        Module *next = head->next;
        SymbolInfo *s = head->symbols;
        while (s) {
            SymbolInfo *sn = s->next;
            free(s);
            s = sn;
        }
        free(head->data);
        free(head);
        head = next;
    }
}

static Module *load_modules(const char *libname) {
    FILE *f = fopen(libname, "rb");
    if (!f) return NULL;

    Module *head = NULL;
    Module *tail = NULL;

    BitReader r = {f, 0, 0};
    skip_header(&r);
    long last_pos = ftell(f);

    char current_name[256] = {0};
    int has_name = 0;
    int flag_adl = 0;
    unsigned int cseg_sz = 0;
    unsigned int dseg_sz = 0;
    SymbolInfo *syms_head = NULL;
    SymbolInfo *syms_tail = NULL;

    while (1) {
        int first_bit = read_bit(&r);
        if (first_bit == -1) break;

        if (first_bit == 0) {
            read_bits(&r, 8);
        } else {
            int t2 = (int)read_bits(&r, 2);
            if (t2 == 0) {
                int ctrl = (int)read_bits(&r, 4);
                if (ctrl <= 4) {
                    char sym[1024] = {0};
                    read_symbol(&r, sym, sizeof(sym));
                    if (ctrl == 2) {
                        snprintf(current_name, sizeof(current_name), "%s", sym);
                        has_name = 1;
                    } else if (ctrl == 1) { // Common block name
                        SymbolInfo *s = calloc(1, sizeof(SymbolInfo));
                        snprintf(s->name, sizeof(s->name), "%s", sym);
                        s->type = SYM_COMMON;
                        if (!syms_head) syms_head = s;
                        if (syms_tail) syms_tail->next = s;
                        syms_tail = s;
                    } else if (ctrl == 4) { // Extension or External
                        if ((unsigned char)sym[0] == EXT_REF_EXTERNAL) { // External reference
                            SymbolInfo *s = calloc(1, sizeof(SymbolInfo));
                            snprintf(s->name, sizeof(s->name), "%s", sym + 1);
                            s->type = SYM_EXTERNAL;
                            if (!syms_head) syms_head = s;
                            if (syms_tail) syms_tail->next = s;
                            syms_tail = s;
                        } else if ((unsigned char)sym[0] == EXT_SET_ADL) {
                            flag_adl = (unsigned char)sym[1];
                        }
                    }
                } else if (ctrl <= 7) {
                    int seg = (int)read_bits(&r, 2); (void)seg;
                    unsigned int val = read_16le(&r);
                    if (flag_adl && (ctrl == 6 || ctrl == 7)) {
                        val |= (read_bits(&r, 8) << 16);
                    }
                    char sym[1024] = {0};
                    read_symbol(&r, sym, sizeof(sym));
                    if (ctrl == 7) { // Define public
                        SymbolInfo *s = calloc(1, sizeof(SymbolInfo));
                        snprintf(s->name, sizeof(s->name), "%s", sym);
                        s->type = SYM_PUBLIC;
                        s->value = val;
                        if (!syms_head) syms_head = s;
                        if (syms_tail) syms_tail->next = s;
                        syms_tail = s;
                    } else if (ctrl == 5) { // Common size
                        SymbolInfo *s = syms_head;
                        while (s) {
                            if (s->type == SYM_COMMON && strcmp(s->name, sym) == 0) {
                                s->value = val;
                                break;
                            }
                            s = s->next;
                        }
                    }
                } else if (ctrl <= 14) {
                    int seg = (int)read_bits(&r, 2); (void)seg;
                    unsigned int val = read_16le(&r);
                    if (flag_adl && (ctrl >= 8 && ctrl <= 14)) {
                        val |= (read_bits(&r, 8) << 16);
                    }
                    if (ctrl == 10) dseg_sz = val;
                    else if (ctrl == 13) cseg_sz = val;
                    else if (ctrl == 14) {
                        long current_pos = ftell(f);
                        size_t mod_size = (size_t)(current_pos - last_pos);
                        Module *m = calloc(1, sizeof(Module));
                        m->size = mod_size;
                        m->data = malloc(mod_size);
                        m->cseg_size = cseg_sz;
                        m->dseg_size = dseg_sz;
                        m->symbols = syms_head;
                        
                        if (has_name) {
                            snprintf(m->name, sizeof(m->name), "%s", current_name);
                        } else {
                            snprintf(m->name, sizeof(m->name), "%s", libname);
                        }
                        
                        long saved_pos = ftell(f);
                        fseek(f, last_pos, SEEK_SET);
                        fread(m->data, 1, mod_size, f);
                        fseek(f, saved_pos, SEEK_SET);

                        if (!head) head = m;
                        if (tail) tail->next = m;
                        tail = m;

                        last_pos = current_pos;
                        current_name[0] = '\0';
                        has_name = 0;
                        flag_adl = 0;
                        cseg_sz = 0;
                        dseg_sz = 0;
                        syms_head = NULL;
                        syms_tail = NULL;
                        r.bits_left = 0;
                        skip_header(&r);
                        last_pos = ftell(f);
                    }
                } else {
                    r.bits_left = 0;
                    skip_header(&r);
                    last_pos = ftell(f);
                }
            } else {
                int addr_size = flag_adl ? 24 : 16;
                read_bits(&r, addr_size);
            }
        }
    }

    fclose(f);
    return head;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static int append_rel(FILE *lib, const char *relname) {
    FILE *rel = fopen(relname, "rb");
    if (!rel) { perror(relname); return -1; }

    fseek(rel, 0, SEEK_END);
    long sz = ftell(rel);
    fseek(rel, 0, SEEK_SET);

    if (sz <= 0) { fclose(rel); return 0; }

    unsigned char last_byte;
    fseek(rel, -1, SEEK_END);
    fread(&last_byte, 1, 1, rel);
    fseek(rel, 0, SEEK_SET);

    long to_read = (last_byte == 0x9E) ? sz - 1 : sz;
    int written = 0;
    unsigned char buf[4096];
    while (to_read > 0) {
        size_t n = fread(buf, 1, (to_read > 4096) ? 4096 : to_read, rel);
        if (n <= 0) break;
        fwrite(buf, 1, n, lib);
        written += (int)n;
        to_read -= (long)n;
    }

    fclose(rel);
    return written;
}

typedef struct {
    char **argv;
    int argc;
    int capacity;
} t_arg_list;

static void arg_list_init(t_arg_list *l) {
    l->capacity = 256;
    l->argc = 0;
    l->argv = malloc(l->capacity * sizeof(char *));
}

static void arg_list_add(t_arg_list *l, char *arg) {
    if (l->argc >= l->capacity - 1) {
        l->capacity *= 2;
        l->argv = realloc(l->argv, l->capacity * sizeof(char *));
    }
    l->argv[l->argc++] = arg;
    l->argv[l->argc] = NULL;
}

static void arg_list_parse_string(t_arg_list *l, char *s) {
    char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        char *token_start = p;
        char *src = p;
        char *dst = p;
        int in_quotes = 0;

        while (*src) {
            if (*src == '\\' && src[1]) {
                src++;
                *dst++ = *src++;
            } else if (*src == '"') {
                in_quotes = !in_quotes;
                src++;
            } else if (!in_quotes && isspace((unsigned char)*src)) {
                break;
            } else {
                *dst++ = *src++;
            }
        }

        char next_char = *src;
        *dst = '\0';
        arg_list_add(l, token_start);
        
        if (next_char == '\0') break;
        p = src + 1;
    }
}

static void arg_list_parse_file(t_arg_list *l, const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz + 1);
    if (fread(buf, 1, sz, f) == (size_t)sz) {
        buf[sz] = 0;
        arg_list_parse_string(l, buf);
    }
    fclose(f);
}

static int arg_list_has(int argc, char **argv, const char *pattern) {
    for (int i = 1; i < argc; i++)
        if (argv[i] && strcmp(argv[i], pattern) == 0) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Global Options                                                     */
/* ------------------------------------------------------------------ */

static int opt_allow_duplicates = 0;
static int opt_show_banner = 1;
static int opt_verbosity = 0;
static char opt_file_extension[32] = ".rel";

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */

static void usage(void) {
    printf("lb80 - Library manager for .REL files (Nestor80/LINK-80 compatible)\n\n");
    printf("Usage:\n");
    printf("  lb80 <command> <libfile> [args...]\n\n");
    printf("Commands:\n");
    printf("  c, create   <libfile> <rel...>    Create a new library from REL files\n");
    printf("  a, add      <libfile> <rel...>    Add REL files to an existing library\n");
    printf("  s, set      <libfile> <rel...>    Add or replace modules in a library\n");
    printf("  r, remove   <libfile> <mod...>    Remove specified modules from library\n");
    printf("  e, extract  <libfile> <mod> [out] Extract a module to a REL file\n");
    printf("  l, list     <libfile>             List names of modules in library\n");
    printf("  v, view     <libfile>             Display detailed module information\n");
    printf("  d, dump     <libfile>             Dump raw link items for debugging\n\n");
    printf("Legacy Syntax:\n");
    printf("  lb80 <libfile> =<rel1>,<rel2>...  Create/Replace syntax\n");
    printf("  lb80 <libfile> /LIST              List modules syntax\n\n");
    printf("Options:\n");
    printf("  --working-dir <dir>      Change directory before processing\n");
    printf("  --allow-duplicates       Allow multiple modules with the same name\n");
    printf("  --no-show-banner         Suppress the startup greeting\n");
    printf("  --verbosity <n>          Set log level (0=quiet, 1=normal, 2+,3=debug)\n");
    printf("  --file-extension <ext>   Default extension for extracted modules (default: .rel)\n");
}

static int cmd_create(const char *libname, int argc, char **relfiles) {
    FILE *lib = fopen(libname, "wb");
    if (!lib) { perror(libname); return 2; }

    int modules = 0;
    for (int i = 0; i < argc; i++) {
        int n = append_rel(lib, relfiles[i]);
        if (n > 0) modules++;
    }

    unsigned char eof_byte = 0x9E;
    fwrite(&eof_byte, 1, 1, lib);
    fclose(lib);
    if (opt_verbosity > 0) printf("Created %s with %d module(s).\n", libname, modules);
    return 0;
}

static int cmd_add(const char *libname, int argc, char **relfiles) {
    FILE *lib = fopen(libname, "r+b");
    if (!lib) return cmd_create(libname, argc, relfiles);

    fseek(lib, -1, SEEK_END);
    int modules = 0;
    for (int i = 0; i < argc; i++) {
        int n = append_rel(lib, relfiles[i]);
        if (n > 0) modules++;
    }

    unsigned char eof_byte = 0x9E;
    fwrite(&eof_byte, 1, 1, lib);
    fclose(lib);
    if (opt_verbosity > 0) printf("Added %d module(s) to %s.\n", modules, libname);
    return 0;
}

static int cmd_set(const char *libname, int argc, char **relfiles) {
    Module *lib_mods = load_modules(libname);
    
    for (int i = 0; i < argc; i++) {
        Module *new_mods = load_modules(relfiles[i]);
        Module *m = new_mods;
        while (m) {
            Module *prev = NULL;
            Module *curr = lib_mods;
            int found = 0;
            while (curr) {
                if (curr->name[0] && m->name[0] && strcasecmp(curr->name, m->name) == 0) {
                    if (prev) prev->next = curr->next;
                    else lib_mods = curr->next;
                    
                    Module *next_mod = curr->next;
                    curr->next = NULL;
                    free_modules(curr);
                    
                    Module *new_m = calloc(1, sizeof(Module));
                    snprintf(new_m->name, sizeof(new_m->name), "%s", m->name);
                    new_m->size = m->size;
                    new_m->data = malloc(m->size);
                    memcpy(new_m->data, m->data, m->size);
                    new_m->next = next_mod;
                    if (prev) prev->next = new_m;
                    else lib_mods = new_m;
                    
                    found = 1;
                    break;
                }
                prev = curr;
                curr = curr->next;
            }
            if (!found) {
                Module *new_m = calloc(1, sizeof(Module));
                snprintf(new_m->name, sizeof(new_m->name), "%s", m->name);
                new_m->size = m->size;
                new_m->data = malloc(m->size);
                memcpy(new_m->data, m->data, m->size);
                if (prev) prev->next = new_m;
                else lib_mods = new_m;
            }
            m = m->next;
        }
        free_modules(new_mods);
    }

    FILE *lib = fopen(libname, "wb");
    Module *m = lib_mods;
    while (m) {
        fwrite(m->data, 1, m->size, lib);
        m = m->next;
    }
    unsigned char eof_byte = 0x9E;
    fwrite(&eof_byte, 1, 1, lib);
    fclose(lib);
    free_modules(lib_mods);
    if (opt_verbosity > 0) printf("Updated %s.\n", libname);
    return 0;
}

static int cmd_remove(const char *libname, int argc, char **names) {
    Module *lib_mods = load_modules(libname);
    if (!lib_mods) return 2;

    int removed = 0;
    for (int i = 0; i < argc; i++) {
        Module *prev = NULL;
        Module *curr = lib_mods;
        while (curr) {
            if (strcasecmp(curr->name, names[i]) == 0) {
                Module *next = curr->next;
                if (prev) prev->next = next;
                else lib_mods = next;
                
                Module *to_free = curr;
                curr = next;
                to_free->next = NULL;
                free_modules(to_free);
                removed++;
            } else {
                prev = curr;
                curr = curr->next;
            }
        }
    }

    if (removed > 0) {
        FILE *lib = fopen(libname, "wb");
        Module *m = lib_mods;
        if (m) {
            while (m) {
                fwrite(m->data, 1, m->size, lib);
                m = m->next;
            }
            unsigned char eof_byte = 0x9E;
            fwrite(&eof_byte, 1, 1, lib);
            fclose(lib);
            if (opt_verbosity > 0) printf("Removed %d module(s) from %s.\n", removed, libname);
        } else {
            fclose(lib);
            unlink(libname);
            if (opt_verbosity > 0) printf("Library %s empty, deleted.\n", libname);
        }
    } else {
        if (opt_verbosity > 0) printf("No modules removed.\n");
    }

    free_modules(lib_mods);
    return 0;
}

static int cmd_extract(const char *libname, const char *modname, const char *outname) {
    Module *lib_mods = load_modules(libname);
    if (!lib_mods) return 2;

    Module *m = lib_mods;
    while (m) {
        if (strcasecmp(m->name, modname) == 0) {
            char default_out[256];
            if (!outname) {
                const char *ext = opt_file_extension;
                if (ext[0] != '.') {
                    snprintf(default_out, sizeof(default_out), "%s.%s", m->name, ext);
                } else {
                    snprintf(default_out, sizeof(default_out), "%s%s", m->name, ext);
                }
                outname = default_out;
            }
            FILE *out = fopen(outname, "wb");
            if (!out) { perror(outname); free_modules(lib_mods); return 2; }
            fwrite(m->data, 1, m->size, out);
            unsigned char eof_byte = 0x9E;
            fwrite(&eof_byte, 1, 1, out);
            fclose(out);
            if (opt_verbosity > 0) printf("Extracted %s to %s.\n", modname, outname);
            free_modules(lib_mods);
            return 0;
        }
        m = m->next;
    }

    printf("Module %s not found.\n", modname);
    free_modules(lib_mods);
    return 1;
}

static int cmd_list(const char *libname) {
    Module *lib_mods = load_modules(libname);
    
    printf("Modules in %s:\n", libname);
    Module *m = lib_mods;
    int n = 0;
    while (m) {
        n++;
        printf("  %3d: %s\n", n, (m->name[0]) ? m->name : "(unnamed)");
        m = m->next;
    }
    printf("Total: %d module(s)\n", n);
    free_modules(lib_mods);
    return 0;
}

static int cmd_view(const char *libname) {
    Module *lib_mods = load_modules(libname);
    if (!lib_mods) return 2;

    printf("Detailed view of %s:\n", libname);
    Module *m = lib_mods;
    int n = 0;
    while (m) {
        n++;
        printf("Module #%d: %s\n", n, (m->name[0]) ? m->name : "(unnamed)");
        printf("  Size: %zu bytes\n", m->size);
        printf("  CSEG size: %04Xh, DSEG size: %04Xh\n", m->cseg_size, m->dseg_size);
        
        SymbolInfo *s = m->symbols;
        int pub_count = 0, ext_count = 0, com_count = 0;
        while (s) {
            if (s->type == SYM_PUBLIC) pub_count++;
            else if (s->type == SYM_EXTERNAL) ext_count++;
            else if (s->type == SYM_COMMON) com_count++;
            s = s->next;
        }
        
        if (pub_count) {
            printf("  Public symbols:\n");
            s = m->symbols;
            while (s) {
                if (s->type == SYM_PUBLIC) printf("    %04X %s\n", s->value, s->name);
                s = s->next;
            }
        }
        if (ext_count) {
            printf("  External references:\n");
            s = m->symbols;
            while (s) {
                if (s->type == SYM_EXTERNAL) printf("    %s\n", s->name);
                s = s->next;
            }
        }
        if (com_count) {
            printf("  Common blocks:\n");
            s = m->symbols;
            while (s) {
                if (s->type == SYM_COMMON) printf("    %04X %s\n", s->value, s->name);
                s = s->next;
            }
        }
        printf("\n");
        m = m->next;
    }
    free_modules(lib_mods);
    return 0;
}

static int cmd_dump(const char *libname) {
    FILE *f = fopen(libname, "rb");
    if (!f) { perror(libname); return 2; }

    printf("Raw link items in %s:\n", libname);
    BitReader r = {f, 0, 0};
    skip_header(&r);
    int mod_n = 1;
    printf("Module #%d:\n", mod_n);

    while (1) {
        int first_bit = read_bit(&r);
        if (first_bit == -1) break;

        if (first_bit == 0) {
            read_bits(&r, 8);
        } else {
            int t2 = (int)read_bits(&r, 2);
            if (t2 == 0) {
                int ctrl = (int)read_bits(&r, 4);
                if (ctrl <= 4) {
                    char sym[1024] = {0};
                    read_symbol(&r, sym, sizeof(sym));
                    printf("  Item %2d: Symbol [%s]\n", ctrl, sym);
                } else if (ctrl <= 7) {
                    int seg = (int)read_bits(&r, 2);
                    unsigned int val = read_16le(&r);
                    char sym[1024] = {0};
                    read_symbol(&r, sym, sizeof(sym));
                    printf("  Item %2d: Seg %d, Val %04Xh, Symbol [%s]\n", ctrl, seg, val, sym);
                } else if (ctrl <= 14) {
                    int seg = (int)read_bits(&r, 2);
                    unsigned int val = read_16le(&r);
                    printf("  Item %2d: Seg %d, Val %04Xh\n", ctrl, seg, val);
                    if (ctrl == 14) {
                        r.bits_left = 0;
                        skip_header(&r);
                        if (peek_bit(&r) != -1) {
                            mod_n++;
                            printf("Module #%d:\n", mod_n);
                        }
                    }
                } else {
                    printf("  Item 15: EOF\n");
                    r.bits_left = 0;
                    skip_header(&r);
                    if (peek_bit(&r) != -1) {
                        mod_n++;
                        printf("Module #%d:\n", mod_n);
                    }
                }
            } else {
                read_bits(&r, 16);
            }
        }
    }
    fclose(f);
    return 0;
}

static void reset_lb80_config(void) {
    opt_allow_duplicates = 0;
    opt_show_banner = 1;
    opt_verbosity = 0;
    strcpy(opt_file_extension, ".rel");
}

int main(int argc, char *argv[]) {
    t_arg_list al;
    arg_list_init(&al);
    arg_list_add(&al, argv[0]);

    int use_env = !arg_list_has(argc, argv, "--no-env-args");
    int use_def_file = !arg_list_has(argc, argv, "--no-default-file-args") && !arg_list_has(argc, argv, "--no-def-file-args");

    if (use_env) {
        const char *env = getenv("LB80_ARGS");
        if (!env) env = getenv("LINKSTOR80_ARGS");
        if (env) {
            char *ecopy = strdup(env);
            arg_list_parse_string(&al, ecopy);
        }
    }

    if (use_def_file) {
        arg_list_parse_file(&al, ".LB80");
    }

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--arguments-file") == 0 || strcmp(argv[i], "--args-file") == 0) {
            if (i + 1 < argc) {
                arg_list_parse_file(&al, argv[++i]);
            }
        } else {
            arg_list_add(&al, argv[i]);
        }
    }

    int final_argc = al.argc;
    char **final_argv = al.argv;

    int arg_idx = 1;
    while (arg_idx < final_argc && final_argv[arg_idx][0] == '-') {
        const char *a = final_argv[arg_idx];
        if (strcmp(a, "--working-dir") == 0 && arg_idx + 1 < final_argc) {
            if (chdir(final_argv[arg_idx + 1]) != 0) { perror(final_argv[arg_idx + 1]); return 2; }
            arg_idx += 2;
        } else if (strcmp(a, "--allow-duplicates") == 0) {
            opt_allow_duplicates = 1;
            arg_idx++;
        } else if (strcmp(a, "--no-show-banner") == 0) {
            opt_show_banner = 0;
            arg_idx++;
        } else if (strcmp(a, "--verbosity") == 0 && arg_idx + 1 < final_argc) {
            opt_verbosity = atoi(final_argv[arg_idx + 1]);
            arg_idx += 2;
        } else if (strcmp(a, "--file-extension") == 0 && arg_idx + 1 < final_argc) {
            strncpy(opt_file_extension, final_argv[arg_idx + 1], sizeof(opt_file_extension) - 1);
            arg_idx += 2;
        } else if (strcmp(a, "--no-env-args") == 0 || strcmp(a, "--no-default-file-args") == 0 || strcmp(a, "--no-def-file-args") == 0) {
            arg_idx++;
        } else if (strcmp(a, "--reset-config") == 0) {
            reset_lb80_config();
            arg_idx++;
        } else {
            arg_idx++;
        }
    }

    if (final_argc - arg_idx < 1) { usage(); return 1; }

    const char *cmd     = final_argv[arg_idx];
    const char *libname = (final_argc - arg_idx >= 2) ? final_argv[arg_idx + 1] : NULL;

    int is_modern = 0;
    if (strcmp(cmd, "c") == 0 || strcmp(cmd, "create") == 0 ||
        strcmp(cmd, "a") == 0 || strcmp(cmd, "add") == 0 ||
        strcmp(cmd, "s") == 0 || strcmp(cmd, "set") == 0 ||
        strcmp(cmd, "r") == 0 || strcmp(cmd, "remove") == 0 ||
        strcmp(cmd, "e") == 0 || strcmp(cmd, "extract") == 0 ||
        strcmp(cmd, "l") == 0 || strcmp(cmd, "list") == 0 ||
        strcmp(cmd, "v") == 0 || strcmp(cmd, "view") == 0 ||
        strcmp(cmd, "d") == 0 || strcmp(cmd, "dump") == 0) {
        is_modern = 1;
    }

    // Legacy syntax detection: libname =files or libname /LIST
    // Only if not a modern command
    if (!is_modern && libname && (libname[0] == '=' || libname[0] == '/')) {
        const char *real_libname = cmd;
        for (int j = arg_idx + 1; j < argc; j++) {
            char *arg = argv[j];
            if (arg[0] == '=') {
                char *copy = strdup(arg + 1);
                char *tok = strtok(copy, ",");
                char *files[256];
                int count = 0;
                while (tok && count < 256) {
                    files[count++] = tok;
                    tok = strtok(NULL, ",");
                }
                cmd_set(real_libname, count, files);
                free(copy);
            } else if (strcasecmp(arg, "/LIST") == 0 || strcasecmp(arg, "/L") == 0) {
                cmd_list(real_libname);
            }
        }
        return 0;
    }

    if (!libname) { usage(); return 1; }

    if (is_modern) {
        if ((strcmp(cmd, "c") == 0 || strcmp(cmd, "create") == 0) && argc - arg_idx >= 3) {
            return cmd_create(libname, argc - arg_idx - 2, argv + arg_idx + 2);
        } else if ((strcmp(cmd, "a") == 0 || strcmp(cmd, "add") == 0) && argc - arg_idx >= 3) {
            return cmd_add(libname, argc - arg_idx - 2, argv + arg_idx + 2);
        } else if ((strcmp(cmd, "s") == 0 || strcmp(cmd, "set") == 0) && argc - arg_idx >= 3) {
            return cmd_set(libname, argc - arg_idx - 2, argv + arg_idx + 2);
        } else if ((strcmp(cmd, "r") == 0 || strcmp(cmd, "remove") == 0) && argc - arg_idx >= 3) {
            return cmd_remove(libname, argc - arg_idx - 2, argv + arg_idx + 2);
        } else if ((strcmp(cmd, "e") == 0 || strcmp(cmd, "extract") == 0) && argc - arg_idx >= 3) {
            return cmd_extract(libname, argv[arg_idx + 2], (argc - arg_idx >= 4) ? argv[arg_idx + 3] : NULL);
        } else if ((strcmp(cmd, "l") == 0 || strcmp(cmd, "list") == 0)) {
            return cmd_list(libname);
        } else if ((strcmp(cmd, "v") == 0 || strcmp(cmd, "view") == 0)) {
            return cmd_view(libname);
        } else if ((strcmp(cmd, "d") == 0 || strcmp(cmd, "dump") == 0)) {
            return cmd_dump(libname);
        }
    }

    fprintf(stderr, "Unknown command or missing arguments: %s\n", cmd);
    usage();
    return 1;
}
