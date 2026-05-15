#include "lexer_extras.h"

// v0.1.55 — keyword tables for the bulk lexer additions. Each function
// returns the keyword set string for its language; the base lexer
// (CPP / Python / Ruby / Java / HTML / Bash / Properties / JSON) handles
// everything else (strings, numbers, comments, operators, folding) so
// behaviour is "highlight as if it were ${BASE}, but recognise the
// language's own reserved words". Two sets per language:
//   Set 1: primary keywords — control flow, declarations, modifiers
//   Set 2: types / builtins — for theme colour differentiation
// Anything else (set 3, 4, 5) falls through to the base lexer's defaults.

// ═══════════════════════════════════════════════════════════════════════
// Dart — Flutter / dart.dev. Reference: dart.dev/language/keywords.
// v0.1.55 — verified against the official keyword list (research agent).
// ═══════════════════════════════════════════════════════════════════════
const char *LexerDart::keywords(int set) const {
    if (set == 1) return
        "abstract as assert async await base break case catch class const "
        "continue covariant default deferred do dynamic else enum export "
        "extends extension external factory false final finally for Function "
        "get hide if implements import in interface is late library "
        "mixin new null of on operator part required rethrow "
        "return sealed set show static super switch sync this throw true "
        "try type typedef var void when while with yield "
        // Dart 3 macros preview — dart.dev/language/keywords
        "augment";
    if (set == 2) return
        "BigInt bool Comparable DateTime double Duration dynamic Error "
        "Exception Function Future int Iterable Iterator List Map Null num "
        "Object Pattern RegExp Runes Set Stream String StringBuffer Symbol "
        "Type Uri void Completer FutureOr Never "
        // Dart 3 records type — dart.dev/language/keywords
        "Record";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Solidity — Ethereum smart contracts. Reference: docs.soliditylang.org.
// v0.1.55 — verified against SolidityLexer.g4 grammar via research agent.
// Now includes all bytesN/intN/uintN width-suffixed type families plus
// the time/ether unit denominators that were missing pre-v0.1.55.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerSolidity::keywords(int set) const {
    if (set == 1) return
        "abstract anonymous as assembly assert at break calldata case catch "
        "constant constructor continue contract default delete do else "
        "emit enum error event external false fallback final for from "
        "function global hex if immutable import in indexed inline interface "
        "internal is layout let library memory modifier new override "
        "payable pragma private public pure receive require return returns "
        "revert static storage struct super switch this throw transient "
        "true try type unchecked unicode using var view virtual while yul "
        // Reserved-but-unused (still tokenised, useful for highlighting)
        "after alias apply auto byte copyof define final implements "
        "macro match mutable null of partial promise reference relocatable "
        "sealed sizeof supports typedef typeof "
        // Time / value units (look-like-keywords)
        "wei gwei ether seconds minutes hours days weeks years";
    if (set == 2) return
        "address bool fixed mapping string ufixed "
        // bytesN family
        "bytes bytes1 bytes2 bytes3 bytes4 bytes5 bytes6 bytes7 bytes8 "
        "bytes9 bytes10 bytes11 bytes12 bytes13 bytes14 bytes15 bytes16 "
        "bytes17 bytes18 bytes19 bytes20 bytes21 bytes22 bytes23 bytes24 "
        "bytes25 bytes26 bytes27 bytes28 bytes29 bytes30 bytes31 bytes32 "
        // intN family
        "int int8 int16 int24 int32 int40 int48 int56 int64 int72 int80 "
        "int88 int96 int104 int112 int120 int128 int136 int144 int152 "
        "int160 int168 int176 int184 int192 int200 int208 int216 int224 "
        "int232 int240 int248 int256 "
        // uintN family
        "uint uint8 uint16 uint24 uint32 uint40 uint48 uint56 uint64 uint72 "
        "uint80 uint88 uint96 uint104 uint112 uint120 uint128 uint136 "
        "uint144 uint152 uint160 uint168 uint176 uint184 uint192 uint200 "
        "uint208 uint216 uint224 uint232 uint240 uint248 uint256 "
        // Globals + builtins
        "msg block tx now self abi keccak256 sha256 sha3 ripemd160 "
        "ecrecover addmod mulmod gasleft selfdestruct suicide "
        // EIP-4844 / block globals — docs.soliditylang.org/en/latest/units-and-global-variables.html
        "blockhash blobhash";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Zig — ziglang.org. Reference: ziglang.org/documentation/master.
// v0.1.55 — verified against the official keyword reference (research
// agent). Note Zig has NO block comments by design.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerZig::keywords(int set) const {
    if (set == 1) return
        "addrspace align allowzero and anyframe anytype asm async await "
        "break callconv catch comptime const continue defer else "
        "enum errdefer error export extern fn for if inline linksection "
        "noalias noinline nosuspend opaque or orelse packed pub resume "
        "return struct suspend switch test threadlocal try union unreachable "
        "usingnamespace var volatile while";
    if (set == 2) return
        "anyerror anyframe anyopaque anytype bool noreturn type void "
        "true false null undefined "
        // C-interop primitive aliases (per official std)
        "c_char c_int c_long c_longdouble c_longlong c_short c_uint "
        "c_ulong c_ulonglong c_ushort "
        // Float types
        "f16 f32 f64 f80 f128 "
        // Signed integers
        "i8 i16 i32 i64 i128 isize "
        // Unsigned integers
        "u8 u16 u32 u64 u128 usize "
        // Compile-time integer/float
        "comptime_int comptime_float";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Vala — wiki.gnome.org/Projects/Vala. v0.1.55 — verified against the
// official Vala keyword reference (research agent). Adds requires/ensures
// (design-by-contract), unichar (32-bit UTF-32 codepoint type), va_list.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerVala::keywords(int set) const {
    if (set == 1) return
        "abstract as async base break case catch class const construct "
        "continue default delegate delete do dynamic else ensures enum "
        "errordomain extern false finally for foreach get global if in "
        "inline interface internal is lock namespace new null out override "
        "owned params private protected public ref requires return sealed "
        "set signal sizeof static struct switch this throw throws true "
        "try typeof unowned using value var virtual void weak while with yield";
    if (set == 2) return
        "bool char double float int int8 int16 int32 int64 long short "
        "size_t ssize_t string time_t uchar uint uint8 uint16 uint32 uint64 "
        "ulong unichar ushort va_list void "
        // GLib / GTK families (commonly imported)
        "GLib Gtk Gdk Gee Pango Cairo";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Hack — Facebook PHP variant. Reference: docs.hhvm.com/hack.
// v0.1.55 — verified against the official keywords reference (research
// agent). Adds modern Hack types (vec/dict/keyset/varray/darray/shape),
// inout, classname, newtype, where, invariant.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerHack::keywords(int set) const {
    if (set == 1) return
        "abstract as async attribute await break case catch category child "
        "class clone const continue default do echo else enum eval exit "
        "extends final finally for foreach function if implements include "
        "include_once inout interface invariant isset list namespace new "
        "newtype noreturn parent print private protected public require "
        "require_once required return self shape static super switch throw "
        "trait try tuple type unset use using where while yield "
        // Reserved-historic / future
        "and declare die elseif empty enddeclare endfor endforeach endif "
        "endswitch endwhile global goto instanceof insteadof or var xor";
    if (set == 2) return
        "arraykey bool classname darray dict dynamic float int keyset mixed "
        "nonnull noreturn nothing num shape string this varray vec void "
        // Modern Hack collections (commonly imported)
        "Vector Map Set ImmVector ImmMap ImmSet Pair Awaitable AsyncIterator "
        "Container KeyedContainer Traversable KeyedTraversable Stringish";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Julia — julialang.org. v0.1.55 — verified against the official Base
// reference (research agent). Adds isa/nothing as keywords, full
// AbstractFloat/AbstractString hierarchy, common stdlib helpers.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerJulia::keywords(int set) const {
    if (set == 1) return
        "abstract baremodule begin break catch const continue do "
        "else elseif end export false finally for function global if "
        "import in isa let local macro module mutable nothing primitive "
        "quote return struct true try type using where while "
        // Loop modifier + Julia 1.11 — docs.julialang.org/en/v1/base/base/#Keywords
        "outer public";
    if (set == 2) return
        "AbstractFloat AbstractString Any Array Bool Char Complex Dict "
        // Float16 — docs.julialang.org/en/v1/base/base/#Keywords
        "Float16 "
        "Float32 Float64 Function Int Int8 Int16 Int32 Int64 Int128 "
        "Integer Matrix Missing Nothing Number Pair Range Real Ref Set "
        "String Symbol Tuple UInt UInt8 UInt16 UInt32 UInt64 UInt128 Vector "
        "BigInt BigFloat Rational AbstractArray AbstractDict AbstractRange "
        "DataType Method Module Expr NamedTuple Channel Task "
        // Common stdlib helpers
        "append! collect convert deepcopy filter getfield hash include "
        "keys length map parse pop! print println push! reduce reverse "
        "setfield! size sort split string sum typeof values";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// R — r-project.org. Reference: cran.r-project.org/manuals.html.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerR::keywords(int set) const {
    if (set == 1) return
        "if else for while repeat break next return function "
        "TRUE FALSE NULL NA NA_integer_ NA_real_ NA_character_ NA_complex_ "
        "Inf NaN in";
    if (set == 2) return
        "c list vector matrix array data.frame factor "
        "numeric integer double character logical complex raw "
        "as.numeric as.integer as.character as.logical as.factor "
        "library require source attach detach Sys.getenv Sys.setenv "
        "summary print cat paste sprintf format mean median sd var "
        "sum prod min max range length nrow ncol dim colnames rownames "
        "apply sapply lapply mapply tapply do.call Reduce Map Filter "
        "lm glm aov t.test cor cor.test anova predict ggplot dplyr tidyr";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Protocol Buffers — protobuf.dev/reference/protobuf/proto3-spec/.
// v0.1.55 — verified against the official proto3 spec (research agent).
// Adds `edition`, `weak`, `inf`, `nan`, `option`, `public`, `stream`.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerProtobuf::keywords(int set) const {
    if (set == 1) return
        "edition enum extend extensions false group import inf map max "
        "message nan oneof option optional package public repeated required "
        "reserved returns rpc service stream syntax to true weak";
    if (set == 2) return
        "bool bytes double fixed32 fixed64 float int32 int64 sfixed32 "
        "sfixed64 sint32 sint64 string uint32 uint64 "
        // Well-Known Types
        "Any Empty Timestamp Duration FieldMask Struct Value ListValue "
        "BoolValue StringValue Int32Value Int64Value FloatValue DoubleValue "
        // Additional wrapper WKTs — protobuf.dev/programming-guides/proto3/
        "BytesValue UInt32Value UInt64Value";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// F# — fsharp.org. v0.1.55 — verified against the official F# keyword
// reference (research agent). Adds bang variants (let!/match!/return!/
// use!/yield!) plus OCaml-reserved keywords (mod/land/lor/lsl/lsr/lxor).
// ═══════════════════════════════════════════════════════════════════════
const char *LexerFSharp::keywords(int set) const {
    if (set == 1) return
        "abstract and as assert async await base begin class const default "
        "delegate do done downcast downto elif else end exception extern "
        "false finally fixed for fun function global if in inherit inline "
        "interface internal lazy let match member module mutable namespace "
        "new not null of open or override private public rec return select "
        "static struct then to true try type upcast use val void when "
        "while with yield "
        // Bang-variants — used in computation expressions
        "let! match! return! use! yield! "
        // OCaml-reserved (recognized for backward compat)
        "asr land lor lsl lsr lxor mod sig "
        // Reserved-future
        "break checked component constraint continue event external include "
        "mixin parallel process protected pure sealed tailcall trait virtual";
    if (set == 2) return
        "array bool byte char decimal double float float32 int int16 int32 "
        "int64 list nativeint obj option sbyte seq single string uint "
        "uint16 uint32 uint64 unativeint unit "
        "Async Choice Error IDisposable IEnumerable Lazy List Map None Ok "
        "Option Result Seq Set Some Tuple "
        // F# 4.5+ struct variants — learn.microsoft.com/.../fsharp/.../keyword-reference
        "ValueOption ValueTuple";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// HCL / Terraform — terraform.io. v0.1.55 — verified against official
// Terraform language reference + functions catalog (research agent).
// ═══════════════════════════════════════════════════════════════════════
const char *LexerHCL::keywords(int set) const {
    if (set == 1) return
        "backend connection content count data depends_on dynamic false "
        "for_each lifecycle locals module null output provider provisioner "
        "required_providers required_version resource source terraform "
        "true variable version "
        // Terraform-meta references
        "var local each path self "
        // Template directives — developer.hashicorp.com/terraform/language/syntax/configuration
        "if else endfor endif";
    if (set == 2) return
        "string number bool list map set tuple object any "
        // Built-in functions (full Terraform stdlib)
        "abs abspath alltrue anytrue base64decode base64encode base64gzip "
        "base64sha256 base64sha512 basename bcrypt can ceil chomp cidrhost "
        "cidrnetmask cidrsubnet cidrsubnets coalesce coalescelist compact "
        "concat contains csvdecode distinct element endswith fileexists "
        "filebase64 filebase64sha256 filebase64sha512 filemd5 fileset "
        "filesha1 filesha256 filesha512 flatten floor format formatdate "
        "formatlist indent index issensitive join jsondecode jsonencode "
        "keys length log lookup lower matchkeys max merge min nonsensitive "
        "one parseint pathexpand plantimestamp pow range regex regexall "
        "replace reverse rsadecrypt sensitive setintersection setproduct "
        "setsubtract setunion sha1 sha256 sha512 signum slice sort split "
        "startswith strcontains strrev substr sum templatefile templatestring "
        "timeadd timecmp timestamp title tobool tolist tomap tonumber toset "
        "tostring transpose trim trimprefix trimspace trimsuffix try type "
        "upper urlencode uuid uuidv5 values yamldecode yamlencode zipmap";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Apache Thrift IDL — thrift.apache.org/docs/idl. v0.1.55 — verified
// against the official IDL grammar (research agent). Adds host-language
// namespacing variants.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerThrift::keywords(int set) const {
    if (set == 1) return
        "async const cpp_include cpp_namespace enum exception extends "
        "include namespace oneway optional php_namespace py_module required "
        "senum service struct throws true false typedef union void xception";
    if (set == 2) return
        "binary bool byte double i8 i16 i32 i64 list map set string";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// GraphQL — spec.graphql.org/draft/. v0.1.55 — verified against the
// official spec (research agent). Adds `repeatable` (Oct 2021), keeps
// directive locations as set 2 alongside built-in scalars.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerGraphQL::keywords(int set) const {
    if (set == 1) return
        "directive enum extend false fragment implements input interface "
        "mutation null on query repeatable scalar schema subscription "
        "true type union";
    if (set == 2) return
        "Boolean Float ID Int String "
        // Directive locations (TitleCase elsewhere; SCREAMING_SNAKE_CASE here)
        "ARGUMENT_DEFINITION ENUM ENUM_VALUE FIELD FIELD_DEFINITION "
        "FRAGMENT_DEFINITION FRAGMENT_SPREAD INLINE_FRAGMENT "
        "INPUT_FIELD_DEFINITION INPUT_OBJECT INTERFACE MUTATION OBJECT "
        "QUERY SCALAR SCHEMA SUBSCRIPTION UNION VARIABLE_DEFINITION";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Python-family
// ═══════════════════════════════════════════════════════════════════════

// GDScript — Godot 4.x. v0.1.55 — verified against the official GDScript
// reference (research agent). Adds Godot 4 decorators (@export / @icon /
// @onready) and constants (INF/NAN/PI/TAU). Note: yield is removed in
// 4.x but still keyword-reserved.
const char *LexerGDScript::keywords(int set) const {
    if (set == 1) return
        "and as assert await break breakpoint class class_name const "
        "continue elif else enum extends false for func if in is match "
        "not null or pass preload return self signal static super true "
        "var void when while yield "
        // Godot 4.x decorators
        "@export @icon @onready "
        // Additional Godot 4 annotations + namespace
        // docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html
        "@rpc @tool @warning_ignore namespace";
    if (set == 2) return
        "AABB Array Basis bool Callable Color Dictionary float "
        // Constants
        "INF NAN PI TAU "
        "int NodePath null Object PackedByteArray PackedColorArray "
        "PackedFloat32Array PackedFloat64Array PackedInt32Array "
        "PackedInt64Array PackedStringArray PackedVector2Array "
        "PackedVector3Array PackedVector4Array Plane Quaternion Rect2 "
        // Integer-coord variants — docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_basics.html
        "Rect2i Vector4i "
        "RID Signal String StringName TAU Transform2D Transform3D "
        "Vector2 Vector2i Vector3 Vector3i Vector4 "
        // Common Node hierarchy
        "Node Node2D Node3D Resource RefCounted Variant";
    return nullptr;
}

// Nim — nim-lang.org. v0.1.55 — verified against the official manual
// (research agent). Operator-keywords (div/mod/shl/shr/xor/and/or/not)
// are reserved tokens, not symbols.
const char *LexerNim::keywords(int set) const {
    if (set == 1) return
        "addr and as asm bind block break case cast concept const continue "
        "converter defer discard distinct div do elif else end enum except "
        "export finally for from func if import in include interface is "
        "isnot iterator let macro method mixin mod nil not notin object "
        "of or out proc ptr raise ref return shl shr static template try "
        "tuple type using var when while xor yield";
    if (set == 2) return
        "array bool byte char cstring float float32 float64 int int8 int16 "
        "int32 int64 openArray pointer Rune seq set string tuple uint uint8 "
        "uint16 uint32 uint64 varargs void "
        // Common stdlib types
        "Table HashSet OrderedTable CountTable Option Future Channel "
        "Thread Lock";
    return nullptr;
}

// Cython — cython.readthedocs.io. v0.1.55 — verified against the official
// language_basics docs (research agent). bint is the Cython boolean.
const char *LexerCython::keywords(int set) const {
    if (set == 1) return
        "False None True and as assert async await break class continue "
        "def del elif else except finally for from global if import in is "
        "lambda nonlocal not or pass raise return try while with yield "
        // Cython-specific (beyond Python)
        "api cclass ccall cdef cfunc cimport const cpdef ctypedef except "
        "exceptval extern fused gil include inline noexcept nogil packed "
        "public readonly struct union enum volatile";
    if (set == 2) return
        "bint char double doublecomplex float floatcomplex int long "
        "longdouble longdoublecomplex longlong object Py_hash_t "
        "Py_ssize_t Py_UCS4 schar short signed size_t ssize_t uchar uint "
        "ulong ulonglong unsigned ushort void";
    return nullptr;
}

// Mojo — docs.modular.com/mojo. v0.1.55 — verified against the official
// manual (research agent). `let` is deprecated in modern Mojo but
// reserved; argument conventions inout/owned/borrowed/mut/out are
// contextual keywords (only legal in parameter lists).
const char *LexerMojo::keywords(int set) const {
    if (set == 1) return
        "False None True alias and as async await borrowed break capturing "
        "class continue def del elif else except finally fn for from global "
        "if import in inout is lambda let mut nonlocal not or out owned pass "
        "raise raises return struct trait try var while with yield "
        // Mojo 24.x reference-binding — docs.modular.com/mojo/manual/
        "ref "
        // Common decorators (highlighted as keywords in user perception)
        "@parameter @register_passable @value @always_inline @adaptive "
        "@fieldwise_init @staticmethod";
    if (set == 2) return
        "Bool DType Float16 Float32 Float64 Int Int8 Int16 Int32 Int64 "
        "LayoutTensor List None object SIMD String True False UInt UInt8 "
        "UInt16 UInt32 UInt64 "
        // Mojo-specific advanced types
        "StringRef Tensor TileTensor Buffer DynamicVector StaticTuple "
        "Pointer DTypePointer AnyType AnyRegType";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Ruby-family
// ═══════════════════════════════════════════════════════════════════════

// Crystal — crystal-lang.org. v0.1.55 — verified against compiler/lexer.cr
// (research agent). Question-mark identifiers (is_a?, nil?, responds_to?)
// and bang identifiers (gsub!) are part of the keyword name.
const char *LexerCrystal::keywords(int set) const {
    if (set == 1) return
        "abstract alias alignof annotation as as? asm begin break case "
        "class def defined? do else elsif end ensure enum extend false for "
        "fun if in include instance_alignof instance_sizeof is_a? lib "
        "macro module next nil nil? of offsetof out pointerof private "
        "protected require rescue responds_to? return select self sizeof "
        "struct super then true type typeof uninitialized union unless "
        "until verbatim when while with yield";
    if (set == 2) return
        "Array Bool Char Float32 Float64 Hash Int8 Int16 Int32 Int64 "
        "Int128 NamedTuple Nil Pointer Proc Range Regex Set Slice "
        "StaticArray String Symbol Tuple UInt8 UInt16 UInt32 UInt64 "
        "UInt128 Bytes Channel Fiber Reference Struct Object";
    return nullptr;
}

// Elixir — hexdocs.pm/elixir. v0.1.55 — verified against the official
// syntax reference (research agent). Strict reserved set is small (15
// words); the def* family are macros from Kernel, but we keep them as
// keywords for syntax-highlighting usability.
const char *LexerElixir::keywords(int set) const {
    if (set == 1) return
        // Strictly reserved
        "after and catch do else end false fn in nil not or rescue true when "
        // Practical keywords (Kernel macros — every highlighter does this)
        "case cond def defguard defguardp defimpl defmacro defmacrop "
        "defmodule defp defprotocol defstruct for if import quote raise "
        "receive require return throw try unless unquote unquote_splicing "
        "use with";
    if (set == 2) return
        "Agent Atom Binary Boolean Code Enum File Float Function GenServer "
        "Integer IO Kernel List Macro Map MapSet Module Node PID Port "
        "Process Range Reference Regex Stream String Supervisor System "
        "Task Tuple URI Date Time DateTime NaiveDateTime Application "
        "Keyword Path";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Java/JVM-family
// ═══════════════════════════════════════════════════════════════════════

// Scala 3 — docs.scala-lang.org/scala3/reference/syntax.html.
// v0.1.55 — verified (research agent). Soft keywords (as/derives/end/
// extension/infix/inline/opaque/open/transparent/using) are contextual
// but treated as set 2 for color differentiation.
const char *LexerScala::keywords(int set) const {
    if (set == 1) return
        "abstract case catch class def do else enum export extends false "
        "final finally for given if implicit import lazy match new null "
        "object override package private protected return sealed super "
        "then throw trait true try type val var while with yield";
    if (set == 2) return
        "Any AnyRef AnyVal Boolean Byte Char Double Float Int Long Nothing "
        "Null Short String Symbol Unit "
        // Common stdlib types
        "ArrayBuffer Either Failure Future Iterable Iterator Left List "
        "Map None Option Promise Right Seq Set Some Success Try Tuple1 "
        "Tuple2 Tuple3 Vector "
        // Soft keywords — contextual
        "as derives end extension infix inline opaque open transparent using";
    return nullptr;
}

// Groovy — groovy-lang.org/syntax.html. v0.1.55 — verified (research
// agent). Adds non-sealed (the only hyphenated JVM keyword), record/
// sealed/permits soft keywords.
const char *LexerGroovy::keywords(int set) const {
    if (set == 1) return
        "abstract assert break case catch class const continue def default "
        "do else enum extends final finally for goto if implements import "
        "instanceof interface native new non-sealed null package private "
        "protected public return static strictfp super switch synchronized "
        "this threadsafe throw throws transient try while "
        // JVM reserved — groovy-lang.org/syntax.html#_keywords
        "volatile "
        // Contextual / soft — fix typo: yield (switch-expression value)
        "as in permits record sealed trait var yield";
    if (set == 2) return
        "boolean byte char double false float int long short true "
        // Common JDK / GDK
        "BigDecimal BigInteger Boolean Byte Character Double Float Integer "
        "Long Number Object Short String StringBuffer StringBuilder "
        "GString Closure Range List Map Set "
        "println print sprintf";
    return nullptr;
}

// Apex (Salesforce) — developer.salesforce.com/docs/atlas.en-us.apexcode/.
// v0.1.55 — verified against the canonical reserved-words list (research
// agent). Includes SOQL/SOSL keywords reserved everywhere (not just
// inside [...]). Identifiers are case-insensitive.
const char *LexerApex::keywords(int set) const {
    if (set == 1) return
        "abstract activate and any array as asc assert autonomous begin "
        "break bulk by case cast catch class collect commit const "
        "continue convertcurrency default delete desc do else end enum "
        "exception exit export extends false final finally for from future "
        "global goto group having hint if implements import inner insert "
        "instanceof interface into join last_90_days last_month "
        "last_n_days last_week like limit loop merge new next_90_days "
        "next_month next_n_days next_week not null nulls of on or outer "
        "override package parallel pragma private protected public "
        "retrieve return returning rollback savepoint search select set "
        "sort stat static super switch synchronized system testmethod then "
        "this this_month this_week throw throws today tolabel tomorrow "
        "transaction transient trigger true try type undelete update "
        "upsert using virtual void webservice when where while yesterday";
    if (set == 2) return
        "bigdecimal blob boolean byte char decimal double float int "
        "integer list long map number object short string "
        "Account Contact Lead Opportunity Case User Profile Database "
        "SObject Schema Date Datetime Decimal Double Integer Long String "
        "Boolean ID Blob List Map Set System Trigger UserInfo Apex "
        "ApexPages Test JSON Limits Math Pattern Matcher Http HttpRequest "
        "HttpResponse Crypto EncodingUtil";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// HTML-family templating
// ═══════════════════════════════════════════════════════════════════════

// Jinja — jinja.palletsprojects.com.
const char *LexerJinja::keywords(int set) const {
    if (set == 1) return
        "and as block break call continue do else elif endblock endcall "
        "endfor endif endmacro endset endwith extends filter for from if "
        "import in include is macro not or pairs raw recursive scoped set "
        "with without true false None null";
    if (set == 2) return
        "abs attr batch capitalize center default dictsort escape filesizeformat "
        "first float forceescape format groupby indent int items join last length "
        "list lower map max min pprint random reject rejectattr replace reverse "
        "round safe select selectattr slice sort string striptags sum title "
        "tojson trim truncate unique upper urlencode urlize wordcount wordwrap";
    return nullptr;
}

// Liquid — Shopify. Reference: shopify.dev/api/liquid.
const char *LexerLiquid::keywords(int set) const {
    if (set == 1) return
        "assign break capture case comment continue cycle decrement echo "
        "elsif else endcapture endcase endcomment endfor endif endunless "
        "for if include increment layout liquid raw render section "
        "tablerow unless when with in true false nil empty";
    if (set == 2) return
        "abs append at_least at_most capitalize ceil compact concat date "
        "default divided_by downcase escape escape_once first floor join "
        "last lstrip map minus modulo newline_to_br plus prepend remove "
        "remove_first replace replace_first reverse round rstrip size "
        "slice sort sort_natural split strip strip_html strip_newlines "
        "times truncate truncatewords uniq upcase url_decode url_encode";
    return nullptr;
}

// Twig — Symfony. Reference: twig.symfony.com.
const char *LexerTwig::keywords(int set) const {
    if (set == 1) return
        "and apply autoescape block deprecated do else elseif embed "
        "endapply endblock endembed endfilter endfor endif endmacro "
        "endsandbox endset endspaceless endtrans endverbatim endwith "
        "extends filter flush for from if import in include is macro "
        "not or sandbox set spaceless trans use verbatim with true false null";
    if (set == 2) return
        "abs batch capitalize column convert_encoding country_name currency_name "
        "currency_symbol date date_modify default escape filter first format "
        "html_to_markdown inline_css inky_to_html join json_encode keys "
        "language_name last length locale_name lower map markdown_to_html "
        "merge nl2br number_format raw reduce replace reverse round slice "
        "slug sort spaceless split striptags timezone_name title trim "
        "u upper url_encode";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Shell-family
// ═══════════════════════════════════════════════════════════════════════

// Dockerfile — docs.docker.com/engine/reference/builder/. v0.1.55 —
// verified (research agent). Instructions case-insensitive but
// conventionally UPPERCASE.
const char *LexerDockerfile::keywords(int set) const {
    if (set == 1) return
        "ADD ARG CMD COPY ENTRYPOINT ENV EXPOSE FROM HEALTHCHECK LABEL "
        "MAINTAINER ONBUILD RUN SHELL STOPSIGNAL USER VOLUME WORKDIR AS";
    if (set == 2) return
        // Parser directives + sub-flags
        "check escape syntax NONE";
    return nullptr;
}

// Fish — fishshell.com.
const char *LexerFish::keywords(int set) const {
    if (set == 1) return
        "and begin break builtin case command continue contains count "
        "echo else end exec exit fish for function functions if not or "
        "read return set set_color status string switch test time true "
        "false while abbr alias bg cd complete dirh dirs disown emit "
        "eval export fg help history isatty jobs math nextd open popd "
        "prevd printf pushd pwd random source suspend trap type ulimit "
        "umask wait";
    return nullptr;
}

// Nushell — nushell.sh.
const char *LexerNushell::keywords(int set) const {
    if (set == 1) return
        "alias and as break case const continue def def-env do else "
        "export extern false for from hide if in let let-env loop match "
        "module mut not null or overlay register return source true try "
        "use where while xor "
        "where each filter map reduce select skip take sort group "
        "into to from get pick first last length count uniq sum avg min max";
    if (set == 2) return
        "any binary bool date duration filesize float int list nothing "
        "number range record string table cell-path closure";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// Properties / config-file lexers — minimal keyword sets; QsciLexerProperties
// already handles key=value, comments, sections. We override for visual
// distinction via the "language" tag.
// ═══════════════════════════════════════════════════════════════════════

const char *LexerToml::keywords(int set) const {
    if (set == 1) return "true false";
    return nullptr;
}

const char *LexerEnv::keywords(int set) const {
    if (set == 1) return "export true false";
    return nullptr;
}

const char *LexerGitignore::keywords(int set) const {
    Q_UNUSED(set)
    return nullptr;  // pure pattern syntax, no keywords
}

// ═══════════════════════════════════════════════════════════════════════
// JSON-family
// ═══════════════════════════════════════════════════════════════════════

// JSON5 — json5.org. JSON with comments, trailing commas, single quotes,
// unquoted keys. The base QsciLexerJSON tokeniser already handles JSON's
// strings/numbers/punctuation; the additional set 1 keywords are the JSON5
// constants the base treats as identifiers.
const char *LexerJson5::keywords(int set) const {
    // v0.1.55 — verified per json5.org spec (research agent). `undefined`
    // is NOT in JSON5; only Infinity / NaN / null / true / false. Signed
    // forms (+Infinity, -NaN) come via the unary +/- prefix, not separate
    // keywords.
    if (set == 1) return "false Infinity NaN null true";
    return nullptr;
}

// ═══════════════════════════════════════════════════════════════════════
// BibTeX — bibtex.org. Bibliography records for LaTeX. Format is
// @entrytype{citekey, field1={...}, field2={...}}. The base QsciLexerCPP
// handles {} braces, = assignments, and "..." strings well enough; we
// just teach it which words are entry types vs field names so they get
// distinct highlighting from the citekey identifiers.
// ═══════════════════════════════════════════════════════════════════════
const char *LexerBibTeX::keywords(int set) const {
    if (set == 1) return
        // Standard BibTeX entry types (with @ prefix in actual files,
        // but the lexer matches on the bare word after @).
        "article book booklet conference inbook incollection inproceedings "
        "manual mastersthesis misc phdthesis proceedings techreport "
        "unpublished "
        // BibLaTeX additions (commonly used in modern .bib files)
        "online electronic www patent collection mvbook bookinbook "
        "suppbook reference mvreference inreference periodical "
        "suppperiodical thesis report set xdata "
        // Special non-entry directives
        "string preamble comment";
    if (set == 2) return
        // Standard BibTeX field names
        "address annote author booktitle chapter crossref edition editor "
        "howpublished institution journal key month note number "
        "organization pages publisher school series title type volume year "
        // BibLaTeX additions
        "abstract addendum afterword annotation annotator authortype "
        "bookauthor bookpagination booksubtitle booktitleaddon "
        "chapterauthor commentator date doi eid eprint eprintclass "
        "eprinttype eventdate eventtitle file foreword holder hyphenation "
        "ids indextitle introduction isan isbn ismn isrn issn issue "
        "issuesubtitle issuetitle iswc journalsubtitle journaltitle "
        "label langid langidopts language library location mainsubtitle "
        "maintitle maintitleaddon nameaddon options origdate origlanguage "
        "origlocation origpublisher origtitle pagetotal pagination part "
        "pubstate reprinttitle shortauthor shorteditor shorthand "
        "shorthandintro shortjournal shortseries shorttitle subtitle "
        "titleaddon translator url urldate venue version volumes "
        // Common namespaced extensions
        "keywords keyword tags";
    return nullptr;
}
