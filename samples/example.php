<?php
// Notepatra palette preview - synthetic; no real data
// Exercises: namespace, class, constructor promotion, readonly, enums,
// match, named arguments, type hints, control flow.

declare(strict_types=1);

namespace Notepatra\Samples;

const PI = 3.14159;
const MAX_RETRIES = 0x10;

enum Status: string {
    case Pending = 'pending';
    case Active = 'active';
    case Archived = 'archived';

    public function label(): string {
        return match ($this) {
            Status::Pending => 'P',
            Status::Active => 'A',
            Status::Archived => 'X',
        };
    }
}

final class User {
    public function __construct(
        public readonly int $id,
        public readonly string $name,
        public readonly string $email,
        public Status $status = Status::Pending,
    ) {}

    public function greet(): string {
        return "hello {$this->name} <{$this->email}>";
    }
}

class Repository {
    /** @var array<int, User> */
    private array $items = [];

    public function add(User $u): self {
        $this->items[$u->id] = $u;
        return $this;
    }

    public function find(int $id): ?User {
        return $this->items[$id] ?? null;
    }

    public function count(): int { return count($this->items); }
}

function describe(mixed $value): string {
    return match (true) {
        $value === null => 'null',
        is_int($value) && $value < 0 => "negative:{$value}",
        is_int($value) => "int:{$value}",
        is_string($value) => "str:{$value}",
        $value instanceof User => "user:{$value->name}",
        default => 'unknown',
    };
}

$repo = new Repository();
$repo->add(new User(id: 1, name: 'Alice', email: 'alice@example.com', status: Status::Active));
$repo->add(new User(id: 2, name: 'Bob',   email: 'bob@example.org'));

printf("count=%d pi=%.5f retries=%d\n", $repo->count(), PI, MAX_RETRIES);
foreach ([$repo->find(1), 7, -3, 'ok', null] as $v) {
    echo describe($v), PHP_EOL;
}
echo $repo->find(1)?->greet() ?? 'missing', PHP_EOL;
