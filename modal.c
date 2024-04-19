#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

/* Bump allocator */

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

#define IS_ROOT(f, x) ((f)->parents[(x)] == (x))

typedef int node_id;

struct forest {
    symbol *symbols;
    node_id *parents;
    unsigned int node_count;
    unsigned int nodes_max;
};

struct forest rules_forest;
struct forest arena1, arena2, *src = NULL, *dst = NULL;

void swap_arenas() {
    struct forest *tmp = src;
    src = dst;
    dst = tmp;
    dst->node_count = 0;
}

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

size_t tree_size(struct forest *forest, node_id id) {
    assert(id < forest->node_count && IS_ROOT(forest, id));
    size_t res = 0;
    do {
        id++;
        res++;
    } while(id < forest->node_count && !IS_ROOT(forest, id));
    return res;
}

void tree_print(struct forest *forest, node_id id) {
    assert(id < forest->node_count && IS_ROOT(forest, id));
    node_id parent = id;
    unsigned int level = 0;
    do { 
        node_id new_parent = forest->parents[id];
        if(new_parent > parent) {
            level++;
            parent = new_parent;
        } else if(new_parent < parent) {
            level--;
            parent = new_parent;
        }
        for(int i = 0; i < level * 4; i++)
            printf(" ");
        sym_print(forest->symbols[id]);
        printf("\n");
        if(IS_ROOT(forest, id)) level++; /* first iteration of the loop */
        id++;
    } while(!IS_ROOT(forest, id) && id < forest->node_count);
}

void tree_print_flat(struct forest *forest, node_id id) {
    assert(id < forest->node_count && IS_ROOT(forest, id));
    node_id old_parent = id;
    size_t size = tree_size(forest, id);
    for(unsigned int i = 0; i < size; i++) {
        sym_print(forest->symbols[id+i]);
            printf(" ");
        node_id new_parent = forest->parents[id+i];
        if(new_parent < old_parent) {
            while(new_parent < old_parent) {
                sym_print(CLOSE_PAREN);
                printf(" ");
                old_parent = forest->parents[old_parent];
            }
        }
        old_parent = new_parent;
    }
    node_id n = id + size - 1;
    while(!IS_ROOT(forest, n)) {
        sym_print(CLOSE_PAREN);
        printf(" ");
        n = forest->parents[n];
    }
}


node_id copy_tree(struct forest *destination, struct forest *source, node_id id) {
    assert(id < source->node_count && IS_ROOT(source, id));
    size_t size = tree_size(source, id);
    node_id new_root = new_root_node(destination, source->symbols[id]);
    for(unsigned int i = 1; i < size; i++)
        new_child_node(destination, source->symbols[id+i], new_root + source->parents[id+i] - id);
    return new_root;
}

typedef unsigned int rule_id;

