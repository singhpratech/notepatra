// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_typescript.h"

const char *LexerTypeScript::keywords(int set) const {
    if (set == 1) {
        // Combines ECMAScript reserved words + TypeScript-specific keywords.
        // Sources verified against:
        //   github.com/microsoft/TypeScript/blob/main/src/compiler/scanner.ts
        //   typescriptlang.org/docs/handbook (utility types, type-from-types)
        return
            // ES standard reserved words
            "break case catch class const continue debugger default delete "
            "do else enum export extends false finally for from function "
            "if import in instanceof new null of return super switch this "
            "throw true try typeof var void while with yield "
            // ES contextual / strict-mode
            "let static await async as accessor "
            // TypeScript declaration / type-system keywords
            "abstract any asserts assert boolean constructor declare global "
            "implements infer interface intrinsic is keyof module namespace "
            "never number object out override package private protected "
            "public readonly require satisfies set string symbol type "
            "undefined unique unknown using bigint";
    }
    if (set == 2) {
        // Built-in types + utility types developers use daily.
        // Sources: lib.es5.d.ts, lib.es2015.iterable.d.ts, lib.dom.d.ts.
        return
            // Collections
            "Array ReadonlyArray Tuple Promise Map Set WeakMap WeakSet "
            // Iteration
            "Iterable Iterator IterableIterator Generator AsyncIterable "
            "AsyncIterator AsyncGenerator AsyncIterableIterator "
            // The 17+ canonical utility types (TS 5.x)
            "Record Partial Required Readonly Pick Omit Exclude Extract "
            "NonNullable Parameters ReturnType ConstructorParameters "
            "InstanceType Awaited Uppercase Lowercase Capitalize "
            "Uncapitalize NoInfer ThisType ThisParameterType "
            "OmitThisParameter "
            // Built-in objects
            "Function Object String Number Boolean BigInt Symbol RegExp "
            "Error TypeError RangeError SyntaxError ReferenceError "
            "EvalError URIError "
            // Common globals
            "Date JSON Math console window document globalThis "
            // Modern JS
            "Proxy Reflect Atomics SharedArrayBuffer ArrayBuffer DataView "
            // Typed arrays
            "Int8Array Uint8Array Uint8ClampedArray Int16Array Uint16Array "
            "Int32Array Uint32Array Float32Array Float64Array "
            "BigInt64Array BigUint64Array "
            // Common DOM types (only if .ts file is browser code)
            "HTMLElement Element Document Node Event Window NodeList "
            "EventTarget HTMLInputElement HTMLDivElement HTMLButtonElement";
    }
    if (set == 3) {
        // JSDoc / TSDoc tags used in /** */ comments. Maps to
        // SCE_C_COMMENTDOCKEYWORD style.
        return
            "param returns return throws example see deprecated since "
            "version author template typedef type property prop link "
            "default override readonly public private protected internal "
            "abstract sealed virtual async await yield generator "
            "this constructor module namespace category group "
            "remarks summary description todo fixme experimental beta "
            "inheritdoc";
    }
    return nullptr;
}
