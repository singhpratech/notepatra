// Notepatra palette preview - synthetic; no real data
// Exercises: package, imports, record, sealed/permits, switch expression,
// instanceof pattern, var, streams, generics, control flow.

package com.example.samples;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

public final class Example {

    public record User(int id, String name, String email) {}

    public sealed interface Shape permits Circle, Square, None {}
    public record Circle(double radius) implements Shape {}
    public record Square(double side) implements Shape {}
    public record None() implements Shape {}

    public enum Status { PENDING, ACTIVE, ARCHIVED }

    public static double area(Shape s) {
        return switch (s) {
            case Circle c -> Math.PI * c.radius() * c.radius();
            case Square sq -> sq.side() * sq.side();
            case None n -> 0.0;
        };
    }

    public static String describe(Object o) {
        if (o instanceof User u && u.id() > 0) {
            return "user:" + u.name();
        } else if (o instanceof Integer n) {
            return "int:" + n;
        }
        return "unknown";
    }

    public static <T> List<T> firstN(List<T> in, int n) {
        return in.stream().limit(n).collect(Collectors.toList());
    }

    public static void main(String[] args) {
        var users = new ArrayList<User>();
        users.add(new User(1, "Alice", "alice@example.com"));
        users.add(new User(2, "Bob", "bob@example.org"));

        var shapes = List.<Shape>of(new Circle(1.5), new Square(2.0), new None());
        var counts = Map.of(Status.ACTIVE, 2, Status.PENDING, 1);

        double total = shapes.stream().mapToDouble(Example::area).sum();
        var names = users.stream().map(User::name).collect(Collectors.toList());

        System.out.printf("total=%.2f names=%s counts=%s%n", total, names, counts);

        Optional<User> alice = users.stream().filter(u -> u.id() == 1).findFirst();
        alice.ifPresent(u -> System.out.println(describe(u)));

        for (var s : firstN(shapes, 2)) {
            System.out.println("shape area = " + area(s));
        }
    }
}
