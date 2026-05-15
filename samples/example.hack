<?hh
// Notepatra palette preview - synthetic; no real data
// Exercises: function, class, generics, type aliases, async, vec/dict/keyset,
// control flow.

namespace Notepatra\Samples;

type UserId = int;
type Email = string;

const float PI = 3.14159;
const int MAX_RETRIES = 0x10;

enum Status: string {
    Pending = 'pending';
    Active = 'active';
    Archived = 'archived';
}

final class User {
    public function __construct(
        public UserId $id,
        public string $name,
        public Email $email,
        public Status $status = Status::Pending,
    ) {}

    public function greet(): string {
        return "hello {$this->name} <{$this->email}>";
    }
}

final class Repository<T> {
    private dict<int, T> $items = dict[];

    public function add(int $id, T $item): void {
        $this->items[$id] = $item;
    }

    public function find(int $id): ?T {
        return idx($this->items, $id);
    }

    public function count(): int {
        return C\count($this->items);
    }
}

async function fetch_label_async(int $id): Awaitable<string> {
    await \HH\Asio\later();
    return 'item-'.\str_pad((string)$id, 4, '0', STR_PAD_LEFT);
}

function classify(mixed $value): string {
    if ($value is null) return 'null';
    if ($value is int) return $value < 0 ? "neg:{$value}" : "int:{$value}";
    if ($value is string) return "str:{$value}";
    if ($value is User) return "user:{$value->name}";
    return 'unknown';
}

<<__EntryPoint>>
async function main_async(): Awaitable<void> {
    $repo = new Repository<User>();
    $repo->add(1, new User(1, 'Alice', 'alice@example.com', Status::Active));
    $repo->add(2, new User(2, 'Bob', 'bob@example.org'));

    $names = vec['Alice', 'Bob', 'Carol'];
    $tags = keyset['alpha', 'beta', 'gamma'];
    $scores = dict['Alice' => 90, 'Bob' => 75];

    echo "pi=".PI." retries=".MAX_RETRIES." count=".$repo->count()."\n";
    foreach ($names as $n) echo "name={$n}\n";
    foreach ($tags as $t) echo "tag={$t}\n";
    foreach ($scores as $k => $v) echo "{$k}={$v}\n";
    echo (await fetch_label_async(7))."\n";
    foreach (vec[-3, 42, 'ok', null] as $v) echo classify($v)."\n";
}
