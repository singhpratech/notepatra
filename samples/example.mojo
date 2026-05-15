# Notepatra palette preview - synthetic; no real data
# Exercises: fn, var, struct, trait, alias, Python interop, control flow.

from python import Python

alias PI: Float64 = 3.14159
alias MAX_RETRIES: Int = 0x10

trait Greetable:
    fn greet(self) -> String: ...

@value
struct User(Greetable):
    var id: Int
    var name: String
    var email: String

    fn greet(self) -> String:
        return "hello " + self.name + " <" + self.email + ">"

@value
struct Point:
    var x: Float64
    var y: Float64

    fn magnitude(self) -> Float64:
        return (self.x * self.x + self.y * self.y) ** 0.5

fn square(n: Int) -> Int:
    return n * n

fn classify(n: Int) -> String:
    if n < 0:
        return "neg"
    elif n == 0:
        return "zero"
    else:
        return "pos"

fn sum_range(start: Int, end: Int) -> Int:
    var total: Int = 0
    var i: Int = start
    while i < end:
        total += i
        i += 1
    return total

fn main() raises:
    var alice = User(1, "Alice", "alice@example.com")
    var bob   = User(2, "Bob", "bob@example.org")
    var p     = Point(3.0, 4.0)

    print("pi =", PI, "retries =", MAX_RETRIES)
    print(alice.greet())
    print(bob.greet())
    print("magnitude =", p.magnitude())

    for i in range(5):
        print("square(", i, ") =", square(i))

    print("sum 1..10 =", sum_range(1, 10))

    for v in List[Int](-3, 0, 42):
        print(classify(v))

    var py = Python.import_module("builtins")
    var py_len = py.len("palette")
    print("python len('palette') =", py_len)
