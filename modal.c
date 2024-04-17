#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char interned_strings_buffer[0x1000], *next_interned = interned_strings_buffer;

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
    if(next_interned + len - interned_strings_buffer > sizeof(interned_strings_buffer)) {  /* TODO: > or >= ?? */
        fprintf(stderr, "Out of memory for interned strings\n");
        exit(1);
    }
    if(string_count >= sizeof(strings) / sizeof(strings[0])) {
        fprintf(stderr, "Out of room for a new symbol\n");
    }
    memcpy(next_interned, s, len);
    strings[string_count] = (string){.ptr = next_interned, .len = len};
    next_interned += len;
    return string_count++;
}

void sym_print(symbol s) {
    if(s >= string_count) {
        fprintf(stderr, "invalid symbol\n");
        exit(1);
    }
    for(size_t i = 0; i < strings[s].len; i++)
        printf("%c", strings[s].ptr[i]);
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(stderr, "usage: %s file.modal\n", argv[0]);
        return 1;
    }

    intern("<>", 2);
    intern("(", 1);
    intern(")", 1);
    intern("hello", 5);
    intern("world", 5);

    for(int i = 0; i < string_count; i++) {
        sym_print(i);
        printf("\n");
    }

    /*
    FILE *f = fopen(argv[1], "r");
    if(!f) {
        perror("fopen");
        return 1;
    }
    fclose(f);
    */

    return 0;
}