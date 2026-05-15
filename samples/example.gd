# Notepatra palette preview - synthetic; no real data
# Exercises: extends, var, func, signal, @export, await, match, class_name,
# control flow.

class_name SamplePlayer
extends Node

signal damaged(amount: int)
signal died

const PI: float = 3.14159
const MAX_RETRIES: int = 0x10

@export var display_name: String = "Alice"
@export var max_health: int = 100
@export_range(0, 100) var speed: float = 50.0

var health: int = 100
var inventory: Array[String] = ["sword", "shield"]
var stats: Dictionary = {
    "strength": 10,
    "agility": 8,
    "intellect": 6,
}

func _ready() -> void:
    print("ready: %s pi=%f retries=%d" % [display_name, PI, MAX_RETRIES])
    print("stats: ", stats)
    for item in inventory:
        print("carrying: ", item)

func greet(other: String = "stranger") -> String:
    return "hello %s, I am %s" % [other, display_name]

func take_damage(amount: int) -> void:
    health = max(health - amount, 0)
    damaged.emit(amount)
    if health <= 0:
        died.emit()

func classify(value) -> String:
    match value:
        null:
            return "null"
        var n when typeof(n) == TYPE_INT and n < 0:
            return "neg:%d" % n
        var n when typeof(n) == TYPE_INT:
            return "int:%d" % n
        var s when typeof(s) == TYPE_STRING:
            return "str:%s" % s
        _:
            return "unknown"

func wait_then(label: String) -> void:
    await get_tree().create_timer(0.0).timeout
    print("done waiting for ", label)

func run_demo() -> void:
    print(greet("Bob"))
    take_damage(20)
    print("health=", health)
    for v in [-3, 42, "ok", null]:
        print(classify(v))
    var squares := []
    for i in range(3):
        squares.append(i * i)
    print("squares=", squares)
