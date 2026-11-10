# int_sqrt() / lcm() differential test

Companion to `tools/testing/selftests/lib/sort/`. It compiles the C and the Rust
implementations of `int_sqrt()`, `lcm()` and `lcm_not_zero()` as user-space
objects, feeds both the same inputs and compares every result.

Transitional: goes away together with the C halves of `lib/math/gcd.c`,
`lib/math/lcm.c` and `lib/math/int_sqrt.c`.

## What is compared

| C (reference) | Rust (under test) |
| --- | --- |
| `int_sqrt()` — `lib/math/int_sqrt.c:20` | `rust/kernel/algo/int_sqrt.rs` |
| `lcm()` — `lib/math/lcm.c:8` | `rust/kernel/algo/lcm.rs` |
| `lcm_not_zero()` — `lib/math/lcm.c:17` | `rust/kernel/algo/lcm.rs` |
| `gcd()` — `lib/math/gcd.c:50` | unchanged: `rust/kernel/algo/gcd.rs` wraps this same C function |

The C side is **not retyped**: `mk-math-c.sh` extracts the function bodies from
the kernel sources themselves, so the reference cannot drift away from what is
being replaced. Only three trivial stubs are added (`__ffs`, `__fls`, `swap`,
`static_branch_likely`).

Likewise `mk-math-rs.sh` generates `math_rs.rs` from the tree sources and gives
`algo::lcm` the same C `gcd()` the kernel version calls, so the composition
under test matches the kernel exactly.

## Build and run

```sh
make                 # generates, builds
./diff               # default: 1000000 random pairs, fixed seed
./diff 1000000 0x9e3779b97f4a7c15
make run             # the exact invocation recorded in the evidence
```

Arguments are `<pairs> [seed]`; the generator is xorshift64* so every run is
reproducible from the printed seed. Exit status is non-zero if any result
differs.

Inputs: all values are `unsigned long`; one in four `lcm` pairs is masked to
16 bits so the common small cases are covered as well as the wide ones. Edge
cases are exercised explicitly first: 0, 1, 2, 3, 4, 15, 16, 17, powers of two
and their neighbours, `0xffffffff`, `0x100000000`, `LONG_MAX`, `LONG_MIN`,
`ULONG_MAX`, plus coprime pairs whose `lcm()` overflows.

## Reading the result

```
int_sqrt/lcm differential: 1000000 random pairs, seed 0x9e3779b97f4a7c15
comparisons: int_sqrt=1000021 lcm=1000012 lcm_not_zero=1000012
mismatches: 0
```

`mismatches: 0` and exit status 0 mean the two implementations agree on every
input. Any difference is printed (up to ten) with the offending operands.

## Overflow

`lcm(a, b)` computes `(a / gcd(a, b)) * b`, which overflows for large coprime
inputs. C wraps silently because the arithmetic is unsigned; the Rust version
uses `wrapping_mul()` for exactly that reason. The `0xffffffffffffffff` /
`0xfffffffffffffffd` edge pair exists to keep that behaviour pinned.
