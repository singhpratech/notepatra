// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_rust.h"

const char *LexerRust::keywords(int set) const {
    if (set == 1) {
        // Strict + reserved + 2024-edition keywords. Sources:
        //   https://doc.rust-lang.org/reference/keywords.html
        //   https://doc.rust-lang.org/edition-guide/rust-2024/gen-keyword.html
        return
            // Strict keywords (always reserved)
            "as async await break const continue crate dyn else enum extern "
            "false fn for if impl in let loop match mod move mut pub ref "
            "return Self self static struct super trait true try type union "
            "unsafe use where while yield "
            // Reserved keywords (reserved for future use)
            "abstract become box do final macro override priv typeof unsized "
            "virtual "
            // 2024 edition reservation
            "gen";
    }
    if (set == 2) {
        // Built-in primitive types + commonly-used std-lib types. Treated
        // as a "secondary keywords" set so themes can colour them
        // differently from control-flow keywords (matches rust-analyzer +
        // VS Code default style).
        return
            // Primitive types
            "bool char str String "
            "i8 i16 i32 i64 i128 isize "
            "u8 u16 u32 u64 u128 usize "
            "f32 f64 "
            // Common collections
            "Vec HashMap HashSet BTreeMap BTreeSet VecDeque LinkedList "
            // Smart pointers + cells
            "Box Rc Arc Cell RefCell Mutex RwLock Weak Pin "
            // Error / option / result helpers
            "Option Some None Result Ok Err "
            // Iteration traits
            "Iterator IntoIterator FromIterator DoubleEndedIterator "
            "ExactSizeIterator FusedIterator "
            // Conversion traits
            "From Into TryFrom TryInto AsRef AsMut FromStr ToString "
            "Deref DerefMut Borrow BorrowMut "
            // Marker traits
            "Send Sync Sized Copy Clone Drop Default Unpin "
            // Comparison + ordering traits
            "Debug Display Hash PartialEq Eq PartialOrd Ord "
            // Operators / arithmetic traits
            "Add Sub Mul Div Rem Neg Not BitAnd BitOr BitXor Shl Shr "
            "AddAssign SubAssign MulAssign DivAssign "
            // Concurrency
            "PhantomData PhantomPinned "
            // Common error
            "Error";
    }
    return nullptr;
}
