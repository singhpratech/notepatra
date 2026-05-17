// SPDX-License-Identifier: GPL-3.0-or-later

// ─────────────────────────────────────────────────────────────────────────
//  lang_keywords.h — comprehensive primary-source keyword lists for every
//  language Notepatra supports beyond SQL (which lives in sql_keywords.h).
//
//  Synthesised 2026-05-15 by parallel primary-source research agents
//  (5 agents covering ~50 languages) for the v0.1.84 palette overhaul.
//  Each section cites its primary URL and lists RESERVED keywords, TYPES,
//  BUILTINS (where applicable), plus per-language deltas vs current
//  src/lexer_extras.cpp keyword overrides.
//
//  Wiring intent: lexerutils.cpp calls
//      lex->SendScintilla(QsciScintilla::SCI_SETKEYWORDS, slot, str);
//  at lexer-attach time. Slot 0 = primary keywords, slot 1 = secondary
//  (types/builtins), slot 4 = "User defined 1" (routed to npKeyword2 by
//  the matcher chain in npp_palette.cpp).
//
//  All identifiers are in canonical case for the language (Rust lowercase,
//  Java/Kotlin lowercase, C# mixed, PowerShell PascalCase-hyphen, etc.).
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace notepatra::langkw {

// ═══════════════════════════════════════════════════════════════════════
// SYSTEMS / COMPILED LANGUAGES
// ═══════════════════════════════════════════════════════════════════════

// ───── RUST (doc.rust-lang.org/reference/keywords.html) ─────
inline constexpr const char *kRustKW =
    "as async await break const continue crate dyn else enum extern false fn "
    "for if impl in let loop match mod move mut pub ref return self Self "
    "static struct super trait true type unsafe use where while "
    "abstract become box do final gen macro override priv try typeof unsized virtual yield "
    "macro_rules raw safe union";
inline constexpr const char *kRustTypes =
    "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize f32 f64 bool char str "
    "String Vec Option Result Box Rc Arc Cell RefCell Mutex RwLock HashMap "
    "HashSet BTreeMap BTreeSet VecDeque LinkedList Cow PhantomData Pin Future "
    "Iterator IntoIterator FromIterator Send Sync Sized Copy Clone Default "
    "Drop Fn FnMut FnOnce Ord PartialOrd Eq PartialEq Hash Debug Display Error";
inline constexpr const char *kRustBuiltins =
    "println eprintln print eprint vec format write writeln assert assert_eq "
    "assert_ne debug_assert debug_assert_eq debug_assert_ne panic todo "
    "unimplemented unreachable include include_str include_bytes concat "
    "stringify env option_env file line column module_path cfg cfg_attr "
    "compile_error format_args dbg matches Some None Ok Err";

// ───── GO (go.dev/ref/spec#Keywords + #Predeclared_identifiers) ─────
inline constexpr const char *kGoKW =
    "break case chan const continue default defer else fallthrough for func go "
    "goto if import interface map package range return select struct switch type var";
inline constexpr const char *kGoTypes =
    "any bool byte comparable complex64 complex128 error float32 float64 int "
    "int8 int16 int32 int64 rune string uint uint8 uint16 uint32 uint64 uintptr";
inline constexpr const char *kGoBuiltins =
    "true false iota nil "
    "append cap clear close complex copy delete imag len make max min new "
    "panic print println real recover";

// ───── C (en.cppreference.com/w/c/keyword — fallback Wikipedia C/C23) ─────
inline constexpr const char *kCKW =
    "auto break case char const continue default do double else enum extern "
    "float for goto if int long register return short signed sizeof static "
    "struct switch typedef union unsigned void volatile while "
    "inline restrict _Bool _Complex _Imaginary "
    "_Alignas _Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local "
    "alignas alignof bool constexpr false nullptr static_assert thread_local "
    "true typeof typeof_unqual _BitInt _Decimal32 _Decimal64 _Decimal128";
inline constexpr const char *kCTypes =
    "size_t ptrdiff_t intptr_t uintptr_t intmax_t uintmax_t "
    "int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t "
    "int_least8_t int_least16_t int_least32_t int_least64_t "
    "uint_least8_t uint_least16_t uint_least32_t uint_least64_t "
    "int_fast8_t int_fast16_t int_fast32_t int_fast64_t "
    "uint_fast8_t uint_fast16_t uint_fast32_t uint_fast64_t "
    "wchar_t char16_t char32_t time_t clock_t FILE va_list nullptr_t";
inline constexpr const char *kCBuiltins =
    "NULL EOF stdin stdout stderr errno "
    "printf fprintf sprintf snprintf scanf fscanf sscanf "
    "malloc calloc realloc free memcpy memmove memset memcmp "
    "strlen strcpy strncpy strcat strncat strcmp strncmp strchr strrchr strstr "
    "fopen fclose fread fwrite fseek ftell fgetc fputc fgets fputs "
    "exit abort assert offsetof";
inline constexpr const char *kCPreproc =
    "define undef include if ifdef ifndef else elif elifdef elifndef endif "
    "line error warning pragma embed defined __has_include __has_c_attribute "
    "__has_embed";

// ───── C++ (isocpp.org/wiki/faq/keywords) ─────
inline constexpr const char *kCppKW =
    "alignas alignof asm auto bool break case catch char char8_t char16_t "
    "char32_t class concept const consteval constexpr constinit const_cast "
    "continue co_await co_return co_yield decltype default delete do double "
    "dynamic_cast else enum explicit export extern false float for friend goto "
    "if inline int long mutable namespace new noexcept nullptr operator "
    "private protected public register reinterpret_cast requires return short "
    "signed sizeof static static_assert static_cast struct switch template "
    "this thread_local throw true try typedef typeid typename union unsigned "
    "using virtual void volatile wchar_t while "
    "final override import module";
inline constexpr const char *kCppTypes =
    "std string string_view wstring u8string u16string u32string "
    "vector array deque list forward_list map unordered_map set unordered_set "
    "multimap multiset stack queue priority_queue span pair tuple optional "
    "variant any expected unique_ptr shared_ptr weak_ptr "
    "size_t ptrdiff_t nullptr_t int8_t int16_t int32_t int64_t "
    "uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t intmax_t uintmax_t "
    "ostream istream iostream stringstream fstream ifstream ofstream "
    "thread mutex condition_variable atomic future promise function "
    "initializer_list type_info exception runtime_error logic_error";
inline constexpr const char *kCppBuiltins =
    "cout cin cerr clog endl flush "
    "move forward swap make_pair make_tuple make_unique make_shared "
    "begin end cbegin cend rbegin rend size empty data "
    "find find_if for_each transform accumulate sort stable_sort copy "
    "static_pointer_cast dynamic_pointer_cast const_pointer_cast";

// ───── C# (learn.microsoft.com/.../csharp/.../keywords/) ─────
inline constexpr const char *kCSharpKW =
    "abstract as base bool break byte case catch char checked class const "
    "continue decimal default delegate do double else enum event explicit "
    "extern false finally fixed float for foreach goto if implicit in int "
    "interface internal is lock long namespace new null object operator out "
    "override params private protected public readonly ref return sbyte sealed "
    "short sizeof stackalloc static string struct switch this throw true try "
    "typeof uint ulong unchecked unsafe ushort using virtual void volatile while "
    "add allows alias and ascending args async await by descending dynamic "
    "equals extension field file from get global group init into join let "
    "managed nameof nint not notnull nuint on or orderby partial record remove "
    "required scoped select set unmanaged value var when where with yield";
inline constexpr const char *kCSharpTypes =
    "Object String Int32 Int64 Int16 Byte SByte UInt32 UInt64 UInt16 Single "
    "Double Decimal Boolean Char DateTime DateTimeOffset TimeSpan Guid "
    "List Dictionary HashSet Queue Stack IEnumerable IEnumerator ICollection "
    "IList IDictionary IReadOnlyList IReadOnlyDictionary Task ValueTask "
    "Action Func Predicate Nullable Span ReadOnlySpan Memory ReadOnlyMemory "
    "Exception ArgumentException InvalidOperationException NullReferenceException";

// ───── JAVA (docs.oracle.com/javase/specs/jls/se21/.../jls-3.html) ─────
inline constexpr const char *kJavaKW =
    "abstract assert boolean break byte case catch char class const continue "
    "default do double else enum extends final finally float for goto if "
    "implements import instanceof int interface long native new package "
    "private protected public return short static strictfp super switch "
    "synchronized this throw throws transient try void volatile while "
    "exports module open opens provides requires to transitive uses with "
    "sealed permits non-sealed record var yield "
    "true false null";
inline constexpr const char *kJavaTypes =
    "String Object Integer Long Short Byte Float Double Boolean Character "
    "Number BigInteger BigDecimal Math System "
    "List ArrayList LinkedList Map HashMap TreeMap LinkedHashMap Set HashSet "
    "TreeSet LinkedHashSet Collection Iterator Iterable Comparable Comparator "
    "Optional Stream Collectors Function Predicate Consumer Supplier "
    "Exception RuntimeException IllegalArgumentException NullPointerException "
    "Thread Runnable Throwable Error";

// ───── KOTLIN (kotlinlang.org/docs/keyword-reference.html) ─────
inline constexpr const char *kKotlinKW =
    "as break class continue do else false for fun if in interface is null "
    "object package return super this throw true try typealias typeof val var "
    "when while "
    "by catch constructor delegate dynamic field file finally get import init "
    "param property receiver set setparam value where "
    "abstract actual annotation companion const crossinline data enum expect "
    "external final infix inline inner internal lateinit noinline open "
    "operator out override private protected public reified sealed suspend "
    "tailrec vararg";
inline constexpr const char *kKotlinTypes =
    "Any Unit Nothing String Char Boolean Byte Short Int Long Float Double "
    "UByte UShort UInt ULong Array IntArray LongArray ShortArray ByteArray "
    "FloatArray DoubleArray BooleanArray CharArray "
    "List MutableList ArrayList Set MutableSet HashSet Map MutableMap HashMap "
    "Pair Triple Sequence Iterable Iterator Collection MutableCollection "
    "Throwable Exception RuntimeException Error Comparable Number Enum";

// ───── SWIFT (docs.swift.org/.../lexicalstructure/) ─────
inline constexpr const char *kSwiftKW =
    "associatedtype class deinit enum extension fileprivate func import init "
    "inout internal let open operator private precedencegroup protocol public "
    "rethrows static struct subscript typealias var "
    "break case continue default defer do else fallthrough for guard if in "
    "repeat return switch where while "
    "Any as catch false is nil rethrows self Self super throw throws true try "
    "await async _ "
    "associativity convenience didSet dynamic final get indirect infix lazy "
    "left mutating none nonmutating optional override postfix precedence "
    "prefix Protocol required right set Type unowned weak willSet";
inline constexpr const char *kSwiftTypes =
    "Int Int8 Int16 Int32 Int64 UInt UInt8 UInt16 UInt32 UInt64 Float Float32 "
    "Float64 Double Bool String Character Substring StaticString "
    "Array Dictionary Set Optional Result Range ClosedRange "
    "AnyObject AnyClass Void Never Error CustomStringConvertible Hashable "
    "Equatable Comparable Codable Encodable Decodable Sequence Collection "
    "Iterator IteratorProtocol Sendable Identifiable";

// ───── ZIG (ziglang.org/documentation/master/#Keyword-Reference) ─────
inline constexpr const char *kZigKW =
    "addrspace align allowzero and anyframe anytype asm async await break "
    "callconv catch comptime const continue defer else enum errdefer error "
    "export extern fn for if inline linksection noalias noinline nosuspend "
    "opaque or orelse packed pub resume return struct suspend switch test "
    "threadlocal try union unreachable usingnamespace var volatile while";
inline constexpr const char *kZigTypes =
    "i8 i16 i32 i64 i128 isize u8 u16 u32 u64 u128 usize "
    "c_char c_short c_ushort c_int c_uint c_long c_ulong c_longlong "
    "c_ulonglong c_longdouble "
    "f16 f32 f64 f80 f128 bool void noreturn type anyerror anyopaque "
    "comptime_int comptime_float true false null undefined";

// ───── D (dlang.org/spec/lex.html#keywords) ─────
inline constexpr const char *kDKW =
    "abstract alias align asm assert auto body bool break byte case cast "
    "catch cdouble cent cfloat char class const continue creal dchar debug "
    "default delegate delete deprecated do double else enum export extern "
    "false final finally float for foreach foreach_reverse function goto "
    "idouble if ifloat immutable import in inout int interface invariant "
    "ireal is lazy long macro mixin module new nothrow null out override "
    "package pragma private protected public pure real ref return scope "
    "shared short static struct super switch synchronized template this "
    "throw true try typeid typeof ubyte ucent uint ulong union unittest "
    "ushort version void wchar while with";

// ═══════════════════════════════════════════════════════════════════════
// DYNAMIC / SCRIPTING LANGUAGES
// ═══════════════════════════════════════════════════════════════════════

// ───── PYTHON (docs.python.org/3/reference/lexical_analysis.html) ─────
inline constexpr const char *kPythonKW =
    "False None True and as assert async await break class continue def del "
    "elif else except finally for from global if import in is lambda match "
    "nonlocal not or pass raise return try while with yield case type _";
inline constexpr const char *kPythonBuiltins =
    "abs aiter all anext any ascii bin bool breakpoint bytearray bytes "
    "callable chr classmethod compile complex delattr dict dir divmod "
    "enumerate eval exec filter float format frozenset getattr globals "
    "hasattr hash help hex id input int isinstance issubclass iter len list "
    "locals map max memoryview min next object oct open ord pow print "
    "property range repr reversed round set setattr slice sorted staticmethod "
    "str sum super tuple type vars zip __import__";
inline constexpr const char *kPythonTyping =
    "Any Callable Dict List Tuple Set FrozenSet Optional Union Type Iterable "
    "Iterator Generator Sequence Mapping MutableMapping MutableSequence "
    "Awaitable Coroutine AsyncIterable AsyncIterator Literal Final ClassVar "
    "Annotated TypedDict Protocol TypeVar NewType NoReturn Never Self "
    "LiteralString TypeAlias Concatenate ParamSpec Unpack Required NotRequired";

// ───── JAVASCRIPT (tc39.es/ecma262/#sec-keywords-and-reserved-words) ─────
inline constexpr const char *kJavaScriptKW =
    "break case catch class const continue debugger default delete do else "
    "export extends finally for function if import in instanceof new return "
    "super switch this throw try typeof var void while with yield "
    "enum await "
    "implements interface package private protected public let static "
    "as async from get of set target meta arguments eval";
inline constexpr const char *kJavaScriptConst =
    "true false null undefined NaN Infinity globalThis";
inline constexpr const char *kJavaScriptBuiltins =
    "Object Function Boolean Symbol Error EvalError RangeError ReferenceError "
    "SyntaxError TypeError URIError Number BigInt Math Date String RegExp "
    "Array Int8Array Uint8Array Uint8ClampedArray Int16Array Uint16Array "
    "Int32Array Uint32Array Float32Array Float64Array BigInt64Array "
    "BigUint64Array Map Set WeakMap WeakSet WeakRef FinalizationRegistry "
    "ArrayBuffer SharedArrayBuffer DataView Atomics JSON Promise Proxy "
    "Reflect Generator AsyncFunction Intl console parseInt parseFloat isNaN "
    "isFinite decodeURI decodeURIComponent encodeURI encodeURIComponent";

// ───── TYPESCRIPT (extends JavaScript) ─────
inline constexpr const char *kTypeScriptExtraKW =
    "abstract any as asserts bigint boolean constructor declare from get "
    "global infer is keyof module namespace never number object out override "
    "readonly require satisfies set string symbol type undefined unique "
    "unknown using accessor intrinsic";
inline constexpr const char *kTypeScriptTypes =
    "any boolean number string symbol object void never unknown bigint "
    "undefined null Array ReadonlyArray Promise Record Partial Required "
    "Readonly Pick Omit Exclude Extract NonNullable ReturnType Parameters "
    "ConstructorParameters InstanceType ThisType Awaited Uppercase Lowercase "
    "Capitalize Uncapitalize";

// ───── RUBY (docs.ruby-lang.org/en/master/keywords_rdoc.html) ─────
inline constexpr const char *kRubyKW =
    "__ENCODING__ __FILE__ __LINE__ BEGIN END alias and begin break case "
    "class def defined? do else elsif end ensure false for if in module next "
    "nil not or redo rescue retry return self super then true undef unless "
    "until when while yield";
inline constexpr const char *kRubyBuiltins =
    "puts print p pp gets readline readlines require require_relative load "
    "attr_accessor attr_reader attr_writer raise throw catch lambda proc "
    "loop sleep at_exit autoload binding block_given? caller eval exec exit "
    "exit! fork format fail open Array Hash Integer Float String Symbol "
    "Rational Complex Range";

// ───── PHP (php.net/manual/en/reserved.keywords.php) ─────
inline constexpr const char *kPhpKW =
    "__halt_compiler abstract and array as break callable case catch class "
    "clone const continue declare default die do echo else elseif empty "
    "enddeclare endfor endforeach endif endswitch endwhile enum eval exit "
    "extends final finally fn for foreach function global goto if implements "
    "include include_once instanceof insteadof interface isset list match "
    "namespace new or print private protected public readonly require "
    "require_once return static switch throw trait try unset use var while "
    "xor yield";
inline constexpr const char *kPhpTypes =
    "int float bool string true false null void iterable object mixed never "
    "numeric resource self parent static";
inline constexpr const char *kPhpBuiltins =
    "echo print isset unset empty count strlen strpos substr str_replace "
    "explode implode trim sprintf printf var_dump print_r json_encode "
    "json_decode array_keys array_values array_map array_filter array_merge "
    "array_push array_pop in_array array_search file_get_contents "
    "file_put_contents fopen fclose fread fwrite preg_match preg_match_all "
    "preg_replace date time strtotime intval floatval strval is_array "
    "is_string is_numeric is_null is_bool defined define";

// ───── PERL (perldoc.perl.org/functions) ─────
inline constexpr const char *kPerlKW =
    "if elsif else unless while until for foreach do last next redo return "
    "sub my our local state use no require package BEGIN END UNITCHECK CHECK "
    "INIT and or not xor eq ne lt gt le ge cmp x q qq qw qr m s tr y";

// ───── LUA 5.4 (lua.org/manual/5.4/manual.html#3.1) ─────
inline constexpr const char *kLuaKW =
    "and break do else elseif end false for function goto if in local nil "
    "not or repeat return then true until while";
inline constexpr const char *kLuaBuiltins =
    "assert collectgarbage dofile error getmetatable ipairs load loadfile "
    "next pairs pcall print rawequal rawget rawlen rawset require select "
    "setmetatable tonumber tostring type warn xpcall _G _VERSION coroutine "
    "debug io math os package string table utf8";

// ───── BASH (gnu.org/software/bash/manual/html_node/Reserved-Words.html) ─────
inline constexpr const char *kBashKW =
    "! [[ ]] { } case coproc do done elif else esac fi for function if in "
    "select then time until while";
inline constexpr const char *kBashBuiltins =
    ": . [ alias bg bind break builtin caller cd command compgen complete "
    "compopt continue declare dirs disown echo enable eval exec exit export "
    "false fc fg getopts hash help history jobs kill let local logout "
    "mapfile popd printf pushd pwd read readarray readonly return set shift "
    "shopt source suspend test times trap true type typeset ulimit umask "
    "unalias unset wait";

// ───── POWERSHELL (learn.microsoft.com/.../about_language_keywords) ─────
inline constexpr const char *kPowerShellKW =
    "begin break catch class clean continue data define do dynamicparam else "
    "elseif end enum exit filter finally for foreach from function hidden if "
    "in param process return static switch throw trap try until using var "
    "while inlinescript parallel sequence workflow";
inline constexpr const char *kPowerShellCmdlets =
    "Add-Content Add-Member Add-Type Clear-Content Clear-Host Clear-Item "
    "Clear-Variable Compare-Object ConvertFrom-Csv ConvertFrom-Json "
    "ConvertTo-Csv ConvertTo-Json ConvertTo-Html Copy-Item Export-Csv "
    "ForEach-Object Format-List Format-Table Format-Wide Get-Acl Get-Alias "
    "Get-ChildItem Get-Command Get-Content Get-Credential Get-Date Get-Help "
    "Get-History Get-Host Get-Item Get-ItemProperty Get-Job Get-Location "
    "Get-Member Get-Module Get-Process Get-PSDrive Get-PSSession Get-Random "
    "Get-Service Get-Variable Group-Object Import-Csv Import-Module "
    "Invoke-Command Invoke-Expression Invoke-Item Invoke-RestMethod "
    "Invoke-WebRequest Join-Path Measure-Object Move-Item New-Alias New-Item "
    "New-Module New-Object New-PSSession New-Variable Out-File Out-GridView "
    "Out-Host Out-Null Out-String Pop-Location Push-Location Read-Host "
    "Receive-Job Remove-Item Remove-Module Remove-Variable Rename-Item "
    "Resolve-Path Restart-Service Resume-Service Select-Object Select-String "
    "Send-MailMessage Set-Alias Set-Content Set-Item Set-ItemProperty "
    "Set-Location Set-Variable Sort-Object Split-Path Start-Job Start-Process "
    "Start-Service Start-Sleep Stop-Job Stop-Process Stop-Service Tee-Object "
    "Test-Connection Test-Path Trace-Command Update-TypeData Wait-Job "
    "Where-Object Write-Debug Write-Error Write-Host Write-Information "
    "Write-Output Write-Progress Write-Verbose Write-Warning";

// ───── COFFEESCRIPT (coffeescript.org/#reserved-words) ─────
inline constexpr const char *kCoffeeKW =
    "break by catch class const continue debugger default delete do else "
    "extends false finally for if in instanceof new null of return super "
    "switch then this throw true try typeof undefined unless until var void "
    "when while with yield "
    "and or is isnt not yes no on off loop own";

// ───── TCL 8.6 (tcl-lang.org/man/tcl8.6/TclCmd/contents.htm) ─────
inline constexpr const char *kTclBuiltins =
    "after append apply array binary break catch cd chan clock close concat "
    "continue coroutine dict encoding eof error eval exec exit expr fblocked "
    "fconfigure fcopy file fileevent flush for foreach format gets glob "
    "global history if incr info interp join lappend lassign lindex linsert "
    "list llength lmap load lrange lrepeat lreplace lreverse lsearch lset "
    "lsort namespace next nextto open package pid proc puts pwd read "
    "refchan regexp registry regsub rename return scan seek set socket "
    "source split string subst switch tailcall tell throw time trace try "
    "unknown unload unset update uplevel upvar variable vwait while yield "
    "yieldto zlib";

// ═══════════════════════════════════════════════════════════════════════
// NICHE / MODERN LANGUAGES (with delta notes vs current lexer_extras.cpp)
// ═══════════════════════════════════════════════════════════════════════

// ───── DART (dart.dev/language/keywords)
//        DELTA: ADD `augment`, `Record`
inline constexpr const char *kDartKW =
    "abstract as assert async augment await base break case catch class const "
    "continue covariant default deferred do dynamic else enum export extends "
    "extension external factory false final finally for Function get hide if "
    "implements import in interface is late library mixin new null of on "
    "operator part required rethrow return sealed set show static super "
    "switch sync this throw true try type typedef var void when while with yield";
inline constexpr const char *kDartTypes =
    "BigInt bool Comparable DateTime double Duration dynamic Error Exception "
    "Function Future FutureOr int Iterable Iterator List Map Never Null num "
    "Object Pattern Record RegExp Runes Set Stream String StringBuffer "
    "Symbol Type Uri void Completer";

// ───── SOLIDITY (docs.soliditylang.org/en/latest/grammar.html)
//        DELTA: ADD `blockhash`, `blobhash` (EIP-4844)
inline constexpr const char *kSolidityKW =
    "abstract anonymous as assembly break calldata case catch constant "
    "constructor continue contract default delete do else emit enum error "
    "event external fallback false for from function hex if immutable import "
    "in indexed interface internal is let library mapping memory modifier new "
    "override payable pragma private public pure receive require return "
    "returns revert storage struct switch this throw true try type unchecked "
    "unicode using view virtual while yul";
inline constexpr const char *kSolidityBuiltins =
    "address abi block msg tx now blockhash blobhash gasleft keccak256 sha256 "
    "ripemd160 ecrecover addmod mulmod selfdestruct wei gwei ether seconds "
    "minutes hours days weeks years";

// ───── JULIA (docs.julialang.org/en/v1/base/base/#Keywords)
//        DELTA: ADD `outer`, `public` (1.11)
inline constexpr const char *kJuliaKW =
    "abstract baremodule begin break catch const continue do else elseif end "
    "export false finally for function global if import in isa let local "
    "macro module mutable outer primitive public quote return struct true "
    "try type using where while";
inline constexpr const char *kJuliaTypes =
    "AbstractFloat AbstractString Any Array Bool Char Complex Dict Float16 "
    "Float32 Float64 Function Int Int8 Int16 Int32 Int64 Int128 Integer "
    "Matrix Missing Nothing Number Pair Range Real Ref Set String Symbol "
    "Tuple UInt UInt8 UInt16 UInt32 UInt64 UInt128 Vector BigInt BigFloat "
    "Rational AbstractArray AbstractDict AbstractRange DataType Method "
    "Module Expr NamedTuple Channel Task";

// ───── R (stat.ethz.ch/.../Reserved.html)
//        DELTA: ADD `repeat`, `return` (both currently MISSING — real bug)
inline constexpr const char *kRKW =
    "if else repeat while function for in next break TRUE FALSE NULL Inf "
    "NaN NA NA_integer_ NA_real_ NA_complex_ NA_character_ return";
inline constexpr const char *kRBuiltins =
    "c list vector matrix array data.frame factor as.numeric as.integer "
    "as.character as.logical as.factor library require source attach detach "
    "summary print cat paste sprintf format mean median sd var sum prod min "
    "max range length nrow ncol dim apply sapply lapply mapply tapply "
    "do.call Reduce Map Filter";

// ───── PROTOBUF proto3 (protobuf.dev/programming-guides/proto3/)
//        DELTA: ADD WKT siblings (BytesValue, UInt32Value, UInt64Value)
inline constexpr const char *kProtoKW =
    "edition enum extend extensions false group import inf map max message "
    "nan oneof option optional package public repeated required reserved "
    "returns rpc service stream syntax to true weak";
inline constexpr const char *kProtoTypes =
    "bool bytes double fixed32 fixed64 float int32 int64 sfixed32 sfixed64 "
    "sint32 sint64 string uint32 uint64 Any Empty Timestamp Duration "
    "FieldMask Struct Value ListValue BoolValue StringValue Int32Value "
    "Int64Value FloatValue DoubleValue BytesValue UInt32Value UInt64Value";

// ───── F# (learn.microsoft.com/.../fsharp/.../keyword-reference)
//        DELTA: ADD ValueOption, ValueTuple
inline constexpr const char *kFSharpKW =
    "abstract and as assert async await base begin class const default "
    "delegate do done downcast downto elif else end exception extern false "
    "finally fixed for fun function global if in inherit inline interface "
    "internal lazy let match member module mutable namespace new not null of "
    "open or override private public rec return select static struct then to "
    "true try type upcast use val void when while with yield "
    "let! match! return! use! yield! asr land lor lsl lsr lxor mod sig";
inline constexpr const char *kFSharpTypes =
    "array bool byte char decimal double float float32 int int16 int32 int64 "
    "list nativeint obj option sbyte seq single string uint uint16 uint32 "
    "uint64 unativeint unit Async Choice Error IDisposable IEnumerable Lazy "
    "List Map None Ok Option Result Seq Set Some Tuple ValueOption ValueTuple";

// ───── SCALA 3 (docs.scala-lang.org/scala3/reference/syntax.html) ─────
inline constexpr const char *kScalaKW =
    "abstract case catch class def do else enum export extends false final "
    "finally for given if implicit import lazy match new null object override "
    "package private protected return sealed super then throw trait true try "
    "type val var while with yield "
    "as derives end extension infix inline opaque open transparent using";
inline constexpr const char *kScalaTypes =
    "Any AnyRef AnyVal Boolean Byte Char Double Float Int Long Nothing Null "
    "Short String Symbol Unit ArrayBuffer Either Failure Future Iterable "
    "Iterator Left List Map None Option Promise Right Seq Set Some Success "
    "Try Tuple1 Tuple2 Tuple3 Vector";

// ───── GROOVY (groovy-lang.org/syntax.html#_keywords)
//        DELTA: FIX `yields` typo → `yield`; ADD `volatile`, `this`
inline constexpr const char *kGroovyKW =
    "abstract assert break case catch class const continue def default do "
    "else enum extends final finally for goto if implements import "
    "instanceof interface native new non-sealed null package private "
    "protected public return static strictfp super switch synchronized this "
    "throw throws transient try void volatile while "
    "as in permits record sealed trait var yield";

// ───── GDSCRIPT (docs.godotengine.org/.../gdscript_basics.html)
//        DELTA: ADD @rpc, @tool, @warning_ignore, namespace, Rect2i, Vector4i
inline constexpr const char *kGDScriptKW =
    "and as assert await break breakpoint class class_name const continue "
    "elif else enum extends false for func if in is match namespace not null "
    "or pass preload return self signal static super true var void when while "
    "yield @export @icon @onready @rpc @tool @warning_ignore";
inline constexpr const char *kGDScriptTypes =
    "AABB Array Basis bool Callable Color Dictionary float INF NAN PI TAU "
    "int NodePath null Object PackedByteArray PackedColorArray "
    "PackedFloat32Array PackedFloat64Array PackedInt32Array PackedInt64Array "
    "PackedStringArray PackedVector2Array PackedVector3Array "
    "PackedVector4Array Plane Quaternion Rect2 Rect2i RID Signal String "
    "StringName Transform2D Transform3D Vector2 Vector2i Vector3 Vector3i "
    "Vector4 Vector4i Node Node2D Node3D Resource RefCounted Variant";

// ───── CRYSTAL (crystal-lang.org/reference/.../syntax_and_semantics/)
//        DELTA: ADD `next`, `until` (both currently MISSING — real bug)
inline constexpr const char *kCrystalKW =
    "abstract alias alignof annotation as as? asm begin break case class def "
    "defined? do else elsif end ensure enum extend false for fun if in include "
    "instance_alignof instance_sizeof is_a? lib macro module next nil nil? "
    "of offsetof out pointerof private protected require rescue responds_to? "
    "return select self sizeof struct super then true type typeof "
    "uninitialized union unless until verbatim when while with yield";

// ───── ELIXIR (hexdocs.pm/elixir/main/syntax-reference.html) ─────
inline constexpr const char *kElixirKW =
    "after and catch do else end false fn in nil not or rescue true when "
    "case cond def defguard defguardp defimpl defmacro defmacrop defmodule "
    "defp defprotocol defstruct for if import quote raise receive require "
    "return throw try unless unquote unquote_splicing use with";

// ───── MOJO (docs.modular.com/mojo/manual/)
//        DELTA: ADD `ref` (Mojo 24.x reference binding)
inline constexpr const char *kMojoKW =
    "False None True alias and as async await borrowed break capturing class "
    "continue def del elif else except finally fn for from global if import "
    "in inout is lambda let mut nonlocal not or out owned pass raise raises "
    "ref return struct trait try var while with yield "
    "@parameter @register_passable @value @always_inline @adaptive "
    "@fieldwise_init @staticmethod";

// ───── HCL / TERRAFORM (developer.hashicorp.com/terraform/language/syntax/)
//        DELTA: ADD template directives `if`, `else`, `endfor`, `endif`
inline constexpr const char *kHclKW =
    "backend connection content count data depends_on dynamic else endfor "
    "endif false for for_each if in lifecycle locals module null output "
    "provider provisioner required_providers required_version resource "
    "source terraform true variable version var local each path self";

// ───── GRAPHQL (spec.graphql.org/draft/) ─────
inline constexpr const char *kGraphqlKW =
    "directive enum extend false fragment implements input interface mutation "
    "null on query repeatable scalar schema subscription true type union";
inline constexpr const char *kGraphqlTypes =
    "Boolean Float ID Int String";

// ═══════════════════════════════════════════════════════════════════════
// MARKUP / DATA / CONFIG
// ═══════════════════════════════════════════════════════════════════════

// ───── HTML5 (html.spec.whatwg.org/multipage/indices.html#elements-3) ─────
inline constexpr const char *kHtmlTags =
    "a abbr address area article aside audio b base bdi bdo blockquote body "
    "br button canvas caption cite code col colgroup data datalist dd del "
    "details dfn dialog div dl dt em embed fieldset figcaption figure footer "
    "form h1 h2 h3 h4 h5 h6 head header hgroup hr html i iframe img input "
    "ins kbd label legend li link main map mark menu meta meter nav noscript "
    "object ol optgroup option output p picture pre progress q rp rt ruby s "
    "samp script search section select slot small source span strong style "
    "sub summary sup table tbody td template textarea tfoot th thead time "
    "title tr track u ul var video wbr";
inline constexpr const char *kHtmlAttrs =
    "accept accept-charset accesskey action allow allowfullscreen alt as "
    "async autocapitalize autocomplete autofocus autoplay charset checked "
    "cite class color cols colspan content contenteditable controls coords "
    "crossorigin data datetime decoding default defer dir dirname disabled "
    "download draggable enctype enterkeyhint fetchpriority for form "
    "formaction formenctype formmethod formnovalidate formtarget headers "
    "height hidden href hreflang id inputmode integrity is ismap itemid "
    "itemprop itemref itemscope itemtype kind label lang list loading loop "
    "max maxlength media method min minlength multiple muted name nomodule "
    "nonce novalidate open pattern ping placeholder poster preload readonly "
    "referrerpolicy rel required reversed role rows rowspan sandbox scope "
    "selected shape sizes slot spellcheck src srcdoc srclang srcset start "
    "step style tabindex target title translate type usemap value width wrap";

// ───── CSS (w3.org/Style/CSS/all-properties.en.html) ─────
// ~370 properties — full W3C index. (~5KB string; trimmed-to-readable.)
inline constexpr const char *kCssProperties =
    "accent-color align-content align-items align-self alignment-baseline all "
    "anchor-name anchor-scope animation animation-composition animation-delay "
    "animation-direction animation-duration animation-fill-mode "
    "animation-iteration-count animation-name animation-play-state "
    "animation-range animation-range-end animation-range-start "
    "animation-timeline animation-timing-function appearance aspect-ratio "
    "backface-visibility background background-attachment "
    "background-blend-mode background-clip background-color background-image "
    "background-origin background-position background-position-x "
    "background-position-y background-repeat background-size baseline-shift "
    "baseline-source block-size border border-block border-block-color "
    "border-block-end border-block-end-color border-block-end-style "
    "border-block-end-width border-block-start border-block-start-color "
    "border-block-start-style border-block-start-width border-block-style "
    "border-block-width border-bottom border-bottom-color "
    "border-bottom-left-radius border-bottom-right-radius border-bottom-style "
    "border-bottom-width border-collapse border-color border-end-end-radius "
    "border-end-start-radius border-image border-image-outset "
    "border-image-repeat border-image-slice border-image-source "
    "border-image-width border-inline border-inline-color border-inline-end "
    "border-inline-end-color border-inline-end-style border-inline-end-width "
    "border-inline-start border-inline-start-color border-inline-start-style "
    "border-inline-start-width border-inline-style border-inline-width "
    "border-left border-left-color border-left-style border-left-width "
    "border-radius border-right border-right-color border-right-style "
    "border-right-width border-spacing border-start-end-radius "
    "border-start-start-radius border-style border-top border-top-color "
    "border-top-left-radius border-top-right-radius border-top-style "
    "border-top-width border-width bottom box-decoration-break box-shadow "
    "box-sizing break-after break-before break-inside caption-side "
    "caret-color clear clip clip-path clip-rule color color-interpolation "
    "color-rendering color-scheme column-count column-fill column-gap "
    "column-rule column-rule-color column-rule-style column-rule-width "
    "column-span column-width columns contain contain-intrinsic-block-size "
    "contain-intrinsic-height contain-intrinsic-inline-size "
    "contain-intrinsic-size contain-intrinsic-width container container-name "
    "container-type content content-visibility counter-increment counter-reset "
    "counter-set cursor cx cy d direction display dominant-baseline "
    "empty-cells fill fill-opacity fill-rule filter flex flex-basis "
    "flex-direction flex-flow flex-grow flex-shrink flex-wrap float "
    "flood-color flood-opacity font font-family font-feature-settings "
    "font-kerning font-language-override font-optical-sizing font-palette "
    "font-size font-size-adjust font-stretch font-style font-synthesis "
    "font-variant font-variant-alternates font-variant-caps "
    "font-variant-east-asian font-variant-emoji font-variant-ligatures "
    "font-variant-numeric font-variant-position font-variation-settings "
    "font-weight forced-color-adjust gap grid grid-area grid-auto-columns "
    "grid-auto-flow grid-auto-rows grid-column grid-column-end "
    "grid-column-start grid-row grid-row-end grid-row-start grid-template "
    "grid-template-areas grid-template-columns grid-template-rows "
    "hanging-punctuation height hyphenate-character hyphenate-limit-chars "
    "hyphens image-orientation image-rendering image-resolution "
    "initial-letter inline-size inset inset-block inset-block-end "
    "inset-block-start inset-inline inset-inline-end inset-inline-start "
    "isolation justify-content justify-items justify-self left letter-spacing "
    "lighting-color line-break line-clamp line-height line-height-step "
    "list-style list-style-image list-style-position list-style-type margin "
    "margin-block margin-block-end margin-block-start margin-bottom "
    "margin-inline margin-inline-end margin-inline-start margin-left "
    "margin-right margin-top margin-trim marker marker-end marker-mid "
    "marker-start mask mask-border mask-border-mode mask-border-outset "
    "mask-border-repeat mask-border-slice mask-border-source "
    "mask-border-width mask-clip mask-composite mask-image mask-mode "
    "mask-origin mask-position mask-repeat mask-size mask-type math-depth "
    "math-shift math-style max-block-size max-height max-inline-size "
    "max-lines max-width min-block-size min-height min-inline-size min-width "
    "mix-blend-mode object-fit object-position offset offset-anchor "
    "offset-distance offset-path offset-position offset-rotate opacity order "
    "orphans outline outline-color outline-offset outline-style outline-width "
    "overflow overflow-anchor overflow-block overflow-clip-margin "
    "overflow-inline overflow-wrap overflow-x overflow-y overscroll-behavior "
    "overscroll-behavior-block overscroll-behavior-inline overscroll-behavior-x "
    "overscroll-behavior-y padding padding-block padding-block-end "
    "padding-block-start padding-bottom padding-inline padding-inline-end "
    "padding-inline-start padding-left padding-right padding-top page "
    "page-break-after page-break-before page-break-inside paint-order "
    "perspective perspective-origin place-content place-items place-self "
    "pointer-events position print-color-adjust quotes r resize right "
    "rotate row-gap ruby-align ruby-position rx ry scale scroll-behavior "
    "scroll-margin scroll-margin-block scroll-margin-block-end "
    "scroll-margin-block-start scroll-margin-bottom scroll-margin-inline "
    "scroll-margin-inline-end scroll-margin-inline-start scroll-margin-left "
    "scroll-margin-right scroll-margin-top scroll-padding "
    "scroll-padding-block scroll-padding-block-end scroll-padding-block-start "
    "scroll-padding-bottom scroll-padding-inline scroll-padding-inline-end "
    "scroll-padding-inline-start scroll-padding-left scroll-padding-right "
    "scroll-padding-top scroll-snap-align scroll-snap-stop scroll-snap-type "
    "scrollbar-color scrollbar-gutter scrollbar-width shape-image-threshold "
    "shape-margin shape-outside shape-rendering stop-color stop-opacity "
    "stroke stroke-dasharray stroke-dashoffset stroke-linecap stroke-linejoin "
    "stroke-miterlimit stroke-opacity stroke-width tab-size table-layout "
    "text-align text-align-last text-anchor text-combine-upright "
    "text-decoration text-decoration-color text-decoration-line "
    "text-decoration-skip text-decoration-skip-ink text-decoration-style "
    "text-decoration-thickness text-emphasis text-emphasis-color "
    "text-emphasis-position text-emphasis-style text-indent text-justify "
    "text-orientation text-overflow text-rendering text-shadow "
    "text-size-adjust text-transform text-underline-offset "
    "text-underline-position text-wrap text-wrap-mode text-wrap-style top "
    "touch-action transform transform-box transform-origin transform-style "
    "transition transition-behavior transition-delay transition-duration "
    "transition-property transition-timing-function translate unicode-bidi "
    "user-select vector-effect vertical-align view-timeline view-timeline-axis "
    "view-timeline-inset view-timeline-name view-transition-name visibility "
    "white-space white-space-collapse widows width will-change word-break "
    "word-spacing word-wrap writing-mode x y z-index zoom";
inline constexpr const char *kCssAtRules =
    "@charset @import @namespace @media @supports @document @page @font-face "
    "@keyframes @counter-style @font-feature-values @property @layer "
    "@container @scope @starting-style @position-try @view-transition "
    "@font-palette-values @color-profile";
inline constexpr const char *kCssPseudoClasses =
    ":active :any-link :autofill :blank :checked :current :default :defined "
    ":dir :disabled :empty :enabled :first :first-child :first-of-type :focus "
    ":focus-visible :focus-within :fullscreen :future :has :host :host-context "
    ":hover :in-range :indeterminate :invalid :is :lang :last-child "
    ":last-of-type :left :link :local-link :modal :not :nth-child :nth-col "
    ":nth-last-child :nth-last-col :nth-last-of-type :nth-of-type :only-child "
    ":only-of-type :optional :out-of-range :past :paused :picture-in-picture "
    ":placeholder-shown :playing :popover-open :read-only :read-write "
    ":required :right :root :scope :state :target :target-within "
    ":user-invalid :user-valid :valid :visited :where";

// ───── YAML 1.2 (yaml.org/spec/1.2.2/) ─────
inline constexpr const char *kYamlKW =
    "true false null yes no on off Yes No On Off True False Null TRUE FALSE "
    "NULL YES NO ON OFF ~";

// ───── TOML (toml.io/en/v1.0.0) ─────
inline constexpr const char *kTomlKW =
    "true false inf nan +inf -inf +nan -nan";

// ───── JSON / JSON5 ─────
inline constexpr const char *kJsonKW = "true false null";
inline constexpr const char *kJson5KW =
    "true false null Infinity -Infinity NaN -NaN undefined";

// ───── DOCKERFILE (docs.docker.com/reference/dockerfile/) ─────
inline constexpr const char *kDockerfileKW =
    "FROM RUN CMD LABEL MAINTAINER EXPOSE ENV ADD COPY ENTRYPOINT VOLUME "
    "USER WORKDIR ARG ONBUILD STOPSIGNAL HEALTHCHECK SHELL";

// ───── MAKEFILE (gnu.org/software/make/manual/) ─────
inline constexpr const char *kMakefileKW =
    "ifeq ifneq ifdef ifndef else endif define endef undefine include "
    "-include sinclude override export unexport private vpath load";
inline constexpr const char *kMakefileBuiltins =
    "subst patsubst strip findstring filter filter-out sort word wordlist "
    "words firstword lastword dir notdir suffix basename addsuffix addprefix "
    "join wildcard realpath abspath if or and foreach call eval file value "
    "origin flavor shell error warning info guile";

// ───── FISH (fishshell.com/docs/current/language.html) ─────
inline constexpr const char *kFishKW =
    "if else switch case while for in begin end function return break "
    "continue and or not";
inline constexpr const char *kFishBuiltins =
    "set read echo printf test string math count contains command builtin "
    "type status jobs fg bg exec eval source abbr alias bind complete history "
    "funced funcsave functions help exit trap cd pwd";

// ───── NUSHELL (nushell.sh/book/) ─────
inline constexpr const char *kNushellKW =
    "def let mut const if else match for while loop break continue return "
    "try catch do source use export module alias hide register overlay";
inline constexpr const char *kNushellBuiltins =
    "ls cd pwd where select get each par-each reduce sort-by group-by filter "
    "first last drop skip take length append prepend str math into format "
    "save open echo print hash http url date ansi char history help exit "
    "columns headers reverse rename insert update upsert reject default "
    "flatten describe range from to compact uniq enumerate";

// ───── BIBTEX ─────
inline constexpr const char *kBibtexEntries =
    "@article @book @booklet @conference @inbook @incollection @inproceedings "
    "@manual @mastersthesis @misc @phdthesis @proceedings @techreport "
    "@unpublished @string @preamble @comment";
inline constexpr const char *kBibtexFields =
    "address annote author booktitle chapter crossref edition editor "
    "howpublished institution journal key month note number organization "
    "pages publisher school series title type volume year doi isbn issn url "
    "eprint archiveprefix primaryclass abstract keywords";

// ───── MATLAB / OCTAVE ─────
inline constexpr const char *kMatlabKW =
    "break case catch classdef continue else elseif end for function global "
    "if otherwise parfor persistent return spmd switch try while";

// ───── SYSTEMVERILOG (IEEE 1800-2017 Annex B) ─────
inline constexpr const char *kSystemVerilogKW =
    "accept_on alias always always_comb always_ff always_latch and assert "
    "assign assume automatic before begin bind bins binsof bit break buf "
    "bufif0 bufif1 byte case casex casez cell chandle checker class clocking "
    "cmos config const constraint context continue cover covergroup "
    "coverpoint cross deassign default defparam design disable dist do edge "
    "else end endcase endchecker endclass endclocking endconfig endfunction "
    "endgenerate endgroup endinterface endmodule endpackage endprimitive "
    "endprogram endproperty endsequence endspecify endtable endtask enum "
    "event eventually expect export extends extern final first_match for "
    "force foreach forever fork forkjoin function generate genvar global "
    "highz0 highz1 if iff ifnone ignore_bins illegal_bins implements implies "
    "import incdir include initial inout input inside instance int integer "
    "interconnect interface intersect join join_any join_none large let "
    "liblist library local localparam logic longint macromodule matches "
    "medium modport module nand negedge nettype new nexttime nmos nor "
    "noshowcancelled not notif0 notif1 null or output package packed "
    "parameter pmos posedge primitive priority program property protected "
    "pull0 pull1 pulldown pullup pulsestyle_ondetect pulsestyle_onevent pure "
    "rand randc randcase randsequence rcmos real realtime ref reg reject_on "
    "release repeat restrict return rnmos rpmos rtran rtranif0 rtranif1 "
    "s_always s_eventually s_nexttime s_until s_until_with scalared sequence "
    "shortint shortreal showcancelled signed small soft solve specify "
    "specparam static string strong strong0 strong1 struct super supply0 "
    "supply1 sync_accept_on sync_reject_on table tagged task this throughout "
    "time timeprecision timeunit tran tranif0 tranif1 tri tri0 tri1 triand "
    "trior trireg type typedef union unique unique0 unsigned until "
    "until_with untyped use uwire var vectored virtual void wait wait_order "
    "wand weak weak0 weak1 while wildcard wire with within wor xnor xor";

}  // namespace notepatra::langkw
