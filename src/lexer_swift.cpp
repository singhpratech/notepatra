// SPDX-License-Identifier: GPL-3.0-or-later

#include "lexer_swift.h"

const char *LexerSwift::keywords(int set) const {
    if (set == 1) {
        // Swift 6 keywords. Comprehensive list from
        // https://docs.swift.org/swift-book/documentation/the-swift-programming-language/lexicalstructure/
        // Categories: declarations, statements, expressions/types,
        // patterns, concurrency (Swift 5.5+), macros (Swift 5.9+),
        // ownership (SE-0377 / Swift 5.9+: borrowing/consuming).
        return
            // Declaration keywords
            "associatedtype borrowing class consuming deinit enum extension "
            "fileprivate func import init inout internal let macro "
            "nonisolated open operator package precedencegroup private "
            "protocol public rethrows static struct subscript typealias var "
            // Statement keywords
            "break case catch continue default defer do else fallthrough "
            "for guard if in repeat return switch throw where while "
            // Expression / type keywords
            "Any as await false is nil rethrows self Self super throws "
            "true try "
            // Pattern keywords
            "_ "
            // Concurrency (Swift 5.5+)
            "actor async distributed isolated "
            // Generic + opaque-type modifiers
            "any each some "
            // Ownership (Swift 5.9+)
            "sending consume copy discard "
            // Common contextual keywords (kept for highlighting consistency)
            "convenience didSet dynamic final get indirect infix lazy "
            "left mutating nonmutating optional override postfix prefix "
            "Protocol required right set Type unowned weak willSet";
    }
    if (set == 2) {
        // Standard-library types Swift devs see daily — comprehensive list
        // from developer.apple.com/documentation/swift.
        return
            // Primitives
            "Bool Int Int8 Int16 Int32 Int64 UInt UInt8 UInt16 UInt32 UInt64 "
            "Float Double Float16 Float80 String Substring Character "
            "StaticString Unicode "
            // Collections
            "Array ContiguousArray ArraySlice Dictionary Set "
            "Sequence Collection BidirectionalCollection RandomAccessCollection "
            "MutableCollection RangeReplaceableCollection "
            "LazySequence LazyCollection "
            // Ranges + Optional + Result
            "Optional Range ClosedRange CountableRange CountableClosedRange "
            "PartialRangeFrom PartialRangeUpTo PartialRangeThrough Stride "
            "Result "
            // Iteration
            "Iterator IteratorProtocol AsyncIterator AsyncSequence "
            "AsyncStream AsyncThrowingStream "
            // Concurrency types
            "Task TaskGroup ThrowingTaskGroup MainActor GlobalActor Actor "
            // Common protocols
            "Comparable Equatable Hashable Codable Decodable Encodable "
            "Identifiable CustomStringConvertible CustomDebugStringConvertible "
            "LosslessStringConvertible "
            "ExpressibleByStringLiteral ExpressibleByIntegerLiteral "
            "ExpressibleByArrayLiteral ExpressibleByDictionaryLiteral "
            "ExpressibleByNilLiteral ExpressibleByBooleanLiteral "
            "ExpressibleByFloatLiteral "
            "RawRepresentable CaseIterable OptionSet "
            "Error LocalizedError "
            "Sendable AnyObject AnyClass AnyHashable "
            // Numeric protocols
            "Numeric BinaryInteger SignedInteger UnsignedInteger "
            "FixedWidthInteger BinaryFloatingPoint FloatingPoint SIMD "
            // Pointers
            "UnsafePointer UnsafeMutablePointer UnsafeRawPointer "
            "UnsafeMutableRawPointer UnsafeBufferPointer "
            "UnsafeMutableBufferPointer "
            // Special
            "Void Never Any";
    }
    if (set == 3) {
        // Doc-comment keywords (markdown headings used in /// /** */ blocks).
        // Maps to SCE_C_COMMENTDOCKEYWORD style. Swift markdown convention
        // uses dash-prefix: - Parameter:, - Returns:, - Throws:, etc.
        return
            "Parameter Parameters Returns Throws Note Warning Important "
            "Precondition Postcondition Requires Invariant Complexity "
            "Author Authors Copyright Date SeeAlso Since Tag TODO FIXME "
            "MARK Version Attention Bug Experiment Remark";
    }
    if (set == 4) {
        // Swift attributes + property wrappers. Maps to SCE_C_GLOBALCLASS
        // (Swift orange in our palette). Add common SwiftUI / Combine
        // wrappers so they highlight in Xcode-orange when used.
        return
            // Function / declaration attributes
            "objc available autoclosure escaping nonescaping discardableResult "
            "frozen inlinable usableFromInline propertyWrapper resultBuilder "
            "globalActor MainActor Sendable unchecked retroactive "
            "dynamicCallable dynamicMemberLookup objcMembers IBOutlet "
            "IBAction IBDesignable IBInspectable NSManaged NSCopying "
            "GKInspectable testable importable "
            // SwiftUI property wrappers
            "State Binding ObservedObject StateObject EnvironmentObject "
            "Environment FocusState AppStorage SceneStorage Published "
            "ViewBuilder SceneBuilder TableColumnBuilder ToolbarContentBuilder "
            "CommandsBuilder MenuBuilder "
            // Concurrency
            "TaskLocal";
    }
    return nullptr;
}
