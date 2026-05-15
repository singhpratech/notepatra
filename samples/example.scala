// Notepatra palette preview - synthetic; no real data
// Exercises: case class, object, trait, given/using, extension methods,
// match, for-comprehension, generics, control flow (Scala 3).

package samples

object Example:

  val PI: Double = 3.14159
  val MAX_RETRIES: Int = 0x10

  enum Status:
    case Pending, Active, Archived

  case class User(id: Int, name: String, email: String, status: Status = Status.Pending)

  sealed trait Shape
  case class Circle(radius: Double) extends Shape
  case class Square(side: Double) extends Shape
  case object NoShape extends Shape

  trait Show[T]:
    extension (t: T) def show: String

  given Show[User] with
    extension (u: User) def show: String = s"user(${u.id}, ${u.name})"

  given Show[Shape] with
    extension (s: Shape) def show: String = s match
      case Circle(r) => s"Circle($r)"
      case Square(s) => s"Square($s)"
      case NoShape   => "None"

  def area(s: Shape): Double = s match
    case Circle(r) => PI * r * r
    case Square(x) => x * x
    case NoShape   => 0.0

  def classify(v: Any): String = v match
    case null              => "null"
    case n: Int if n < 0   => s"neg:$n"
    case n: Int            => s"int:$n"
    case s: String         => s"str:$s"
    case u: User           => s"user:${u.name}"
    case _                 => "unknown"

  extension (n: Int) def squared: Int = n * n

  @main def run(): Unit =
    val users = List(
      User(1, "Alice", "alice@example.com", Status.Active),
      User(2, "Bob", "bob@example.org"),
    )
    val shapes: List[Shape] = List(Circle(1.5), Square(2.0), NoShape)

    val total = shapes.map(area).sum
    val labels =
      for
        u <- users
        if u.email.endsWith("example.com") || u.email.endsWith("example.org")
      yield u.show

    println(s"pi=$PI retries=$MAX_RETRIES total=$total")
    labels.foreach(println)
    shapes.foreach(s => println(s.show))
    List(-3, 42, "ok", null, users.head).foreach(v => println(classify(v)))
    println(5.squared)
