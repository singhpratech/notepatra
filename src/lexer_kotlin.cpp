#include "lexer_kotlin.h"

const char *LexerKotlin::keywords(int set) const {
    if (set == 1) {
        return
            // Hard keywords (always reserved)
            "as break class continue do else false for fun if in interface "
            "is null object package return super this throw true try "
            "typealias typeof val var when while "
            // Soft keywords (reserved in some positions)
            "by catch constructor delegate dynamic field file finally get "
            "import init param property receiver set setparam value where "
            // Modifier keywords
            "abstract actual annotation companion const crossinline data "
            "enum expect external final infix inline inner internal "
            "lateinit noinline open operator out override private "
            "protected public reified sealed suspend tailrec vararg "
            // Identifier-like keywords used in expressions
            "it";
    }
    if (set == 2) {
        // Built-in / standard-library types Kotlin devs see daily.
        return
            "Any Boolean Byte Char Double Float Int Long Nothing Number "
            "Short String Unit "
            "Array ByteArray ShortArray IntArray LongArray FloatArray "
            "DoubleArray BooleanArray CharArray "
            "List MutableList ArrayList Set MutableSet HashSet "
            "Map MutableMap HashMap LinkedHashMap "
            "Pair Triple Sequence Iterable Iterator "
            "Result Throwable Exception RuntimeException";
    }
    return nullptr;
}
