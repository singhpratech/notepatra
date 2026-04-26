#include "lexer_go.h"

const char *LexerGo::keywords(int set) const {
    if (set == 1) {
        // Go has exactly 25 keywords -- one of the smallest keyword sets
        // of any modern language. Listed alphabetically per Go spec.
        return
            "break case chan const continue default defer else fallthrough "
            "for func go goto if import interface map package range return "
            "select struct switch type var";
    }
    if (set == 2) {
        // Predeclared identifiers: built-in types, constants, and
        // functions that Go treats as part of the language.
        return
            // Built-in types
            "bool byte complex64 complex128 error float32 float64 "
            "int int8 int16 int32 int64 rune string "
            "uint uint8 uint16 uint32 uint64 uintptr "
            "any comparable "
            // Built-in constants
            "true false iota nil "
            // Built-in functions
            "append cap close complex copy delete imag len make new panic "
            "print println real recover min max clear";
    }
    return nullptr;
}
