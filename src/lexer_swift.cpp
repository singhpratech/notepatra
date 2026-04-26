#include "lexer_swift.h"

const char *LexerSwift::keywords(int set) const {
    if (set == 1) {
        // Swift 6 keywords. Includes declarations, statements, expressions,
        // patterns, and access modifiers.
        return
            // Declaration keywords
            "associatedtype borrowing class consuming deinit enum extension "
            "fileprivate func import init inout internal let nonisolated "
            "open operator precedencegroup private protocol public package "
            "rethrows static struct subscript typealias var "
            // Statement keywords
            "break case catch continue default defer do else fallthrough "
            "for guard if in repeat return switch throw where while "
            // Expression and type keywords
            "Any as await false is nil rethrows self Self super throws "
            "true try "
            // Pattern keywords
            "_ "
            // Concurrency
            "actor async distributed isolated";
    }
    if (set == 2) {
        // Standard-library types Swift devs see daily.
        return
            "Bool Int Int8 Int16 Int32 Int64 UInt UInt8 UInt16 UInt32 UInt64 "
            "Float Double Float80 String Substring Character "
            "Optional Array ContiguousArray ArraySlice Dictionary Set "
            "Range ClosedRange CountableRange CountableClosedRange "
            "Result Sequence Collection Iterator Comparable Equatable "
            "Hashable Codable Decodable Encodable Identifiable "
            "AnyObject AnyClass Void Never";
    }
    return nullptr;
}
