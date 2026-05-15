// Notepatra palette preview - synthetic; no real data
// Exercises: classes (public/private), templates, lambdas, auto, constexpr,
// concepts/requires, ranges, smart pointers, std::vector/string, control flow.

#include <algorithm>
#include <concepts>
#include <iostream>
#include <memory>
#include <numeric>
#include <ranges>
#include <string>
#include <vector>

constexpr double kPi = 3.14159;
constexpr int kMax = 0x40;

template <typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template <Numeric T>
constexpr T square(T x) requires(sizeof(T) <= 16) { return x * x; }

class Shape {
public:
    explicit Shape(std::string name) : name_(std::move(name)) {}
    virtual ~Shape() = default;
    virtual double area() const = 0;
    const std::string& name() const { return name_; }
private:
    std::string name_;
};

class Circle final : public Shape {
public:
    Circle(std::string n, double r) : Shape(std::move(n)), radius_(r) {}
    double area() const override { return kPi * radius_ * radius_; }
private:
    double radius_;
};

int main() {
    std::vector<std::unique_ptr<Shape>> shapes;
    shapes.emplace_back(std::make_unique<Circle>("c1", 1.0));
    shapes.emplace_back(std::make_unique<Circle>("c2", 2.5));

    auto total = std::accumulate(
        shapes.begin(), shapes.end(), 0.0,
        [](double acc, const auto& s) { return acc + s->area(); });

    std::vector<int> nums{1, 2, 3, 4, 5};
    auto evens = nums | std::views::filter([](int n) { return n % 2 == 0; });
    for (int n : evens) std::cout << "even=" << n << '\n';

    std::cout << "total area=" << total << " sq=" << square(7) << " max=" << kMax << '\n';
    for (const auto& s : shapes) {
        std::cout << s->name() << " area=" << s->area() << '\n';
    }
    return 0;
}
