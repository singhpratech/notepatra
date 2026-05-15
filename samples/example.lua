-- Notepatra palette preview - synthetic; no real data
-- Exercises: local, function, table, metatable, for-in, while, coroutine,
-- string formatting, ipairs/pairs, control flow.

local PI = 3.14159
local MAX_RETRIES = 0x10

local Shape = {}
Shape.__index = Shape

function Shape.new(kind, size)
    local self = setmetatable({}, Shape)
    self.kind = kind
    self.size = size
    return self
end

function Shape:area()
    if self.kind == "circle" then
        return PI * self.size * self.size
    elseif self.kind == "square" then
        return self.size * self.size
    else
        return 0.0
    end
end

function Shape:__tostring()
    return string.format("Shape(%s, %.2f)", self.kind, self:area())
end

local function describe(value)
    local t = type(value)
    if value == nil then return "nil"
    elseif t == "number" then return value < 0 and "neg:" .. value or "num:" .. value
    elseif t == "string" then return "str:" .. value
    elseif t == "table" then return "tab"
    else return "unknown" end
end

local function producer()
    for i = 1, 3 do coroutine.yield(i * i) end
end

local users = {
    { id = 1, name = "Alice", email = "alice@example.com" },
    { id = 2, name = "Bob",   email = "bob@example.org" },
}

local shapes = { Shape.new("circle", 1.5), Shape.new("square", 2.0), Shape.new("none", 0) }

for i, u in ipairs(users) do
    print(string.format("%d: hello %s <%s>", i, u.name, u.email))
end

for k, v in pairs({ retries = MAX_RETRIES, pi = PI }) do
    print(k, "=", v)
end

local i = 1
while i <= #shapes do
    print(tostring(shapes[i]))
    i = i + 1
end

local co = coroutine.create(producer)
local ok, v = coroutine.resume(co)
while ok and v do print("co:", v); ok, v = coroutine.resume(co) end

print(describe(-7), describe("ok"), describe(nil))
