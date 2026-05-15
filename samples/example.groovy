// Notepatra palette preview - synthetic; no real data
// Exercises: class, def, closures, string interpolation, list/map literals,
// @Annotation, control flow, generics.

import groovy.transform.CompileStatic
import groovy.transform.ToString

final BigDecimal PI = 3.14159
final int MAX_RETRIES = 0x10

@ToString(includeNames = true)
class User {
    int id
    String name
    String email
    String status = 'pending'

    String greet() { "hello ${name} <${email}>" }
}

@CompileStatic
class Repository<T> {
    private final Map<Integer, T> items = [:]
    void add(int id, T item) { items[id] = item }
    T find(int id) { items[id] }
    int getCount() { items.size() }
}

def classify = { value ->
    switch (value) {
        case null:                return 'null'
        case { it instanceof Integer && (it as Integer) < 0 }:
            return "neg:${value}"
        case Integer:             return "int:${value}"
        case String:              return "str:${value}"
        case User:                return "user:${value.name}"
        default:                  return 'unknown'
    }
}

def users = [
    new User(id: 1, name: 'Alice', email: 'alice@example.com', status: 'active'),
    new User(id: 2, name: 'Bob',   email: 'bob@example.org'),
]

def shapes = [
    [kind: 'circle', size: 1.5],
    [kind: 'square', size: 2.0],
    [kind: 'none',   size: 0.0],
]

def area = { Map s ->
    switch (s.kind) {
        case 'circle': return PI * (s.size as BigDecimal)**2
        case 'square': return (s.size as BigDecimal)**2
        default:       return 0.0g
    }
}

def repo = new Repository<User>()
users.each { repo.add(it.id, it) }

println "pi=${PI} retries=${MAX_RETRIES} count=${repo.count}"
users.findAll { it.email ==~ /.*@example\.(com|org)/ }.each { println it.greet() }
shapes.each { println "area=${area(it)}" }
[-3, 42, 'ok', null, users[0]].each { println classify(it) }

(1..3).collect { it * it }.with { println "squares=${it}" }
