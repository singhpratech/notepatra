# Notepatra palette preview - synthetic; no real data
# Exercises: ->, =>, classes, list/object comprehensions, string interpolation,
# splats, existential operator, control flow.

PI = 3.14159
MAX_RETRIES = 0x10

class User
  constructor: (@id, @name, @email, @status = 'pending') ->

  greet: ->
    "hello #{@name} <#{@email}>"

  toJSON: =>
    { id: @id, name: @name, email: @email, status: @status }

class Repository
  constructor: ->
    @items = {}

  add: (user) ->
    @items[user.id] = user
    this

  find: (id) ->
    @items[id] ? null

  count: ->
    Object.keys(@items).length

square = (n) -> n * n
add = (a, b) -> a + b

classify = (value) ->
  switch
    when not value? then 'null'
    when typeof value is 'number' and value < 0 then "neg:#{value}"
    when typeof value is 'number' then "int:#{value}"
    when typeof value is 'string' then "str:#{value}"
    when value instanceof User then "user:#{value.name}"
    else 'unknown'

users = [
  new User 1, 'Alice', 'alice@example.com', 'active'
  new User 2, 'Bob',   'bob@example.org'
  new User 3, 'Carol', 'carol@example.org'
]

repo = new Repository()
repo.add u for u in users

squares = (square n for n in [1..5])
evens = (n for n in [1..10] when n % 2 is 0)
byName = (u.name for u in users when u.email?.endsWith 'example.com')

stats =
  pi: PI
  retries: MAX_RETRIES
  count: repo.count()

variadic = (first, rest...) ->
  "first=#{first} rest=[#{rest.join ', '}]"

console.log "stats =", stats
console.log u.greet() for u in users
console.log "squares=#{squares}, evens=#{evens}, byName=#{byName}"
console.log variadic 'a', 'b', 'c', 'd'
console.log classify v for v in [-3, 42, 'ok', null, users[0]]
