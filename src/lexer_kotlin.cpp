// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_kotlin.h"

const char *LexerKotlin::keywords(int set) const {
    if (set == 1) {
        // Comprehensive Kotlin keyword set (1.4 → 2.1, language stable
        // across these versions). Sources verified against:
        //   https://kotlinlang.org/docs/keyword-reference.html
        //   https://kotlinlang.org/spec/syntax-and-grammar.html
        return
            // Hard keywords (always reserved)
            "as break class continue do else false for fun if in interface "
            "is null object package return super this throw true try "
            "typealias typeof val var when while "
            // Soft keywords (reserved in some positions, identifiers elsewhere)
            "by catch constructor delegate dynamic field file finally get "
            "import init param property receiver set setparam value where "
            // Modifier keywords (modifiers in declaration context)
            "abstract actual annotation companion const crossinline data "
            "enum expect external final infix inline inner internal "
            "lateinit noinline open operator out override private "
            "protected public reified sealed suspend tailrec vararg "
            // Special identifiers used in expressions
            "it";
    }
    if (set == 2) {
        // Standard-library types Kotlin devs see daily. Includes:
        // - Primitives + nullable wrappers
        // - Array specialisations (incl. unsigned UByteArray etc.)
        // - Collection types
        // - Coroutine types (Job/Deferred/Flow/Channel/etc.)
        // - Result + exception hierarchy
        // - Common annotations (Deprecated, Suppress, JvmStatic, etc.)
        return
            // Primitives
            "Any Boolean Byte Char Double Float Int Long Nothing Number "
            "Short String Unit "
            // Unsigned (Kotlin 1.5+)
            "UByte UShort UInt ULong "
            // Array specialisations
            "Array ByteArray ShortArray IntArray LongArray FloatArray "
            "DoubleArray BooleanArray CharArray "
            "UByteArray UShortArray UIntArray ULongArray "
            // Collections
            "List MutableList ArrayList Collection MutableCollection "
            "Set MutableSet HashSet LinkedHashSet "
            "Map MutableMap HashMap LinkedHashMap "
            // Sequences + iterators
            "Sequence Iterable Iterator MutableIterator ListIterator "
            "MutableListIterator Pair Triple "
            "Comparable Comparator "
            // Coroutines (kotlinx.coroutines)
            "Job Deferred CoroutineScope CoroutineContext "
            "CoroutineDispatcher Dispatchers "
            "Flow MutableStateFlow StateFlow MutableSharedFlow SharedFlow "
            "Channel SendChannel ReceiveChannel "
            // Result + exceptions
            "Result Throwable Exception RuntimeException "
            "IllegalArgumentException IllegalStateException "
            "IndexOutOfBoundsException NullPointerException "
            "NoSuchElementException UnsupportedOperationException "
            "ClassCastException ArithmeticException NumberFormatException "
            "ConcurrentModificationException AssertionError Error "
            // Lazy + delegates
            "Lazy LazyThreadSafetyMode "
            // Common annotations
            "Deprecated DeprecationLevel Suppress JvmStatic JvmField "
            "JvmOverloads JvmName JvmMultifileClass JvmSynthetic Throws "
            "Volatile Synchronized Strictfp Transient JvmInline JvmRecord "
            "OptIn RequiresOptIn Experimental DslMarker PublishedApi "
            "SinceKotlin Target Retention Repeatable MustBeDocumented";
    }
    if (set == 3) {
        // KDoc tags. Maps to SCE_C_COMMENTDOCKEYWORD style.
        return
            "param return property receiver constructor throws exception "
            "sample see author since suppress";
    }
    return nullptr;
}
