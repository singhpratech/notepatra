/* Notepatra palette preview - synthetic; no real data
 * Exercises: includes, #define, struct, typedef, enum, switch/case, sizeof,
 * pointers, function pointers, _Atomic, _Generic, control flow, hex/bin literals.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#define MAX_ITEMS 16
#define SQUARE(x) ((x) * (x))

typedef enum {
    COLOR_RED = 0,
    COLOR_GREEN,
    COLOR_BLUE,
} Color;

typedef struct {
    int id;
    char name[32];
    double weight;
} Item;

typedef int (*BinaryOp)(int, int);

static _Atomic int g_counter = 0;

static int add(int a, int b) { return a + b; }
static int mul(int a, int b) { return a * b; }

#define type_name(x) _Generic((x), \
    int: "int", \
    double: "double", \
    char *: "string", \
    default: "other")

static const char *color_name(Color c) {
    switch (c) {
        case COLOR_RED:   return "red";
        case COLOR_GREEN: return "green";
        case COLOR_BLUE:  return "blue";
        default:          return "?";
    }
}

int main(void) {
    Item items[2] = {
        { .id = 1, .name = "alpha", .weight = 1.5 },
        { .id = 2, .name = "beta",  .weight = 2.5 },
    };
    int hex = 0xCAFE;
    int bin = 0b1010;

    BinaryOp ops[2] = { add, mul };
    for (size_t i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        printf("op[%zu] = %d\n", i, ops[i](3, 4));
    }

    atomic_fetch_add(&g_counter, 1);
    printf("counter=%d hex=%d bin=%d sq=%d\n",
           atomic_load(&g_counter), hex, bin, SQUARE(5));
    printf("color=%s type=%s\n", color_name(COLOR_BLUE), type_name(3.14));
    for (int i = 0; i < 2; ++i) {
        printf("item %d: %s w=%.2f\n", items[i].id, items[i].name, items[i].weight);
    }
    return EXIT_SUCCESS;
}
