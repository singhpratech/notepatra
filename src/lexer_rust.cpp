#include "lexer_rust.h"

const char *LexerRust::keywords(int set) const {
    if (set == 1) {
        // Rust language keywords -- everything that affects control flow,
        // declarations, modifiers, and reserved words. List taken from the
        // Rust Reference v1.85.
        return
            "as async await break const continue crate do dyn else enum "
            "extern false fn for if impl in let loop match mod move mut "
            "pub ref return Self self static struct super trait true try "
            "type union unsafe use where while yield "
            // 2024 edition reserved
            "abstract become box final macro override priv typeof unsized "
            "virtual";
    }
    if (set == 2) {
        // Built-in primitive types + commonly-used std types. Treated as
        // a "secondary keywords" set so themes can colour them differently
        // from control-flow keywords (matches VS Code rust-analyzer style).
        return
            "bool char str String i8 i16 i32 i64 i128 isize u8 u16 u32 u64 "
            "u128 usize f32 f64 "
            "Vec HashMap HashSet BTreeMap BTreeSet VecDeque LinkedList "
            "Option Result Box Rc Arc Cell RefCell Mutex RwLock "
            "Iterator IntoIterator FromIterator "
            "PhantomData PhantomPinned "
            "Send Sync Sized Copy Clone Drop Default "
            "Ok Err Some None";
    }
    return nullptr;
}
