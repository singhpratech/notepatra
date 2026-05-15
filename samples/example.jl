# Notepatra palette preview - synthetic; no real data
# Exercises: function, struct, abstract type, multiple dispatch,
# broadcasting (dot), type parameters, do-block, control flow.

const PI = 3.14159
const MAX_RETRIES = 0x10

abstract type Shape end

struct Circle <: Shape
    radius::Float64
end

struct Square <: Shape
    side::Float64
end

struct NoShape <: Shape end

area(s::Circle) = PI * s.radius^2
area(s::Square) = s.side^2
area(::NoShape) = 0.0

struct User{T<:Integer}
    id::T
    name::String
    email::String
end

greet(u::User) = "hello $(u.name) <$(u.email)>"

function classify(value)
    if value === nothing
        return "nothing"
    elseif value isa Integer
        return value < 0 ? "neg:$value" : "int:$value"
    elseif value isa AbstractString
        return "str:$value"
    elseif value isa User
        return "user:$(value.name)"
    else
        return "unknown"
    end
end

users = [
    User(1, "Alice", "alice@example.com"),
    User(2, "Bob",   "bob@example.org"),
]

shapes = Shape[Circle(1.5), Square(2.0), NoShape()]
areas = area.(shapes)
total = sum(areas)

squared = map(x -> x^2, 1:5)
labels = [greet(u) for u in users]
counts = Dict(:pending => 0, :active => 2, :archived => 1)

println("pi=$PI retries=$MAX_RETRIES total=$(round(total; digits=2))")
for (i, l) in enumerate(labels)
    println("$i: $l")
end

result = map(1:3) do i
    i * i + 1
end

for v in (-3, 42, "ok", nothing, users[1])
    println(classify(v))
end

println("squared=", squared, " counts=", counts, " result=", result)
