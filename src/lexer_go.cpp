// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_go.h"

const char *LexerGo::keywords(int set) const {
    if (set == 1) {
        // Go has exactly 25 reserved keywords per the language spec
        // (https://go.dev/ref/spec#Keywords). Listed alphabetically.
        return
            "break case chan const continue default defer else fallthrough "
            "for func go goto if import interface map package range return "
            "select struct switch type var";
    }
    if (set == 2) {
        // Predeclared identifiers per https://go.dev/ref/spec#Predeclared_identifiers
        // — built-in types, constants, and functions. Includes Go 1.18+
        // additions (any, comparable) and Go 1.21+ promotions (clear,
        // min, max).
        return
            // Built-in types (Go 1.0)
            "bool byte complex64 complex128 error float32 float64 "
            "int int8 int16 int32 int64 rune string "
            "uint uint8 uint16 uint32 uint64 uintptr "
            // Generics-era type constraints (Go 1.18+)
            "any comparable "
            // Built-in constants
            "true false iota nil "
            // Built-in functions (Go 1.0 + 1.21 additions)
            "append cap clear close complex copy delete imag len make max "
            "min new panic print println real recover";
    }
    return nullptr;
}
