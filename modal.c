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

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/* Symbols / string interning ********************************************** */

static char interned_strings[0x10000], *next_interned = interned_strings;

typedef struct string {
    char *ptr;
    size_t len;
} string;

static string strings[0x1000];
static unsigned int string_count = 0;

typedef unsigned int symbol;

static symbol intern(const char *s, size_t len) {
    for(int i = 0; i < string_count; i++) {
        if(len != strings[i].len)
            continue;
        if(!strncmp(strings[i].ptr, s, len))
            return i;
    }
    if(next_interned + len - interned_strings > sizeof(interned_strings))  /* TODO: > or >= ?? */
        ERROR("Out of memory for interned strings");
    if(string_count >= sizeof(strings))
        ERROR("Out of space for a new symbol");
    memcpy(next_interned, s, len);
    strings[string_count] = (string){.ptr = next_interned, .len = len};
    next_interned += len;
    return string_count++;
}

static void sym_print(symbol s) {
    if(s >= string_count)
        ERROR("invalid symbol");
    for(size_t i = 0; i < strings[s].len; i++)
        printf("%c", strings[s].ptr[i]);
}

/* Constants (calculated at runtime) */

symbol DEFINE, OPEN_PAREN, CLOSE_PAREN, LAST_REGISTER;

#define IS_REGISTER(x) ((x) <= LAST_REGISTER)

/* Tree struff ************************************************************* */

typedef int node_id;
typedef struct node {
    node_id p, l; /* parent, left-sibling */
    symbol sym;
} node_t;

#define NODES_MAX 0x10000
static node_t forest[NODES_MAX];
static unsigned int free_list[NODES_MAX], free_list_ptr = 0;

static void init_forest() {
    for(node_id i = 0; i < NODES_MAX; i++)
        forest[i] = (node_t){.p = -1, .p = -1};
}

static void init_free_list() {
    for(int i = 0; i < NODES_MAX; i++)
        free_list[i] = NODES_MAX - 1 - i;
    free_list_ptr = NODES_MAX - 1;
}

void init() {
    init_forest();
    init_free_list();
}

static node_id new_node_raw() {
    if(free_list_ptr == 0) {
        ERROR("out of free nodes");
        exit(1);
    }
    return free_list[free_list_ptr--];
}

static node_id new_node(node_id p, node_id l, symbol sym) {
    assert(sym < string_count);
    node_id id = new_node_raw();
    forest[id].p = p != -1 ? p : id;
    forest[id].l = l != -1 ? l : id;
    forest[id].sym = sym;
    return id;
}

static void free_node(node_id id) {
    #ifndef NDEBUG
    for(node_id i = 0; i < NODES_MAX; i++) {
        if(free_list[i] == id) {
            ERROR("node %d already freed", id);
            exit(1);
        }
    }
    #endif
    assert(free_list_ptr < NODES_MAX - 1);
    free_list[free_list_ptr++] = id;
    forest[id].l = -1;
    forest[id].p = -1;
}

/* Tree walking ************************************************************ */

static int up(node_id *id) {
    node_id p = forest[*id].p;
    if(p == *id) return 0;
    *id = p;
    return 1;
}

static int down(node_id *id) {
    for(int i = 0; i < NODES_MAX; i++) {
        if(forest[i].p == *id) {
            *id = i;
            return 1;
        }
    }
    return 0;
}

static int left(node_id *id) {
    node_id l = forest[*id].l;
    if(l == *id) return 0;
    *id = l;
    return 1;
}

static int right(node_id *id) {
    for(int i = 0; i < NODES_MAX; i++) {
        if(forest[i].l == *id) {
            *id = i;
            return 1;
        }
    }
    return 0;
}

/* Graphviz **************************************************************** */

int is_free(node_id id) {
    for(node_id i = 0; i <= free_list_ptr; i++) {
        if(free_list[i] == id)
            return 1;
    }
    return 0;
}

void graphviz(const char *path) {
    FILE *f = fopen(path, "w");
    if(!f) {
        ERROR("failed to open %s", path);
        exit(1);
    }
    fprintf(f, "digraph G {\n");
    for(node_id i = 0; i < NODES_MAX; i++) {
        if(!is_free(i)) {
            symbol sym;

            node_id p = forest[i].p;
            sym = forest[p].sym;
            printf("i = %d, p=%d, parent sym = ", i, p);
            sym_print(sym);
            printf("\n");
            fprintf(f, "\"%d '", p);
            fwrite(strings[sym].ptr, strings[sym].len, 1, f);
            fprintf(f, "'\" -> ");

            sym = forest[i].sym;
            fprintf(f, "\"%d '", i);
            fwrite(strings[sym].ptr, strings[sym].len, 1, f);
            fprintf(f, "'\";\n");
        }
    }
    fprintf(f, "}\n");
    fclose(f);

}

/* Parsing ***************************************************************** */

node_id tokenize(FILE *f) {
    char scratch[1024];
    size_t char_count = 0;
    node_id root = new_node(-1, -1, OPEN_PAREN);
    node_id cl = -1; /* current left sibling */
    int look = fgetc(f);
    while(look != EOF) {
        if(look != ' ' && look != '\n' && look != '(' && look != ')') {
            scratch[char_count++] = look;
            look = fgetc(f);
            symbol sym = intern(scratch, char_count);
            if(sym == DEFINE) {
                if(look != ' ')
                    ERROR("expected space after <>");
                cl = new_node(root, cl, sym);
                char_count = 0;
            }
        } else {
            if(char_count > 0) {
                symbol sym = intern(scratch, char_count);
                cl = new_node(root, cl, sym);
                char_count = 0;
                if(sym == DEFINE && look != ' ')
                        ERROR("expected space after <>");
            }
            switch(look) {
            case '(':
                cl = new_node(root, cl, OPEN_PAREN);
                break;
            case ')':
                cl = new_node(root, cl, CLOSE_PAREN);
                break;
            case ' ':
            case '\n':
                break;
            default:
                ERROR("unreachable");
                exit(1);
            }
            look = fgetc(f);
        }
        
    }
    if(char_count > 0) {
        new_node(root, cl, intern(scratch, char_count));
    }
    return root;
}

node_id parse(node_id tokens_root) {
    node_id id = tokens_root;
    if(!down(&id)) {
        ERROR("no tokens");
        exit(1);
    }

    node_id cp = -1, cl = -1;
    do {
        symbol sym = forest[id].sym;
        if(sym == OPEN_PAREN)
            cp = new_node(cp, -1, sym);
        else if(sym == CLOSE_PAREN) {
            if(cp == -1) {
                ERROR("")
            }
        }
    } while(right(&id));

    /* *** old code ***
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
    */
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s file.modal\n", argv[0]);
        return 1;
    }

    init();

    char reg[2] = {'?', 0};
    for(int i = 33; i < 256; i++) {
        reg[1] = i;
        symbol s = intern(reg, 2);
        if(i == 255) LAST_REGISTER = s;
    }
    DEFINE = intern("<>", 2);
    OPEN_PAREN = intern("(", 1);
    CLOSE_PAREN = intern(")", 1);

    FILE *f = fopen(argv[1], "r");
    if(!f) {
        perror("fopen");
        return 1;
    }
    node_id tokens = tokenize(f);
    fclose(f);

    graphviz("tokens.dot");

    return 0;
}