#!/usr/bin/env nu
# Notepatra palette preview — synthetic; no real data

let app_name = "notepatra"
let app_version = "0.1.84"
mut counter = 0

def greet [name: string = "world"] {
    $"Hello, ($name)! Running ($app_name) v($app_version)."
}

def is-admin [user: record] {
    $user.role == "admin"
}

let users = [
    { name: "Alice", email: "alice@example.com", role: "admin",  score: 9.5 }
    { name: "Bob",   email: "bob@example.com",   role: "editor", score: 8.0 }
    { name: "Carol", email: "carol@example.com", role: "viewer", score: 7.2 }
]

# Pipelines + structured data
$users
| where score > 7.5
| sort-by score --reverse
| select name email score
| each { |row| $"($row.name) -> ($row.score)" }

# String formatting and math
for u in $users {
    print (greet $u.name)
    $counter = $counter + 1
}
print $"Greeted ($counter) users."

# Error handling
try {
    let data = open "missing.json"
    print $data
} catch { |err|
    print -e $"Could not read file: ($err.msg)"
}

# Parsers
"alice,42,true"
| split row ","
| {
    name: ($in.0)
    age:  ($in.1 | into int)
    active: ($in.2 | into bool)
}

# External command + structured output
ls | where size > 1kb | first 5 | get name
