# Notepatra palette preview - synthetic; no real data
# Exercises: proc, set, puts, if/elseif/else, foreach, while, lists, dict,
# control flow.

set PI 3.14159
set MAX_RETRIES 16

set users [list \
    [dict create id 1 name "Alice" email "alice@example.com" status active]  \
    [dict create id 2 name "Bob"   email "bob@example.org"   status pending] \
    [dict create id 3 name "Carol" email "carol@example.org" status pending] \
]

set counts [dict create pending 0 active 2 archived 1]

proc greet {user} {
    set name  [dict get $user name]
    set email [dict get $user email]
    return "hello $name <$email>"
}

proc classify {value} {
    if {$value eq ""} {
        return "empty"
    } elseif {[string is integer -strict $value]} {
        if {$value < 0} {
            return "neg:$value"
        } else {
            return "int:$value"
        }
    } elseif {[string match "*@example.*" $value]} {
        return "email:$value"
    } else {
        return "str:$value"
    }
}

proc square {n} {
    return [expr {$n * $n}]
}

puts "pi=$PI retries=$MAX_RETRIES"

foreach user $users {
    set status [dict get $user status]
    switch -- $status {
        active  { puts "[greet $user] (active)" }
        pending { puts "[greet $user] (pending)" }
        default { puts "[greet $user] (other)" }
    }
}

set i 0
set squares [list]
while {$i < 5} {
    lappend squares [square $i]
    incr i
}
puts "squares = $squares"

dict for {k v} $counts {
    puts "$k => $v"
}

foreach v [list "" 42 -7 alice@example.com hello] {
    puts [classify $v]
}
