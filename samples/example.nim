# Notepatra palette preview - synthetic; no real data
# Exercises: proc, var/let/const, type, object, ref, when (compile-time),
# template, macro, control flow.

import std/[strformat, sequtils, tables]

const
  PI = 3.14159
  MAX_RETRIES = 0x10

type
  Status = enum
    sPending, sActive, sArchived

  User = object
    id: int
    name: string
    email: string
    status: Status

  Shape = ref object of RootObj
  Circle = ref object of Shape
    radius: float
  Square = ref object of Shape
    side: float

method area(s: Shape): float {.base.} = 0.0
method area(s: Circle): float = PI * s.radius * s.radius
method area(s: Square): float = s.side * s.side

template debug(msg: string) =
  when defined(debugBuild):
    echo "DBG: ", msg
  else:
    discard

proc greet(u: User): string =
  fmt"hello {u.name} <{u.email}>"

proc classify(value: int): string =
  if value < 0: fmt"neg:{value}"
  elif value == 0: "zero"
  else: fmt"pos:{value}"

proc squared(n: int): int = n * n

let users = @[
  User(id: 1, name: "Alice", email: "alice@example.com", status: sActive),
  User(id: 2, name: "Bob",   email: "bob@example.org",   status: sPending),
]

var shapes: seq[Shape] = @[]
shapes.add(Circle(radius: 1.5))
shapes.add(Square(side: 2.0))

let total = shapes.mapIt(it.area()).foldl(a + b, 0.0)
let counts = {sPending: 0, sActive: 2, sArchived: 1}.toTable

echo fmt"pi={PI} retries={MAX_RETRIES} total={total:.2f}"
for u in users:
  echo greet(u)
for v in [-3, 0, 42]:
  echo classify(v)
echo "squares: ", toSeq(1..3).mapIt(squared(it))
echo "counts: ", counts
debug("done")
