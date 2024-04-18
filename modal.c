#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ERROR(...) \
    do { \
        fprintf(stderr, "%s:%d: %s(): ", __FILE__, __LINE__, __func__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        exit(1); \
    } while (0)

#define MEMORY_SIZE 0x10000
#define INTERNED_STRINGS_BUFFER_SIZE 0x1000
#define STRING_COUNT_MAX 0x100
#define SYMBOL_SIZE_MAX 0X100
#define RULES_FOREST_NODES_MAX 0x100
#define RULES_COUNT_MAX 0x100
#define ARENA_NODES_MAX 0x200

/* */

#define DEFINE 0
#define OPEN_PAREN 1
#define CLOSE_PAREN 2

/* Buddy allocator */

char memory[MEMORY_SIZE] = {0}, *next_free = memory;

void *alloc(size_t size) {
    printf("allocating 0x%x bytes\n", size);
    if(next_free - memory >= sizeof(memory))
        ERROR("out of memory\n");
    void *res = next_free;
    next_free += size;
    return res;
}


/* Symbols / string interning ********************************************** */

#define RESERVED(x) ((x) == 0) /* We intern reserved '<>' first, so that it has symbol number 0 */

static char *interned_strings_buffer = NULL, *next_interned = NULL;

typedef struct string {
    char *ptr;
    size_t len;
} string;

static string *strings = NULL;
static unsigned int string_count = 0;

typedef unsigned int symbol;

static symbol intern(const char *s, size_t len) {
    for(int i = 0; i < string_count; i++) {
        if(len != strings[i].len)
            continue;
        if(!strncmp(strings[i].ptr, s, len))
            return i;
    }
    if(next_interned + len - interned_strings_buffer > INTERNED_STRINGS_BUFFER_SIZE)  /* TODO: > or >= ?? */
        ERROR("Out of memory for interned strings");
    if(string_count >= STRING_COUNT_MAX)
        ERROR("Out of space for a new symbol");
    memcpy(next_interned, s, len);
    strings[string_count] = (string){.ptr = next_interned, .len = len};
    next_interned += len;
    return string_count++;
}

void sym_print(symbol s) {
    if(s >= string_count)
        ERROR("invalid symbol");
    for(size_t i = 0; i < strings[s].len; i++)
        printf("%c", strings[s].ptr[i]);
}

/* Tree struff ************************************************************* */

typedef unsigned int node_id;

struct forest {
    symbol *symbols;
    unsigned int *parents;
    unsigned int node_count;
    unsigned int nodes_max;
};

struct forest rules_forest;
struct forest arena1, arena2, *src = NULL, *dst = NULL;

struct forest alloc_forest(unsigned int nodes_max) {
    struct forest res;
    res.symbols = alloc(sizeof(*res.symbols) * nodes_max);
    res.parents = alloc(sizeof(*res.parents) * nodes_max);
    res.node_count = 0;
    res.nodes_max = nodes_max;
    return res;
}

node_id new_node(struct forest *forest) {
    if(forest->node_count >= forest->nodes_max)
        ERROR("not enough free nodes\n");
    return forest->node_count++;
}

node_id new_child_node(struct forest *forest, symbol sym, unsigned int parent) {
    node_id id = new_node(forest);
    forest->symbols[id] = sym;
    forest->parents[id] = parent;
    return id;
}

node_id new_root_node(struct forest *forest, symbol sym) {
    node_id id = new_node(forest);
    forest->symbols[id] = sym;
    forest->parents[id] = id;
    return id;
}

typedef unsigned int rule_id;

struct rules {
    unsigned int *lhs, *rhs;
    unsigned int count, count_max;
};

struct rules rules;

void init_rules() {
    rules.lhs = alloc(sizeof(*rules.lhs) * RULES_COUNT_MAX);
    rules.rhs = alloc(sizeof(*rules.lhs) * RULES_COUNT_MAX);
    rules.count = 0;
    rules.count_max = RULES_COUNT_MAX;
}

rule_id new_rule() {
    if(rules.count >= rules.count_max) {
        ERROR("not enough free rules");
    }
    return rules.count++;
}


void parse(FILE *f) {
    char scratch[SYMBOL_SIZE_MAX];
    size_t char_count = 0;
    int look = fgetc(f);
    while(look != EOF) {
        printf("look = %c", look);
        if(look != ' ') {
            scratch[char_count++] = look;
            look = fgetc(f);
            symbol sym = intern(scratch, char_count);
            if(sym == OPEN_PAREN || sym == CLOSE_PAREN || sym == DEFINE) {
                new_root_node(src, sym);
                char_count = 0;
                if(sym == DEFINE && look != ' ')
                    ERROR("expected space after <>");
            }
        } else if(char_count > 0) {
            new_root_node(src, intern(scratch, char_count));
            char_count = 0;
            look = fgetc(f);
        } else {
            look = fgetc(f);
        }
    }
    if(char_count > 0) {
        new_root_node(src, intern(scratch, char_count));
    }
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s file.modal\n", argv[0]);
        return 1;
    }

    interned_strings_buffer = alloc(INTERNED_STRINGS_BUFFER_SIZE);
    next_interned = interned_strings_buffer;

    strings = alloc(sizeof(string) * STRING_COUNT_MAX);
    rules_forest = alloc_forest(RULES_FOREST_NODES_MAX);
    arena1 = alloc_forest(ARENA_NODES_MAX);
    src = &arena1;
    arena2 = alloc_forest(ARENA_NODES_MAX);
    dst = &arena2;

    init_rules();


    intern("<>", 2);
    intern("(", 1);
    intern(")", 1);
    char reg[2] = {'?', 0};
    for(int i = 33; i < 127; i++) {
        reg[1] = i;
        intern(reg, 2);
    }

    for(int i = 0; i < string_count; i++) {
        sym_print(i);
        printf("\n");
    }

    

    
    FILE *f = fopen(argv[1], "r");
    if(!f) {
        perror("fopen");
        return 1;
    }
    parse(f);

    fclose(f);

    for(int i = 0; i < src->node_count; i++) {
        sym_print(src->symbols[i]);
        printf("-");
    }
    

    return 0;
}