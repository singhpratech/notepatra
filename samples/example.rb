# Notepatra palette preview - synthetic; no real data
# Exercises: class, module, methods, blocks, symbols, string interpolation,
# heredoc, regex, attr_accessor, control flow, hash/array literals.

require 'json'

PI = 3.14159
MAX_RETRIES = 0x10

module Greetable
  def greet
    "hello #{name} <#{email}>"
  end
end

class User
  include Greetable
  attr_accessor :name, :email
  attr_reader :id

  def initialize(id:, name:, email:)
    @id = id
    @name = name
    @email = email
  end

  def to_h
    { id: @id, name: @name, email: @email }
  end
end

class Repository
  def initialize
    @items = {}
  end

  def add(user)
    @items[user.id] = user
    self
  end

  def each(&block)
    @items.each_value(&block)
  end

  def count = @items.size
end

def describe(value)
  case value
  when nil then 'nil'
  when Integer then value < 0 ? "negative:#{value}" : "int:#{value}"
  when String then "str:#{value}"
  when User then "user:#{value.name}"
  else 'unknown'
  end
end

EMAIL_RE = /\A[a-z0-9._%+-]+@example\.(com|org)\z/i

repo = Repository.new
repo.add(User.new(id: 1, name: 'Alice', email: 'alice@example.com'))
repo.add(User.new(id: 2, name: 'Bob',   email: 'bob@example.org'))

doc = <<~TEXT
  Repository has #{repo.count} users.
  Pi is #{PI}; retries=#{MAX_RETRIES}.
TEXT

puts doc
repo.each { |u| puts u.greet if EMAIL_RE.match?(u.email) }
[1, 'ok', nil, repo].each { |v| puts describe(v) }
puts JSON.generate(repo.each.map(&:to_h).to_a) rescue nil
