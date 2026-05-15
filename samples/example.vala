// Notepatra palette preview - synthetic; no real data
// Exercises: namespace, class, signal, async/yield, generics, control flow.

namespace Notepatra.Samples {

    const double PI = 3.14159;
    const int MAX_RETRIES = 0x10;

    public enum Status {
        PENDING,
        ACTIVE,
        ARCHIVED
    }

    public class User : GLib.Object {
        public int id { get; construct; }
        public string name { get; set; }
        public string email { get; set; }
        public Status status { get; set; default = Status.PENDING; }

        public signal void renamed (string old_name, string new_name);

        public User (int id, string name, string email) {
            Object (id: id);
            this.name = name;
            this.email = email;
        }

        public string greet () {
            return "hello %s <%s>".printf (this.name, this.email);
        }

        public void rename (string new_name) {
            string old = this.name;
            this.name = new_name;
            renamed (old, new_name);
        }
    }

    public class Repository<T> : GLib.Object {
        private GLib.HashTable<int, T> items;

        public Repository () {
            items = new GLib.HashTable<int, T> (direct_hash, direct_equal);
        }

        public void add (int id, T item) { items.insert (id, item); }
        public T? find (int id) { return items.lookup (id); }
        public uint count () { return items.size (); }
    }

    public async string fetch_label_async (int id) {
        Idle.add (fetch_label_async.callback);
        yield;
        return "item-%04d".printf (id);
    }

    public string classify (Value? value) {
        if (value == null) return "null";
        Type t = value.type ();
        if (t == typeof (int)) {
            int n = value.get_int ();
            return n < 0 ? "neg:%d".printf (n) : "int:%d".printf (n);
        }
        if (t == typeof (string)) return "str:%s".printf (value.get_string ());
        return "unknown";
    }

    public static int main (string[] args) {
        var repo = new Repository<User> ();
        repo.add (1, new User (1, "Alice", "alice@example.com"));
        repo.add (2, new User (2, "Bob",   "bob@example.org"));

        var alice = repo.find (1);
        if (alice != null) {
            alice.renamed.connect ((o, n) => stdout.printf ("renamed %s -> %s\n", o, n));
            stdout.printf ("%s\n", alice.greet ());
            alice.rename ("Alice II");
        }

        stdout.printf ("pi=%f retries=%d count=%u\n", PI, MAX_RETRIES, repo.count ());
        Value iv = 42;
        Value sv = "ok";
        stdout.printf ("%s / %s\n", classify (iv), classify (sv));
        return 0;
    }
}
