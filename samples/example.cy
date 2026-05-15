# Notepatra palette preview - synthetic; no real data
# Exercises: cdef, cpdef, ctypedef, cimport, nogil, type declarations,
# control flow.

# cython: language_level=3
# distutils: language=c

from libc.math cimport sqrt
from libc.stdlib cimport malloc, free

cdef double PI = 3.14159
cdef int MAX_RETRIES = 0x10

ctypedef double real_t
ctypedef struct Point:
    double x
    double y

cdef class Counter:
    cdef int _value
    cdef readonly str name

    def __cinit__(self, str name, int start=0):
        self.name = name
        self._value = start

    cpdef int increment(self, int by=1):
        self._value += by
        return self._value

    cpdef int value(self):
        return self._value

cdef inline double magnitude(Point p) nogil:
    return sqrt(p.x * p.x + p.y * p.y)

cdef int square(int n) nogil:
    return n * n

cpdef double sum_squares(int n):
    cdef int i
    cdef double total = 0.0
    with nogil:
        for i in range(n):
            total += square(i)
    return total

cpdef str classify(object value):
    if value is None:
        return "none"
    elif isinstance(value, int):
        if value < 0:
            return f"neg:{value}"
        return f"int:{value}"
    elif isinstance(value, str):
        return f"str:{value}"
    return "unknown"

def main():
    cdef Point p
    p.x = 3.0
    p.y = 4.0
    cdef real_t m
    with nogil:
        m = magnitude(p)

    c = Counter("alpha", 10)
    c.increment()
    c.increment(2)

    print(f"pi={PI} retries={MAX_RETRIES} magnitude={m}")
    print(f"counter[{c.name}]={c.value()}")
    print("sum_squares(5)=", sum_squares(5))
    for v in (-3, 42, "ok", None):
        print(classify(v))

    cdef int *buf = <int *>malloc(4 * sizeof(int))
    if buf is NULL:
        raise MemoryError("alloc failed")
    try:
        for i in range(4):
            buf[i] = i * i
        print("buf[3] =", buf[3])
    finally:
        free(buf)