struct rules {
    node_id *lhs, *rhs;
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


void tokenize(FILE *f) {
    char scratch[SYMBOL_SIZE_MAX];
    size_t char_count = 0;
    node_id current_parent = -1;
    int look = fgetc(f);
    while(look != EOF) {
        if(look != ' ' && look != '\n' && look != '(' && look != ')') {
            scratch[char_count++] = look;
            look = fgetc(f);
            symbol sym = intern(scratch, char_count);
            if(sym == DEFINE) {
                if(look != ' ')
                    ERROR("expected space after <>");
                new_root_node(src, sym);
                char_count = 0;
            }
        } else {
            if(char_count > 0) {
                symbol sym = intern(scratch, char_count);
                new_root_node(src, sym);
                char_count = 0;
                if(sym == DEFINE && look != ' ')
                        ERROR("expected space after <>");
            }
            switch(look) {
            case '(':
                new_root_node(src, OPEN_PAREN);
                break;
            case ')':
                new_root_node(src, CLOSE_PAREN);
                break;
            case ' ':
            case '\n':
                break;
            default:
                ERROR("unreachable");
            }
            look = fgetc(f);
        }
        
    }
    if(char_count > 0) {
        new_root_node(src, intern(scratch, char_count));
    }
}

void parse() {
    node_id current_parent = -1;
    for(node_id i= 0; i < src->node_count; i++) {
        symbol sym = src->symbols[i];
        if(sym == OPEN_PAREN) {
            if(current_parent == -1)
                current_parent = new_root_node(dst, sym);
            else
                current_parent = new_child_node(dst, sym, current_parent);
        } else if(sym == CLOSE_PAREN) {
            if(current_parent == -1)
                ERROR("unexpected ')'");
            else if(IS_ROOT(dst, current_parent))
                current_parent = -1;
            else
                current_parent = dst->parents[current_parent];
        } else {
            if(current_parent == -1)
                new_root_node(dst, sym);
            else
                new_child_node(dst, sym, current_parent);
        }
    }
}

void parse_rules() {
    unsigned int i = 0;
    while(i < src->node_count) {
        if(src->symbols[i] == DEFINE) {
            rule_id r_id = new_rule();
            i++;
            rules.lhs[r_id] = copy_tree(&rules_forest, src, i);
            i += tree_size(src, i);
            rules.rhs[r_id] = copy_tree(&rules_forest, src, i);
            i += tree_size(src, i);
        } else {
            copy_tree(dst, src, i);
            i += tree_size(src, i);
        }
    }
}

int match(struct forest *f1, node_id id1, struct forest *f2, node_id id2) {
    assert(id1 < f1->node_count && IS_ROOT(f1, id1));
    assert(id2 < f2->node_count && IS_ROOT(f2, id2));
    printf("match : ");
    tree_print_flat(f1, id1);
    printf(" <--> ");
    tree_print_flat(f2, id2);
    //printf("\n");
    size_t size1 = tree_size(f1, id1), size2 = tree_size(f2, id2);
    if(size1 != size2) {
        printf(" : false (different size)\n");
        return 0;
    }
    for(unsigned int i = 0; i < size1; i++) {
        if(f1->symbols[id1 + i] != f2->symbols[id2 + i]) {
            printf(" : false (different symbols)\n");
            return 0;
        }
        if(f1->parents[id1 + i] - id1 != f2->parents[id2 + i] - id2) {
            printf(": false (different structure)\n");;
            return 0;
        }
    }
    printf(" : true\n");
    return 1;
}

void interpret() {
    node_id id = 0;
    while(id < src->node_count) {
        for(rule_id r = 0; r < rules.count; r++) {
            if(match(&rules_forest, rules.lhs[r], src, id)) {
                copy_tree(dst, &rules_forest, rules.rhs[r]);
            }
        }
        id += tree_size(src, id);
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

    FILE *f = fopen(argv[1], "r");
    if(!f) {
        perror("fopen");
        return 1;
    }
    tokenize(f);
    fclose(f);
    for(int i = 0; i < src->node_count; i++) {
        sym_print(src->symbols[i]);
        printf("-");
    }
    parse();
    
    swap_arenas();
    parse_rules();
    swap_arenas();

    for(node_id i = 0; i < src->node_count; i++) {
        if(IS_ROOT(src, i)) {
            printf("*********\n");
            tree_print(src, i);
        }
    }

    printf("*** rules ***\n");
    for(unsigned int i = 0; i < rules.count; i++) {
        tree_print_flat(&rules_forest, rules.lhs[i]);
        printf(" --> ");
        tree_print_flat(&rules_forest, rules.rhs[i]);
        printf("\n");
    }

    printf("\n");

    printf("Go !!\n");

    interpret();
    swap_arenas();
    printf("output:\n");
    for(node_id i = 0; i < src->node_count; i++) {
        if(IS_ROOT(src, i)) {
            printf("*********\n");
            tree_print_flat(src, i);
            printf("\n");
        }
    }
    

    return 0;
}