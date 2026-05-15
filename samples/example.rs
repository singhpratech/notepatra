// Notepatra palette preview - synthetic; no real data
// Exercises: structs, enums, traits, impl, lifetimes, async fn, match,
// Option/Result/Vec, macros (println!/vec!), unsafe, attributes, generics.

#![allow(dead_code)]

use std::collections::HashMap;
use std::fmt;

const MAX_RETRIES: u32 = 3;
const PI: f64 = 3.14159;

#[derive(Debug, Clone)]
struct Point<T> {
    x: T,
    y: T,
}

impl<T: Copy + std::ops::Add<Output = T>> Point<T> {
    fn new(x: T, y: T) -> Self { Self { x, y } }
    fn sum(&self) -> T { self.x + self.y }
}

#[derive(Debug)]
enum Shape {
    Circle { radius: f64 },
    Square(f64),
    None,
}

trait Area {
    fn area(&self) -> f64;
}

impl Area for Shape {
    fn area(&self) -> f64 {
        match self {
            Shape::Circle { radius } => PI * radius * radius,
            Shape::Square(side) => side * side,
            Shape::None => 0.0,
        }
    }
}

impl fmt::Display for Shape {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Shape(area={:.2})", self.area())
    }
}

fn longest<'a>(a: &'a str, b: &'a str) -> &'a str {
    if a.len() >= b.len() { a } else { b }
}

async fn fetch(id: u32) -> Result<String, &'static str> {
    if id == 0 { Err("invalid id") } else { Ok(format!("item-{:04}", id)) }
}

fn main() {
    let p: Point<i32> = Point::new(3, 4);
    let shapes = vec![Shape::Circle { radius: 1.5 }, Shape::Square(2.0)];
    let mut counts: HashMap<&str, u32> = HashMap::new();
    counts.insert("circles", 1);

    let raw: *const i32 = &p.x;
    unsafe { println!("raw deref: {}", *raw); }

    for s in &shapes { println!("{} sum={}", s, p.sum()); }
    println!("longest = {}", longest("alpha", "beta"));
    println!("retries={} pi={}", MAX_RETRIES, PI);

    let _ = futures_lite_stub(fetch(1));
}

fn futures_lite_stub<T>(_: T) {}
