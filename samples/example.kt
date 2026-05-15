// Notepatra palette preview - synthetic; no real data
// Exercises: data class, sealed class, when, suspend/coroutines, nullable types,
// extension functions, lambdas, generics, control flow.

package com.example.samples

import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking

const val PI: Double = 3.14159
const val MAX_RETRIES: Int = 0x10

data class User(val id: Int, val name: String, val email: String)

sealed class Shape {
    data class Circle(val radius: Double) : Shape()
    data class Square(val side: Double) : Shape()
    object None : Shape()
}

enum class Status { PENDING, ACTIVE, ARCHIVED }

fun Shape.area(): Double = when (this) {
    is Shape.Circle -> PI * radius * radius
    is Shape.Square -> side * side
    Shape.None -> 0.0
}

fun String.greet(): String = "Hello, $this!"

inline fun <T, R> List<T>.mapEvenIndex(transform: (T) -> R): List<R> {
    val out = mutableListOf<R>()
    for ((i, v) in this.withIndex()) if (i % 2 == 0) out += transform(v)
    return out
}

suspend fun fetchLabel(id: Int): String {
    delay(0L)
    return "item-${id.toString().padStart(4, '0')}"
}

fun describe(value: Any?): String = when (value) {
    null -> "null"
    is Int -> if (value < 0) "negative:$value" else "int:$value"
    is String -> "str:$value"
    is User -> "user:${value.name}"
    else -> "unknown"
}

fun main() = runBlocking {
    val users = listOf(
        User(1, "Alice", "alice@example.com"),
        User(2, "Bob", "bob@example.org"),
    )
    val shapes: List<Shape> = listOf(Shape.Circle(1.5), Shape.Square(2.0), Shape.None)

    val total = shapes.sumOf { it.area() }
    val labels = users.mapEvenIndex { it.name.greet() }

    println("total=$total labels=$labels retries=$MAX_RETRIES")
    for (u in users) println(describe(u))
    println(fetchLabel(7))
    println(Status.ACTIVE)
}
