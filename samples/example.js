// Notepatra palette preview - synthetic; no real data
// Exercises: keywords (const/let/class/async/await), template literals,
// regex literals, private fields, optional chaining, arrow funcs,
// destructuring, numbers, strings, comments, control flow.

'use strict';

const PI = 3.14159;
const HEX = 0xCAFEBABE;
const BIN = 0b1010_1010;
const greeting = `Hello, ${'palette'}!`;
const EMAIL_RE = /^[a-z0-9._%+-]+@example\.(com|org)$/i;

class Counter {
    #value = 0;
    static #instances = 0;

    constructor(start = 0) {
        this.#value = start;
        Counter.#instances += 1;
    }

    get value() { return this.#value; }
    increment(by = 1) { this.#value += by; return this; }

    static get total() { return Counter.#instances; }
}

const square = (n) => n * n;
const add = (a, b) => a + b;

async function fetchItems(ids) {
    const out = [];
    for (const id of ids) {
        await Promise.resolve();
        out.push({ id, label: `item-${String(id).padStart(4, '0')}` });
    }
    return out;
}

function describe({ name = 'anonymous', email = 'alice@example.com', meta } = {}) {
    const domain = email?.split('@')?.[1] ?? 'unknown';
    const safeName = meta?.display ?? name;
    return `name=${safeName} domain=${domain}`;
}

const numbers = [1, 2, 3, 4, 5];
const doubled = numbers.map((n) => n * 2).filter((n) => n > 2);
const lookup = new Map([['a', 1], ['b', 2]]);
const tags = new Set(['alpha', 'beta', 'gamma']);

(async () => {
    const c = new Counter(10).increment().increment(2);
    const items = await fetchItems([1, 2, 3]);
    console.log(greeting, PI, HEX, BIN, c.value, items.length);
    console.log(describe({ name: 'Alice', meta: { display: 'A.' } }));
    console.log(doubled, [...lookup.keys()], [...tags]);
    console.log(EMAIL_RE.test('bob@example.org'));
})();
