// Notepatra palette preview - synthetic; no real data
// Exercises: class, mixin, async/await, Stream, generics, named parameters,
// null safety, Future, extension methods, control flow.

import 'dart:async';

const double pi = 3.14159;
const int maxRetries = 0x10;

mixin Greetable {
  String get name;
  String greet() => 'hello $name';
}

class User with Greetable {
  final int id;
  @override
  final String name;
  final String email;
  final int? score;

  User({required this.id, required this.name, required this.email, this.score});

  String describe() => 'user($id, $name, ${email})';
}

class Repository<T> {
  final Map<int, T> _items = {};
  void add(int id, T item) => _items[id] = item;
  T? find(int id) => _items[id];
  int get count => _items.length;
  Iterable<T> get all => _items.values;
}

extension StringX on String {
  String shout() => '${toUpperCase()}!';
}

Future<String> fetchLabel(int id) async {
  await Future<void>.delayed(Duration.zero);
  return 'item-${id.toString().padLeft(4, '0')}';
}

Stream<int> squares(int n) async* {
  for (var i = 1; i <= n; i++) {
    yield i * i;
  }
}

String classify(Object? v) {
  if (v == null) return 'null';
  if (v is int) return v < 0 ? 'neg:$v' : 'int:$v';
  if (v is String) return 'str:$v';
  if (v is User) return 'user:${v.name}';
  return 'unknown';
}

Future<void> main() async {
  final repo = Repository<User>();
  repo.add(1, User(id: 1, name: 'Alice', email: 'alice@example.com', score: 90));
  repo.add(2, User(id: 2, name: 'Bob', email: 'bob@example.org'));

  print('count=${repo.count} pi=$pi retries=$maxRetries');
  for (final u in repo.all) {
    print('${u.greet()} ${u.describe()}');
  }

  await for (final s in squares(3)) {
    print('square=$s');
  }
  print(await fetchLabel(7));
  print('alice'.shout());
  for (final v in <Object?>[42, -1, 'ok', null]) {
    print(classify(v));
  }
}
