# Bitsets: A Learning Guide

> For RCL-8 (`rcl_bitset`) and, later, component masks and tile flags in Z.
> This guide explains the idea and leaves the implementation to you. It has
> no set/test/count code and no step-by-step algorithms. The C snippets show
> how a *caller* might use a bitset, against an API that is only a sketch.

## Summary

A **bitset** stores a set of integers taken from a known universe
`0 .. N-1` using **one bit per possible member**. Bit `i` is 1 when `i` is in
the set and 0 when it is not. It supports:

- set, clear, toggle and test of a single position, all in **O(1)**;
- count (popcount), find-first-set, and bulk AND / OR / XOR between two
  bitsets, all in **O(N / w)**, where `w` is the width of the machine word
  you store the bits in.

That `N / w` is the whole story. A bitset is not fast because it does
clever things to each bit. It is fast because it almost never deals with
one bit at a time: a single instruction on a 64-bit word answers a question
about 64 members at once.

The design work is in two places. First, how a position maps onto storage
(you will derive this). Second, what happens at the **end** of the bitset
when `N` is not a multiple of `w`. The ticket asks for tests on exactly
those sizes, for good reason.

---

## 1. The problem

Your universe is small integers again: component type ids, entity ids,
tile type ids, map cells. You want a set of them that can:

1. answer "is `x` in the set?" with one memory access, and add, remove or
   flip `x` as cheaply;
2. combine with another set: "in both", "in either", "in exactly one";
3. tell you how many members it has, and which is the lowest;
4. be small enough that thousands of them fit in cache.

Point 2 is where other structures struggle. Intersecting two hashtables or
sorted arrays costs a lookup or a merge step per element. A bitset costs
one operation per *word*, whatever is in the sets.

### The usual suspects

