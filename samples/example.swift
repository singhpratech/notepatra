// Notepatra palette preview - synthetic; no real data
// Exercises: struct, class, protocol, enum with associated values,
// async/await, optionals, guard let, computed properties, generics.

import Foundation

let PI: Double = 3.14159
let MAX_RETRIES: Int = 0x10

protocol Greeter {
    func greet() -> String
}

struct User: Greeter {
    let id: Int
    var name: String
    var email: String
    var displayName: String { name.capitalized }
    func greet() -> String { "hello \(name) <\(email)>" }
}

enum Shape {
    case circle(radius: Double)
    case square(side: Double)
    case none

    var area: Double {
        switch self {
        case .circle(let r): return PI * r * r
        case .square(let s): return s * s
        case .none: return 0.0
        }
    }
}

final class Repository<T> {
    private(set) var items: [Int: T] = [:]
    func add(_ id: Int, _ item: T) { items[id] = item }
    func find(_ id: Int) -> T? { items[id] }
    var count: Int { items.count }
}

func describe(_ value: Any?) -> String {
    guard let v = value else { return "nil" }
    if let n = v as? Int { return n < 0 ? "negative:\(n)" : "int:\(n)" }
    if let s = v as? String { return "str:\(s)" }
    return "unknown"
}

func fetchLabel(_ id: Int) async -> String {
    try? await Task.sleep(nanoseconds: 0)
    return "item-\(String(format: "%04d", id))"
}

@main
struct App {
    static func main() async {
        let repo = Repository<User>()
        repo.add(1, User(id: 1, name: "alice", email: "alice@example.com"))
        repo.add(2, User(id: 2, name: "bob", email: "bob@example.org"))

        let shapes: [Shape] = [.circle(radius: 1.5), .square(side: 2.0), .none]
        let total = shapes.reduce(0.0) { $0 + $1.area }

        print("count=\(repo.count) total=\(total) pi=\(PI) retries=\(MAX_RETRIES)")
        if let alice = repo.find(1) { print(alice.greet(), alice.displayName) }
        print(describe(-3), describe("ok"), describe(nil))
        print(await fetchLabel(7))
    }
}
