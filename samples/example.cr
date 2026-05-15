# Notepatra palette preview - synthetic; no real data
# Exercises: class, def, struct, fiber, type annotations, modules, macros,
# control flow.

require "json"

PI = 3.14159
MAX_RETRIES = 0x10

module Greetable
  def greet : String
    "hello #{name} <#{email}>"
  end
end

class User
  include Greetable

  getter id : Int32
  property name : String
  property email : String

  def initialize(@id : Int32, @name : String, @email : String)
  end
end

struct Point
  getter x : Float64
  getter y : Float64

  def initialize(@x : Float64, @y : Float64)
  end

  def magnitude : Float64
    Math.sqrt(@x * @x + @y * @y)
  end
end

enum Status
  Pending
  Active
  Archived
end

macro define_classifier(name)
  def {{name.id}}(v)
    case v
    when Nil    then "nil"
    when Int    then v < 0 ? "neg:#{v}" : "int:#{v}"
    when String then "str:#{v}"
    when User   then "user:#{v.name}"
    else "unknown"
    end
  end
end

define_classifier(classify)

users = [
  User.new(1, "Alice", "alice@example.com"),
  User.new(2, "Bob",   "bob@example.org"),
]

points = [Point.new(3.0, 4.0), Point.new(1.0, 1.0)]
counts = {Status::Pending => 0, Status::Active => 2, Status::Archived => 1}

channel = Channel(Int32).new(3)
spawn do
  3.times { |i| channel.send(i * i) }
  channel.close
end

puts "pi=#{PI} retries=#{MAX_RETRIES}"
users.each { |u| puts u.greet }
points.each { |p| puts "magnitude=#{p.magnitude}" }
puts counts.to_json
while v = channel.receive?
  puts "square=#{v}"
end
[-3, 42, "ok", nil, users.first].each { |v| puts classify(v) }