**`bool[N]`** (Z's `alive[]`). One byte per position, so 8 times a bitset's
memory, and bulk operations touch `N` bytes instead of `N/8`.

**A struct of `bool` flags** (Z's `tile_properties_t`). Readable when you
ask about one flag of one item. It cannot answer "which tile types are
solid?" without walking every item.

**One `uint64_t`** (Z's `component_mask_t`). This *is* a bitset, with `N`
fixed at 64 and storage inline. Every operation is one instruction. It
stops working at the 65th member.

**Sorted array of members.** Memory proportional to members, not the
universe. Membership is O(log n); intersection is a merge.

**Sparse set** (RCL-1). O(1) everything plus packed iteration, at the cost
of an index per possible key: 32 or 64 times a bitset's single bit.

**Hashtable.** Pointless for small dense integers; set operations are per
element.

### Side by side

```text
                  test   set   count   first   and/or/xor   iterate   memory
------------------------------------------------------------------------------
bool[N]           O(1)   O(1)  O(N)    O(N)    O(N)         O(N)      N bytes
struct of bools   O(1)   O(1)  n/a     n/a     n/a          n/a       F bytes
uint64_t          O(1)   O(1)  O(1)    O(1)    O(1)         O(n)      8 bytes
sorted array      O(lgn) O(n)  O(1)    O(1)    O(n+m)       O(n)      n * I
sparse set        O(1)   O(1)  O(1)    O(n)    O(min(n,m))  O(n)      N*I+n*I
bitset            O(1)   O(1)  O(N/w)  O(N/w)  O(N/w)       O(N/w+n)  N/8 bytes

 N = universe size, n and m = members of the two sets, w = word width
 in bits, I = size of an index, F = number of flags.
```

Read that last column again. For N = 1024, a bitset is 128 bytes: two
cache lines. A sparse set over the same universe is several kilobytes
before it holds anything. Where the bitset loses is iteration when the
set is sparse: it has to look at every word, even empty ones, to find the
few members. If you mostly iterate a handful of members out of a huge
universe, prefer a sparse set. If you mostly test, combine and count, a
bitset is very hard to beat.

---

## 2. The mental model

### An analogy

Picture a hotel with `N` rooms, numbered from 0, each with a light that is
on when the room is occupied. The manager's control room does not show one
light per switch. The lights are wired in **panels** of `w`, and each panel
shows all its lights at once as a row. To ask "is anyone on this floor?"
she glances at one panel. To ask "which rooms are occupied *and* due for
cleaning?", she lays the two panels side by side and reads off where both
are lit, one panel at a time.

Two questions are hiding in that picture. First: given a room number, how
does she find its panel and its position on that panel? Second: the last
panel probably has more light positions than there are rooms left. What do
the positions with no room behind them show? Who decides? Hold on to both.

### The state, drawn

Real words are 64 bits, which does not fit on a page. This drawing uses
8-bit words. Universe N = 13, members {1, 4, 8, 10, 12}:

```text
  position:  0 1 2 3 4 5 6 7 | 8 9 10 11 12 | 13 14 15
  bit:       0 1 0 0 1 0 0 0 | 1 0  1  0  1 |  ?  ?  ?
             '---- word 0 ---' '------- word 1 --------'
                                              '-------'
                                         padding: not part
                                         of the universe

  members = {1, 4, 8, 10, 12}      count = 5      first set = 1
```

Two things to notice.

1. **Thirteen bits needed sixteen.** Storage comes in whole words, so the
   last word has 3 positions that do not correspond to any member of the
   universe. With 64-bit words and N = 600 (Z's demo map has exactly 600
   cells, see section 5), there are 40 such positions.
2. **Orientation is a convention.** This drawing puts position 0 on the
   left because people read that way. If you print word 0 as a number in
   hex or binary, the conventional bit order puts its lowest bit on the
   *right*, so the same state looks mirrored. Pick one picture, write it
   in your header comment, and draw your test traces with it.

### The invariants

These must hold after every public operation. They say *what* must be true.
*How* you keep them true is your job.

1. **Faithfulness.** For every `i` in `0 .. N-1`, `test(i)` is true exactly
   when `i` is a member.
2. **Locality.** Changing position `i` changes the answer of `test(j)` for
   no `j != i`.
3. **No phantom members.** No observable result (`test`, `count`,
   `find-first-set`, equality, and the result of any bulk operation) ever
   reports, counts, or is influenced by a position at or beyond `N`,
   **whatever bit pattern those padding positions hold in memory**.
4. **Closure.** A bulk operation over two bitsets of the same size yields a
   bitset of that size that also satisfies 1 to 3.
5. **Bounds.** Every position the caller can name is `< N`, and every
   access stays inside the allocated words.

Invariant 3 is the one this ticket is really about. Ask yourself: is there
one cheap rule about the padding bits that makes invariant 3 automatic for
every operation, or is it better for each operation to protect itself?
Which operations in the ticket could *create* a stray padding bit, and
which can only *suffer* from one?

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation           time          notes
----------------------------------------------------------------------
test / set /        O(1)          one word read, maybe one word write
clear / toggle
count               O(N/w)        one step per word, not per bit
find-first-set      O(N/w)        worst case; stops early when found
and / or / xor      O(N/w)        one operation per word pair
clear-all / fill    O(N/w)        not O(1), unlike a sparse set
create              O(N/w)        to initialize the words
iterate members     O(N/w + n)    every word is visited, even empty ones
```

The constant hidden in `O(N/w)` is tiny. For N = 1024 and 64-bit words,
`N/w` is 16. Sixteen iterations of a loop with no branches is often
cheaper than one hashtable lookup.

### Memory

- Whole words covering `N` bits, plus whatever header you keep. For
  N = 1024 that is 128 bytes of bits.
- For small N the header outweighs the bits. A heap-allocated bitset of 64
  positions is several times the size of the `uint64_t` it replaces, and
  adds an indirection. Whether RCL's bitset can live *inline* is one of
  your most important decisions (section 7).
- Memory never depends on the member count. An empty bitset over a million
  positions is 125 KB.

### Cache behaviour

- **Single-bit operations** touch one word; neighbouring positions share a
  word and a cache line.
- **Bulk operations** are linear walks over arrays of words: ideal for the
  prefetcher, and simple enough that compilers often auto-vectorize them.
  Check the assembly once yours exists.
- **Sparse iteration** is the weak spot. Visiting 3 members in a bitset of
  a million positions reads all 125 KB.

### Observable behaviour

A trace over a 13-position bitset, using the drawing convention above:

```text
new(13)                       count -> 0     first -> none
set 4, set 1, set 12          count -> 3     first -> 1
toggle 1                      count -> 2     first -> 4      test(1) -> false
toggle 8, toggle 10           count -> 4     first -> 4
clear 4                       count -> 3     first -> 8
set 12                        count -> 3     (already set: no change)
```

And a bulk operation, A and B both of size 13:

```text
A          = {1, 4, 8, 12}
B          = {4, 5, 12}
A and B    = {4, 12}            count -> 2
A or  B    = {1, 4, 5, 8, 12}   count -> 5
A xor B    = {1, 5, 8}          count -> 3
```

Check for yourself, with the 8-bit picture, that each result is one
operation per word pair. Then ask what happens to the 3 padding positions
of word 1 in each of the three operations, if A's padding happened to hold
`1 1 1` and B's held `0 0 0`.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, argument order,
> whether results go into a destination or overwrite the left operand,
> what "none" looks like, and how errors are reported are all yours to
> decide. It is loosely styled after RCL's `array_new(type, .opt = ...)`
> idiom.

### 4.1 Single positions

```c
bitset_t *b = bitset_new(.size = 100);

bitset_set(b, 3);
bitset_set(b, 64);          /* lives in a different word from 3 */
bitset_set(b, 99);          /* the last valid position */

assert(bitset_test(b, 64));
assert(!bitset_test(b, 65));

bitset_toggle(b, 3);        /* now clear */
bitset_toggle(b, 4);        /* now set */
assert(!bitset_test(b, 3) && bitset_test(b, 4));

bitset_clear(b, 99);
assert(bitset_count(b) == 2);   /* {4, 64} */

bitset_destroy(&b);         /* RCL style: takes T**, nulls it */
```

Positions 63, 64 and the last valid position are where most bugs live.
Use them in every test.

### 4.2 Count and find-first-set

```c
bitset_t *b = bitset_new(.size = 200);

assert(bitset_count(b) == 0);
assert(bitset_find_first(b) == BITSET_NONE);  /* what is "none"? your call */

bitset_set(b, 150);
bitset_set(b, 70);
assert(bitset_find_first(b) == 70);

bitset_clear(b, 70);
assert(bitset_find_first(b) == 150);
assert(bitset_count(b) == 1);
```

Note the second assertion. Whatever sentinel you choose for "no bit is
set", it must be impossible to confuse with a real position. Note also
that POSIX already has an `ffs()` in `<strings.h>` whose result is
**1-based**, with 0 meaning "none". If you borrow the name, people will
expect that convention.

### 4.3 Visiting every member, as a caller

```c
/* Prints members in ascending order, using only public operations. */
bitset_t *tmp = bitset_clone(b);
for (size_t i = bitset_find_first(tmp); i != BITSET_NONE;
     i = bitset_find_first(tmp)) {
  printf("member: %zu\n", i);
  bitset_clear(tmp, i);
}
bitset_destroy(&tmp);
```

This works, but costs an allocation plus a scan from the start per member:
O(n * N / w). The ticket does not ask for "find next set bit after `i`",
yet almost every bitset user ends up wanting it. Should RCL v1 offer it?

### 4.4 Bulk operations: a component query

```c
/* One bitset per component, indexed by entity id ("column" view). */
bitset_t *has_pos = bitset_new(.size = MAX_ENTITIES);
bitset_t *has_vel = bitset_new(.size = MAX_ENTITIES);
bitset_t *alive   = bitset_new(.size = MAX_ENTITIES);
/* ... filled as entities gain and lose components ... */

bitset_t *movers = bitset_new(.size = MAX_ENTITIES);
bitset_and(movers, has_pos, has_vel);    /* dst, a, b */
bitset_and(movers, movers, alive);       /* dst aliases a: allowed? */

printf("%zu movers\n", bitset_count(movers));
```

Two ANDs over 1024 entities are 32 word operations, whatever the
population. The second call passes the destination as an operand. Decide
whether that is allowed and say so in the header; for AND, OR and XOR it
is natural to support, but you should know why it is safe rather than
assume it.

### 4.5 Flags over a fixed universe: tile types

```c
/* One bitset per property, indexed by tile type id. */
bitset_t *solid  = bitset_new(.size = MAX_TILE_TYPES);
bitset_t *hazard = bitset_new(.size = MAX_TILE_TYPES);

bitset_set(solid, 12);
bitset_set(solid, 13);
bitset_set(hazard, 13);   /* spikes: solid and dangerous */
bitset_set(hazard, 40);   /* lava: dangerous, not solid */

if (bitset_test(solid, tile_id)) { /* resolve collision */ }

bitset_t *both = bitset_new(.size = MAX_TILE_TYPES);
bitset_and(both, solid, hazard);
assert(bitset_count(both) == 1 && bitset_test(both, 13));
```

The per-tile question ("is tile 13 solid?") stays O(1). The per-property
question ("how many solid tile types are there, and which is the first?")
is now O(N/w) instead of a walk over every tile type.

### 4.6 Traps

**Trap 1: the padding.** A bitset of size 70, so one full word and 6
positions in the second:

```text
b = new(70)
fill(b)                 /* or: xor with an "all ones" bitset */
count(b)                -> must be 70, not 128
toggle every i < 70     -> b is now empty
count(b)                -> must be 0
find_first(b)           -> must be none, not 70
```

If your design can let a padding bit become 1, one of those lines will
fail. Whether you have a `fill` or a `not` at all is a decision, but the
moment any operation can write whole words, the question is live.

**Trap 2: mismatched sizes.**

```c
bitset_t *a = bitset_new(.size = 70);
bitset_t *b = bitset_new(.size = 200);
bitset_or(a, a, b);   /* what happens to b's bits 70..199? */
```

Assert? Truncate? Grow `a`? Return an error? Undefined behaviour with a
loud comment? There is no universal answer, but there must be *an* answer.

**Trap 3: shifting a plain `int`.** The classic hand-built-mask bug:

```c
uint64_t m = 1 << 40;     /* undefined: 1 is an int, 40 >= its width */
uint64_t k = 1ULL << 40;  /* what was meant */
```

The same mistake can hide *inside* your implementation. Keep it in mind
when you write the word-level code, and test positions above 31 in every
word, not only the first.

---

## 5. Relation to Z

All references are to the working tree of `/Users/paulo/Projects/z` as I
read it (branch `feature/per-40`, with local edits in the demo and in
`engine/src/tilemap.c`). Line numbers may drift.

### 5.1 Component masks today

Z already uses a bitset. It just has a fixed size of 64.

- `component_mask_t` is `uint64_t` (`engine/src/zecs.h:14`), and the world
  holds one per entity in `masks[MAX_ENTITIES]` (`zecs.h:42`).
  `MAX_COMPONENT_TYPES` is 64 (`zecs.h:9`), which is exactly the width of
  that mask. The two numbers are tied together, though nothing in the code
  enforces it.
- Component ids are single-bit flags. Built-ins are `1 << 0` to `1 << 3`
  (`zecs.h:18-21`). User components start at `USER_COMPONENT_START`, 4
  (`zecs.h:35`), and the demo defines two with `1ULL << (...)`
  (`demo/src/player.h:6-7`). Note the difference: the built-ins shift a
  plain `int`, which is fine at those positions and would not be above 30
  (trap 3).
- **set / clear.** `zworld_add_X` sets the entity's flag bit
  (`zecs.h:69`) and `zworld_remove_X` clears it (`zecs.h:78`), each with
  one word operation on `masks[entity]`.
- **test of several bits at once.** `ENTITY_HAS_MASK` (`zecs.h:10`) is
  "every bit of `m` is also set in the entity's mask": a subset test, built
  from AND and equality. Every system uses it.
- **find-first-set.** `__builtin_ctzll(flag)` turns a one-bit flag into its
  index (`zecs.h:70`, `zecs.h:74`, `engine/src/zecs.c:99`). That is
  find-first-set on a single word, used as "flag to array slot".
- **iterate set bits.** `zworld_destroy_entity` (`zecs.c:77-93`) loops
  over all 64 positions and tests each (`zecs.c:79-84`) to decide which
  destructors to run. That is a member iteration done the slow, obvious
  way. At 64 positions it hardly matters, but it is the same shape as 4.3.
- **alive.** `bool alive[MAX_ENTITIES]` (`zecs.h:41`) is a set over
  entity ids stored as `bool[N]`: 1024 bytes where a bitset needs 128.

Every system I read loops `i` over `0 .. entity_count`, skips dead
entities, and skips mask mismatches: `demo/src/input.c:39-41`,
`demo/src/render.c:11-13`, `demo/src/physics.c:98-100` and `:167-169`,
`demo/src/scene.c:57-59`, and `collision_system`, which does it twice,
nested (`physics.c:72-79`).

### 5.2 How a bitset maps onto it

There are two different ways to use RCL's bitset here, and they are worth
keeping apart.

**Row view: a wider mask per entity.** Replace the `uint64_t` with a bitset
whose universe is the set of component types. This removes the 64-type
ceiling and nothing else. Be honest about the cost: a heap-allocated
bitset per entity means 1024 allocations, a pointer chase per
`ENTITY_HAS_MASK`, and a header bigger than the bits. This is only
attractive if RCL's bitset can be embedded by value with a size fixed at
compile time. With 64 or fewer components, `uint64_t` is already the
optimal bitset and RCL adds nothing.

**Column view: one set of entities per component.** Turn the matrix on its
side: for each component, a bitset over entity ids (1024 bits, 128 bytes),
plus one for `alive`. Then:

- The membership a system needs is a bulk AND of a few columns (example
  4.4): for `physics_system`, position AND velocity AND alive. That is 48
  word operations for 1024 entities, versus 1024 mask checks today.
- The system then visits only the members of the result. How cheaply it
  can do that depends on whether you offer find-next or a foreach
  (section 4.3).
- `collision_system`'s outer and inner loops could each run over a
  precomputed set (movers, solids), so the quadratic part scales with
  populations rather than with `entity_count` squared.
- Per-entity questions ("does entity 7 have a sprite?") become a test in
  the sprite column instead of a read of `masks[7]`.

Unlike a sparse set, positions never move, so none of the reordering
hazards from the sparse-set guide appear. To be fair to today's design:
with 1024 entities and a mask in a register, the current loops are fast.
The column view earns its keep as populations grow, or when you want counts
("how many enemies are alive?") for free.

### 5.3 Tile flags today

- `tile_properties_t` has two `bool`s, `solid` and `hazard`
  (`engine/src/tilemap.h:28-32`). The tilemap holds
  `tile_properties[MAX_TILE_TYPES]` (`tilemap.h:59`), with
  `MAX_TILE_TYPES` defaulting to 1024 (`tilemap.h:8-10`). That is 2 KiB of
  flags per tilemap.
- The array is zero-initialized by the compound literal in `ztilemap_new`
  (`engine/src/tilemap.c:262-270`). I found no code anywhere in `engine/`
  or `demo/` that writes or reads it.
- Collision ignores it. `tilemap_collision_system` treats any nonzero tile
  id on the ECS layer as solid (`demo/src/physics.c:121-123`).
- Layer data is an `int *`, one per map cell, converted from JSON numbers
  (`tilemap.c:38-41`). The demo loads `level1_notinfinite.tmj`
  (`demo/src/main.c:47`), whose `wall` and `ecs` layers are 30 x 20 =
  **600 cells**, with tile ids up to 171.

### 5.4 How a bitset maps onto tile flags

**Per-property sets over tile type ids** (example 4.5): one bitset each for
`solid` and `hazard`, 128 bytes each instead of 2 KiB of bools. Whether
that beats the struct depends on whether you ever ask questions *across*
tile types.

**Per-cell sets over the map.** A bitset of `width * height` positions per
layer, answering "is this cell solid?" without looking up the tile id and
then its properties. For the demo map that is 600 bits: 9 full 64-bit
words and a 10th with 24 used positions and 40 padding positions. That is
the "size not a multiple of the word size" case from the ticket, in your
own game. Neighbouring cells in a row share a word, which suits the
row-by-row scan in `tilemap_collision_system` (`physics.c:119-124`).

Do not confuse either with Tiled's flip and rotation flags, which live in
the top bits of each tile id: bits inside one integer, not a set.

### 5.5 What I could not verify

- Whether `tile_properties` is meant to be filled from Tiled tileset
  properties in a future ticket. Nothing populates it today.
- Whether any planned feature needs more than 64 component types. The demo
  uses 6.
- Whether the demo maps use flipped or rotated tiles. The demo map I
  checked has none (no id is near the flag bits), but I did not check how
  `tilemap.c:40` would treat one, since flagged ids exceed `INT_MAX`.
- Performance. I did not measure any of the loops above; every cost claim
  in this section is by counting operations, not by profiling.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. You have a position `i` and an array of `w`-bit words. Which word holds
   bit `i`, and which bit within that word? Write the answer in terms of
   `w`. Then ask: if `w` is a power of two, what cheaper operations compute
   the same two numbers, and will your compiler find them anyway?
2. How do you build a word that has exactly one bit set, at a given
   position, without falling into trap 3?
3. With that single-bit word in hand, which one bitwise operator gives you
   *set*, which *toggle*, which *test*, and what extra step does *clear*
   need?
4. For N = 13 and `w` = 8, how many words? For N = 16? N = 0? Find one
   formula that gives all three correctly.
5. How many bits of the last word are real, as a function of N and `w`?
   What is that number when N is an exact multiple of `w`? Does your formula
   from question 4 agree with it there?
6. Go through the ticket's operations one at a time. Which of them can turn
   a padding bit from 0 to 1? Which give a wrong answer if a padding bit is
   already 1? Is there an operation that is in *both* lists?
7. Given your answer to 6: is it cheaper to guarantee something about the
   padding once, after the few operations that could break it, or to guard
   against it in every operation that reads it? What does each choice cost
   a caller who never uses the dangerous operations?
8. Find-first-set must scan words, not bits. What test tells you that a
   whole word can be skipped? Once you have found a non-empty word, how do
   you get the position inside it without looping over its bits?
9. Counting must not test `N` bits one by one. What does the hardware or
   the compiler already know how to do with a single word?
10. In `and(dst, a, b)`, what can go wrong if `dst` is the same object as
    `a`? Work it out for word-by-word processing. Would the answer change
    for an operation that combined *different* word positions, like a
    shift?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Storage and lifetime**

- Heap-allocated and opaque (`bitset_t *`, like `array_t`), a value type
  the caller embeds, caller-provided storage, or several of these? Which
  one makes a 64-bit bitset cost no allocation?
- Size fixed at creation, or growable? If growable, what does a newly
  exposed region contain, and what does that do to invariant 3?
- Is `size = 0` legal?

**Word type**

- `uint64_t`, `unsigned long`, `size_t`, or overridable? Do you expose the
  raw words (serialization, hashing, debugging) or keep them private?

**Reaching the hardware**

- RCL builds as C99 but already uses GNU extensions (`__auto_type`,
  statement expressions in `array.h`). Do you use compiler builtins for
  count and find-first-set, write portable fallbacks, or both behind a
  macro? Z builds as C23, which has `<stdbit.h>`; RCL does not. Does that
  matter for a library header that Z includes?

**Semantics and naming**

- "Clear" means two things: clear one bit, and clear all bits. How do the
  names keep them apart? Same for "set" versus "set all", if you offer it.
- Find-first-set: 0-based or 1-based? What is "none": a sentinel equal to
  `size`, `SIZE_MAX`, a `bool` return with an out parameter?
- Do set, clear and toggle return the previous value? That is free to
  compute and sometimes very handy ("was this the first time?").
- Out-of-range position: assert, ignore, return an error?

**Bulk operations**

- Three-operand (`dst = a op b`) or two-operand in place (`a op= b`)? If
  three, is aliasing allowed and documented?
- Mismatched sizes (trap 2): assert, truncate, grow, or forbid by type?
- Beyond the ticket: AND-NOT (difference), NOT, subset test ("all of `m`
  is in `x`", which is `ENTITY_HAS_MASK`), intersects-at-all, equality.
  Which ones are worth it for v1? Subset and intersects can stop early;
  does your API let them?

**Iteration**

- Find-next-set from a position, a foreach macro, a callback like
  `hashtable_foreach`, or nothing in v1? Each has different costs and a
  different story for "what if the set changes while I iterate?".

**Padding policy**

- The question from section 2: one rule maintained everywhere, or every
  reader protects itself? Write down which you picked, in the header,
  because the next person to add an operation needs to know.

**Error model**

- RCL so far asserts and returns pointers. Same here, or status codes?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, locating a bit.** You have done this arithmetic many times for
> things that are not bits. Think of any unit that comes in fixed-size
> groups.

> **Hint B, the single-bit word.** The type of the `1` you shift decides
> what positions you can reach. Make the type of that literal match the type
> of your word, every time.

> **Hint C, clear.** The single-bit word is not the word you need for
> clear, but you can derive it from that word.

> **Hint D, padding.** Look at the trace in 4.6 trap 1. Count the
> operations in the ticket that read the last word as a whole. Then count
> the ones that can write arbitrary bits into it. One list is much shorter
> than the other.

> **Hint E, the last word's mask.** The number of real bits in the last
> word, from question 5, has one value that needs special care. Check what
> your shift does there.

> **Hint F, find-first-set.** Ask which words cannot possibly contain the
> answer. For the first word that can, reread section 5.1: Z already does
> the single-word half of this job.

> **Hint G, aliasing.** For word-by-word operations, ask whether word `k` of
> the output depends on anything other than word `k` of the inputs.

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/bitset_test.c` using Unity (`setUp` and `tearDown`,
`static void name(void)` cases, `RUN_TEST` in `main`, as in
`src/array_test.c` and `src/hashtable_test.c`), registered in `meson.build`
under `if get_option('tests')` like the others.

**Sizes to run everything against.** The ticket requires sizes that are
not a multiple of the word size. A good set: 1, `w - 1`, `w`, `w + 1`,
`2w - 1`, `2w`, a larger odd size such as 1000, and 600 for the Z demo map.
Add 0 if you decided it is legal. Consider looping each behaviour below
over that list rather than picking one size per test.

**Basics**

- A new bitset has count 0, find-first-set reports none, and `test` is
  false at every position.
- Setting one position makes `test` true there, false at its neighbours,
  and count 1. Do it at 0, at `w - 1`, at `w`, and at `size - 1`.
- Setting an already-set position does not change count. Clearing an
  already-clear one does not either.
- Toggling twice restores the original state.
- Locality: set every position one at a time; after each, count equals the
  number set so far and no other position changed.

**Count and find-first-set**

- Find-first-set returns the lowest member when members sit only in the
  first word, only in the last word, and only at `size - 1`.
- After clearing the lowest member, find-first-set moves to the next one.

**Bulk operations**

- AND, OR, XOR of two hand-built sets give the expected members and counts
  (the trace in section 3 is a ready-made case).
- Identities: `a and a == a`, `a or a == a`, `a xor a` is empty,
  `a xor empty == a`.
- Aliased destination, if allowed: the result is the same as with a
  separate destination.
- Mismatched sizes behave as documented.

**Padding (the ticket's real requirement)**

- For each non-multiple size: after any sequence of operations, count
  never exceeds `size`, and find-first-set never returns a position `>=
  size`.
- If you have fill or NOT: fill then count equals `size`; fill, toggle
  every real position, then count is 0 and find-first-set is none.
- XOR of two full bitsets is empty, with count 0.
- If your design lets a test reach the raw words, deliberately dirty the
  padding bits and check whichever behaviour your padding policy promises.

**Randomized against a reference**

- Thousands of mixed set, clear, toggle and bulk operations with a fixed
  seed, mirrored on a plain `bool[N]`. After each step compare `test` at
  every position, count, and find-first-set.

**ECS- and tile-shaped**

- Three column bitsets over 1024 entity ids: their AND holds exactly the
  entities in all three.
- A subset test, if you add one, agrees with `ENTITY_HAS_MASK` for masks
  that fit in 64 bits.

---

## 10. Further reading

Read these *after* your own attempt. Several of them show the word-level
tricks directly.

- **Henry S. Warren Jr., *Hacker's Delight*** (Addison-Wesley, 2nd ed.
  2012). The standard reference for bit manipulation: counting bits,
  finding bits, and why the tricks work.
- **Sean Eron Anderson, "Bit Twiddling Hacks"**, a long-running page from
  Stanford's graphics group. A catalogue of word-level techniques, many
  relevant to count and find-first-set.
- **C++'s `std::bitset` and `std::vector<bool>`, and Boost's
  `dynamic_bitset`.** Fixed and inline versus dynamic and heap-allocated:
  the storage decision from section 7, made three ways.
- **Java's `java.util.BitSet`**. Growable, with `nextSetBit` for
  iteration, and documentation that specifies its edge cases well.
- **The Linux kernel's bitmap API** (`include/linux/bitmap.h`, with
  `find_first_bit` and friends). Production C, careful about the last word.
- **Roaring Bitmaps** (Daniel Lemire and collaborators). Compressed bitsets
  for huge, sparse universes: the answer to this structure's weak spot.

---

## If you only remember three things

1. **A bitset is a set where each possible member is one bit, and its
   speed comes from working a word at a time.** Single-position operations
   are O(1); count, find-first-set and bulk operations are O(N / w), with a
   tiny constant.
2. **The end of the bitset is where the design is.** When N is not a
   multiple of the word size, the padding bits exist in memory but not in
   the set. Decide one policy that guarantees they never leak into any
   result, and test it at every awkward size.
3. **In Z, `component_mask_t` is already a 64-bit bitset.** RCL's bitset
   pays off when you go past 64 positions or turn the matrix on its side:
   one set of entities per component, or one set of cells or tile types per
   property, combined with bulk AND instead of per-entity loops.
