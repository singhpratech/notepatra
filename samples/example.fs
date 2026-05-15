// Notepatra palette preview - synthetic; no real data
// Exercises: module, let, type, match, pipe operator, computation expressions,
// discriminated union, records, control flow.

module Samples.Example

open System

let PI = 3.14159
let MAX_RETRIES = 0x10

type Status = Pending | Active | Archived

type User = {
    Id: int
    Name: string
    Email: string
    Status: Status
}

type Shape =
    | Circle of radius: float
    | Square of side: float
    | NoShape

let area shape =
    match shape with
    | Circle r -> PI * r * r
    | Square s -> s * s
    | NoShape  -> 0.0

let greet user =
    sprintf "hello %s <%s>" user.Name user.Email

let classify value =
    match box value with
    | null               -> "null"
    | :? int as n when n < 0 -> sprintf "neg:%d" n
    | :? int as n        -> sprintf "int:%d" n
    | :? string as s     -> sprintf "str:%s" s
    | _                  -> "unknown"

let users = [
    { Id = 1; Name = "Alice"; Email = "alice@example.com"; Status = Active }
    { Id = 2; Name = "Bob";   Email = "bob@example.org";   Status = Pending }
]

let shapes : Shape list = [ Circle 1.5; Square 2.0; NoShape ]

let total =
    shapes
    |> List.map area
    |> List.sum

let labels =
    users
    |> List.filter (fun u -> u.Email.EndsWith "example.com" || u.Email.EndsWith "example.org")
    |> List.map greet

let result =
    async {
        do! Async.Sleep 0
        return total
    } |> Async.RunSynchronously

[<EntryPoint>]
let main _argv =
    printfn "pi=%f retries=%d total=%f result=%f" PI MAX_RETRIES total result
    labels |> List.iter (printfn "%s")
    [ -3 :> obj; 42 :> obj; "ok" :> obj ] |> List.iter (classify >> printfn "%s")
    0
