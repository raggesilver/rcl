<div align="center">

<h1>RCL — Raggesilver's C Library</h1>

<img src="https://github.com/raggesilver/rcl/actions/workflows/ci.yml/badge.svg" alt="CI Status" />
<a href="https://github.com/raggesilver/rcl/actions/workflows/benchmark.yml"><img src="https://github.com/raggesilver/rcl/actions/workflows/benchmark.yml/badge.svg" alt="Benchmark" /></a>

</div>

This is a collection of useful functions and data structures I use in my C
projects. I do not have the time to write full documentation, although all
functions are documented in their header files. I don't recommend using this
library unless you know what you're doing.

> **Note:** This library does not check for `malloc`/`calloc`/`realloc`
> returning `NULL`. It is assumed that the program has enough memory available
> to run. If an allocation fails, behavior is undefined.

## Data structures

- `hashtable_t` — Decently fast open-addressing, linear-probing implementation.
- `array_t` — Generic, dynamic array. Includes helper functions for accessing
  `arr->data` with any type.
- `string_t` — String. Includes the basic stuff you'd expect from a string
  type in a modern language.

## JSON Parser

`json_value_t` is a one-pass, recursive descent JSON parser. It scans the input
string and builds the value tree in a single pass — no separate tokenization
phase.

Parsing is done via `json_parse_safe()`, which returns a `json_value_t*` tree,
or `json_parse_assert()` for quick scripts where you'd rather crash on bad
input. Errors include column numbers for easy debugging.

Quirks and non-standard behavior:

- **Trailing commas are allowed** in both arrays and objects (`[1, 2,]` is
  valid).
- Leading commas are rejected (`[,1]` is an error).

### Benchmarks

Compared against [cJSON](https://github.com/DaveGamble/cJSON) on real-world
JSON files:

| File | rcl (us/op) | cJSON (us/op) | rcl vs cJSON |
|---|---|---|---|
| 500_keys.json | 76.40 | 80.30 | 1.05x faster |
| big_string.json | 1285.95 | 1620.55 | 1.26x faster |
| canada.json | 10279.00 | 12242.25 | 1.19x faster |
| long_string_obj.json | 1174.25 | 1646.90 | 1.40x faster |
| mixed_100.json | 20.25 | 16.25 | 1.25x slower |
| numbers.json | 3356.55 | 4711.10 | 1.40x faster |

rcl beats cJSON on 5 out of 6 files. The remaining gap on small object-heavy
inputs (`mixed_100.json`) is due to per-node `malloc` overhead — an area for
future improvement.

### Caveats

Compared to [cJSON's caveats](https://github.com/DaveGamble/cJSON?tab=readme-ov-file#caveats),
here's where rcl stands:

**Fixed in rcl:**

- **Thread safety** — No global state. Multiple threads can call
  `json_parse_safe()` concurrently without issue.
- **Case sensitivity** — Object keys are always case-sensitive. No separate
  API needed.
- **Locale-independent number parsing** — Uses `strtod_l`/`_strtod_l` with an
  explicit "C" locale, so decimal parsing is correct regardless of the user's
  locale settings.
- **Duplicate object keys** — Last value wins (hashtable overwrites), rather
  than silently keeping duplicates.

**Shared with cJSON:**

- **Zero character** — Strings containing `\0` (`\u0000`) are not supported.
  C strings are null-terminated, so the string will be truncated at the first
  zero byte.
- **Character encoding** — Only UTF-8 input is supported. Invalid UTF-8 is
  not rejected — it passes through as-is.
- **Floating point** — Only IEEE 754 double precision is supported.

**Introduced by rcl:**

- **Trailing commas** — rcl accepts trailing commas in arrays and objects
  (`[1, 2,]`), which is not valid JSON per RFC 8259.
- **No nesting limit** — rcl does not impose an artificial nesting depth limit
  (cJSON defaults to 1000). Extremely deep nesting from adversarial input could
  cause a stack overflow, though in practice the default stack size (~8 MB on
  most platforms) allows tens of thousands of levels.

## Data structures (coming soon)

- `list_t` — Linked list. (coming soon)
- `string_builder_t` — A string builder. Useful for building one string out of
  many, without reallocating a string on each concatenation. (coming soon)

## Functions (coming soon)

- `read_line` — Read a line from a file descriptor. Keeps an internal buffer
  for each file descriptor for consecutive calls. (coming soon)
- `read_word` — Read the next word (space-separated) from a file descriptor.
  Keeps an internal buffer for each file descriptor for consecutive calls.
  (coming soon)
