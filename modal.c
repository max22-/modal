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

#define IS_ROOT(x) (forest[(x)].p == (x))

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
    for(node_id i = 0; i < free_list_ptr; i++) {
        if(free_list[i] == id) {
            ERROR("node %d already freed", id);
            exit(1);
        }
    }
    #endif
    assert(free_list_ptr < NODES_MAX - 1);
    free_list[++free_list_ptr] = id;
    forest[id].l = -1;
    forest[id].p = -1;
}

static void dfpo_next(node_id *id);

static void free_tree(node_id id) {
    node_id tmp_id = id;
    dfpo_next(&tmp_id);
    while(tmp_id != id) {
        node_id saved = tmp_id;
        dfpo_next(&tmp_id);
        free_node(saved);
    };
    free_node(tmp_id);
}

/* Tree walking ************************************************************ */

static int up(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    node_id p = forest[*id].p;
    if(p == *id) return 0;
    *id = p;
    return 1;
}

static int down(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    for(int i = 0; i < NODES_MAX; i++) {
        if(forest[i].p == *id && i != *id) {
            *id = i;
            return 1;
        }
    }
    return 0;
}

static int left(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    node_id l = forest[*id].l;
    if(l == *id) return 0;
    *id = l;
    return 1;
}

static int right(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    for(int i = 0; i < NODES_MAX; i++) {
        if(forest[i].l == *id && i != *id) {
            *id = i;
            return 1;
        }
    }
    return 0;
}

static void bottom(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    while(down(id));
}

static int rightmost(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    node_id saved = *id;
    while(right(id));
    return saved != *id;
}

/* depth-first post-order traversal */
static void dfpo_next(node_id *id) {
    assert(*id >= 0 && *id < NODES_MAX);
    if(IS_ROOT(*id)) {
        bottom(id);
        return;
    }
    if(right(id))
        bottom(id);
    else
        up(id);
}

/* removes a subtree from a tree, without freeing it (the goal is to paste it afterwards) */
static void cut(node_id id) {
    assert(id >= 0 && id < NODES_MAX);
    node_id left_sibling = id, right_sibling = id;
    left(&left_sibling);
    right(&right_sibling);
    if(right_sibling != id) {
        if(left_sibling != id)
            forest[right_sibling].l = left_sibling;
        else
            forest[right_sibling].l = right_sibling;
    }
    forest[id].l = id;
    forest[id].p = id;
}

static void delete(node_id id) {
    assert(id >= 0 && id < NODES_MAX);
    cut(id);
    free_tree(id);
}

static void append_child(node_id parent, node_id child) {
    assert(parent >= 0 && parent < NODES_MAX);
    assert(child >= 0 && child < NODES_MAX);
    forest[child].p = parent;
    node_id id = parent;
    if(!down(&id)) {
        forest[child].l = child;
    } else {
        rightmost(&id);
        forest[child].l = id;
    }
}

/* Graphviz **************************************************************** */

static int is_free(node_id id) {
    for(node_id i = 0; i <= free_list_ptr; i++) {
        if(free_list[i] == id)
            return 1;
    }
    return 0;
}

static void graphviz(const char *path) {
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

static void graphviz_parent_sibling(const char *path) {
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
            node_id l = forest[i].l;
            node_id src_id;
            if(l == i)
                src_id = p;
            else
                src_id = l;
            sym = forest[src_id].sym;
            fprintf(f, "\"%d '", src_id);
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

static node_id tokenize(FILE *f) {
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

static void parse_rule(node_id rules, node_id location) {
    assert(forest[location].sym == DEFINE);
    node_id token = location;
    if(!right(&location))
        ERROR("unexpected <> at end of file");
    node_id lhs = location;
    if(!right(&location))
        ERROR("unexpected end of file in rule definition");
    node_id rhs = location;
    node_id rule = new_node(-1, -1, OPEN_PAREN);
    cut(lhs);
    cut(rhs);
    delete(token);
    append_child(rule, lhs);
    append_child(rule, rhs);
    append_child(rules, rule);
}

static node_id parse_rules(node_id ast) {
    node_id id = ast;
    node_id rules = new_node(-1, -1, OPEN_PAREN);
    if(!down(&id))
        return;
    int stop = 0;
    do {
        if(forest[id].sym == DEFINE) {
            node_id next = id;
            stop = !right(&next) || !right(&next) || !right(&next);
            parse_rule(rules, id);
            id = next;
        } else {
            stop = !right(&id);
        }
    } while(!stop);
    return rules;
}

static node_id parse(node_id tokens_root) {
    node_id id = tokens_root;
    if(!down(&id)) {
        ERROR("no tokens");
        exit(1);
    }

    node_id ast = new_node(-1, -1, OPEN_PAREN);

    node_id cp = ast, cl = -1;
    do {
        symbol sym = forest[id].sym;
        if(sym == OPEN_PAREN) {
            cp = new_node(cp, cl, sym);
            cl = -1;
        }
        else if(sym == CLOSE_PAREN) {
            if(cp == ast) {
                ERROR("unexpected ')'");
                exit(1);
            } else {
                cl = cp;
                rightmost(&cl);
                up(&cp);
                
            }
        } else {
            cl = new_node(cp, cl, sym);
        }
    } while(right(&id));

    if(cp != ast) {
        ERROR("missing ')'");
        exit(1);
    }

    return ast;
}

/* Interpreter ************************************************************* */

enum move2_result {FAIL=-1, SAME_END, SAME_FORWARD};

#define MOVE2(dir) \
    static int dir ## 2(node_id *id1, node_id *id2) { \
        node_id t1 = *id1, t2 = *id2; \
        int res1 = dir(&t1), res2 = dir(&t2); \
        if(res1 != res2) { \
            return FAIL; \
        } else if(res1 == 1) { \
            *id1 = t1; \
            *id2 = t2; \
            return SAME_FORWARD; \
        } else return SAME_END; \
    }

MOVE2(down)
MOVE2(right)
MOVE2(leftmost)

static int match(node_id id1, node_id id2) {
    assert(id1 >= 0 && id1 < NODES_MAX);
    assert(id2 >= 0 && id2 < NODES_MAX);
    node_id c1 = id1, c2 = id2; /* 'c' for cursor */
    if(forest[c1].sym != forest[c2].sym)
        return 0;
    switch(down2(&c1, &c2)) {
        case FAIL:
            return 0;
        case SAME_END:
            return 1;
        case SAME_FORWARD:
            break;
        default:
            ERROR("unreachable");
    }
    while(1) {
        if(forest[c1].sym != forest[c2].sym)
            return 0;
        switch(right2(&c1, &c2)) {
            case FAIL:
                return 0;
            case SAME_END: {
                leftmost2(&c1, &c2);
                switch(down2(&c1, &c2)) {
                    case FAIL:
                        return 0;
                    case SAME_END:
                        return 1;
                    case SAME_FORWARD:
                        break;
                    default:
                        ERROR("unreachable");
                }
            }

        }
        
    }
}



/*************************************************************************** */

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

    node_id ast = parse(tokens);

    graphviz("tokens.dot");
    free_tree(tokens);
    graphviz("ast.dot");
    graphviz_parent_sibling("ast2.dot");

    node_id rules = parse_rules(ast);
    free_tree(ast);
    graphviz("rules.dot");
    free_tree(rules);
    graphviz("leak.dot");
    if(free_list_ptr == NODES_MAX - 1)
        printf("all nodes freed successfully\n");
    else
        printf("%u nodes were not freed\n", NODES_MAX - free_list_ptr - 1);

    return 0;
}