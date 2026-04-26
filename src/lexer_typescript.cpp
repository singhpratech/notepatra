#include "lexer_typescript.h"

const char *LexerTypeScript::keywords(int set) const {
    if (set == 1) {
        // JS keywords + TS-only additions. Listed in roughly ECMAScript
        // / TypeScript Handbook order to keep groups together.
        return
            // ES standard reserved words
            "break case catch class const continue debugger default delete "
            "do else enum export extends false finally for function if "
            "import in instanceof new null return super switch this throw "
            "true try typeof var void while with yield "
            // ES contextual / strict-mode
            "let static await async of "
            // TypeScript declaration keywords
            "abstract as asserts assert any boolean constructor declare "
            "from get global implements infer interface intrinsic is keyof "
            "module namespace never number object out override package "
            "private protected public readonly require satisfies set "
            "string symbol type undefined unique unknown using";
    }
    if (set == 2) {
        // Built-in types and utility types developers see daily.
        return
            "Array ReadonlyArray Tuple Promise Map Set WeakMap WeakSet "
            "Iterable Iterator Generator AsyncIterable AsyncIterator "
            "Record Partial Required Readonly Pick Omit Exclude Extract "
            "NonNullable Parameters ReturnType ConstructorParameters "
            "InstanceType Awaited Uppercase Lowercase Capitalize Uncapitalize "
            "Function Object String Number Boolean BigInt Symbol RegExp "
            "Error TypeError RangeError SyntaxError "
            "Date JSON Math console window document";
    }
    return nullptr;
}
