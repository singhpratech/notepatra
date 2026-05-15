// Notepatra palette preview - synthetic; no real data
// Exercises: namespace, class, properties, records, async/await, LINQ,
// pattern matching, nullable types, attributes, generics.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading.Tasks;

namespace Notepatra.Samples;

[Serializable]
public record User(int Id, string Name, string Email);

public enum Status { Pending, Active, Archived }

[AttributeUsage(AttributeTargets.Class)]
public sealed class TaggedAttribute : Attribute
{
    public string Tag { get; }
    public TaggedAttribute(string tag) => Tag = tag;
}

[Tagged("demo")]
public class Repository<T> where T : class
{
    private readonly Dictionary<int, T> _items = new();
    public int Count => _items.Count;
    public string? LastError { get; private set; }

    public void Add(int id, T item) => _items[id] = item;

    public T? Find(int id) => _items.TryGetValue(id, out var v) ? v : null;

    public async Task<IReadOnlyList<T>> SnapshotAsync()
    {
        await Task.Yield();
        return _items.Values.ToList();
    }
}

public static class Program
{
    private const double Pi = 3.14159;

    public static string Describe(object? value) => value switch
    {
        null => "null",
        int n when n < 0 => $"negative:{n}",
        int n => $"int:{n}",
        string s => $"str:{s}",
        User u => $"user:{u.Name}",
        _ => "unknown",
    };

    public static async Task Main()
    {
        var repo = new Repository<User>();
        repo.Add(1, new User(1, "Alice", "alice@example.com"));
        repo.Add(2, new User(2, "Bob", "bob@example.org"));

        var snap = await repo.SnapshotAsync();
        var admins = from u in snap where u.Id < 10 orderby u.Name select u.Name;

        Console.WriteLine($"count={repo.Count} pi={Pi}");
        foreach (var name in admins) Console.WriteLine($"admin={name}");
        Console.WriteLine(Describe(new User(3, "Carol", "carol@example.org")));
        Console.WriteLine(Describe(-5));
    }
}
