# Notepatra palette preview - synthetic; no real data
# Exercises: keywords, builtins, types, decorators, f-strings, comments,
# numbers (int/float/hex/bin), operators, control flow, match/case, async/await,
# class/function names, comprehensions, type hints.

from __future__ import annotations
from dataclasses import dataclass, field
from typing import Optional, Generic, TypeVar
import asyncio
import re

T = TypeVar("T")

PI: float = 3.14159
MAX_RETRIES: int = 0x10
BIT_MASK: int = 0b1010
GREETING: str = "Hello, palette!"


@dataclass(frozen=True)
class Point(Generic[T]):
    x: T
    y: T
    tags: list[str] = field(default_factory=list)

    def magnitude(self) -> float:
        return (float(self.x) ** 2 + float(self.y) ** 2) ** 0.5


def classify(value: int | str | None) -> str:
    match value:
        case None:
            return "none"
        case int() if value < 0:
            return "negative"
        case int():
            return f"int:{value}"
        case str() as s:
            return f"str:{s!r}"
        case _:
            return "unknown"


async def fetch_all(items: list[int]) -> dict[int, str]:
    async def one(i: int) -> tuple[int, str]:
        await asyncio.sleep(0.0)
        return i, f"item-{i:04d}"
    results = await asyncio.gather(*(one(i) for i in items))
    return {k: v for k, v in results}


def main() -> None:
    pts = [Point(i, i * 2, tags=["even" if i % 2 == 0 else "odd"]) for i in range(5)]
    seen: set[int] = {p.x for p in pts}
    pattern = re.compile(r"^item-\d+$")
    assert all(isinstance(p, Point) for p in pts)
    print(GREETING, len(seen), bool(pattern), pts[0].magnitude())
    asyncio.run(fetch_all(list(seen)))


if __name__ == "__main__":
    main()
