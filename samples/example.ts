// Notepatra palette preview - synthetic; no real data
// Exercises: interfaces, type aliases, generics, enums, namespaces,
// satisfies, keyof, infer, conditional types, readonly, decorators-free TS.

export {};

interface User {
    readonly id: number;
    name: string;
    email: `${string}@${string}`;
    roles?: ReadonlyArray<Role>;
}

type Role = 'admin' | 'editor' | 'viewer';

enum Status {
    Pending = 'pending',
    Active = 'active',
    Archived = 'archived',
}

type Keys<T> = keyof T;
type UnwrapPromise<T> = T extends Promise<infer U> ? U : T;
type ReadonlyDeep<T> = { readonly [K in keyof T]: ReadonlyDeep<T[K]> };

namespace Util {
    export function isUser(x: unknown): x is User {
        return typeof x === 'object' && x !== null && 'id' in x && 'email' in x;
    }
    export const MAX_ROLES: number = 8;
}

class Repository<T extends { id: number }> {
    private items: Map<number, T> = new Map();

    add(item: T): this {
        this.items.set(item.id, item);
        return this;
    }

    find(id: number): T | undefined {
        return this.items.get(id);
    }

    get size(): number { return this.items.size; }

    async snapshot(): Promise<readonly T[]> {
        await Promise.resolve();
        return Array.from(this.items.values());
    }
}

const sampleUser = {
    id: 1,
    name: 'Alice',
    email: 'alice@example.com',
    roles: ['admin'],
} satisfies User;

const repo = new Repository<User>();
repo.add(sampleUser).add({ id: 2, name: 'Bob', email: 'bob@example.org' });

type SampleKey = Keys<User>;
type Resolved = UnwrapPromise<Promise<number>>;

async function main(): Promise<void> {
    const snap = await repo.snapshot();
    for (const u of snap) {
        if (Util.isUser(u)) console.log(u.name, Status.Active);
    }
}

void main();
