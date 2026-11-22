# Small Vectors: A Learning Guide

> For RCL-10 (inline storage for `array_t`) and, later, short lists in Z.
> This guide explains the idea and leaves the implementation to you. It has
> no push/grow/destroy code and no step-by-step algorithms. The C snippets
> show how a *caller* might use a small vector, against an API that is only a
> sketch, not a prescription.

## Summary

A **small vector** is a growable array that keeps its first **N** elements
*inside the container object itself*. While the array holds N elements or
fewer, it needs no separate buffer, so it costs no allocation for its
storage. When it outgrows N, it **spills** to the heap and from then on
behaves like an ordinary dynamic array.

It is a bet on a distribution. If most of your arrays are short and a few
are long, you pay a fixed size cost on every container (the inline area) to
remove an allocation, a free and a pointer chase from the common case. If
your arrays are usually long, or usually empty, the bet loses.

Ticket RCL-10 asks three things: a design note on whether this is a
**variant type** or an **option on the existing `array_t`**, an
implementation if it fits cleanly, and a benchmark that shows a win for
small sizes. This guide is meant to help you answer the first question with
evidence before you write the other two.

---

## 1. The problem

Look at what `array_new` costs today. `array_new_full`
(`src/array.c:6-18`) does **two** allocations: one for the `array_t` header
(`array.c:7`) and one for the element buffer (`array.c:10`), sized
`capacity * item_size`. With the default `ARRAY_DEFAULT_CAPACITY` of 32
(`src/rcl/array.h:8`), an array of pointers reserves 256 bytes before you
push anything. Destroying it is two frees.

For one long-lived array, nobody cares. The cost shows up when you create
*many short* arrays: JSON arrays with one to four values, the tile layers
of a small map, "the three things this entity overlapped this frame". Each
costs two trips through the allocator, two cache lines that are probably
far apart, and memory the allocator rounds up.

The README already points at this. It says the remaining gap against cJSON
on `mixed_100.json` "is due to per-node `malloc` overhead". JSON arrays are
created with capacity 4 (`src/json.c:13-14`, `:522-523`), which says that
someone already decided most JSON arrays are short.

### The usual suspects

**Plain dynamic array** (today's `array_t`). Unbounded, simple, one layout.
Every array costs at least one buffer allocation, plus one for the header
because `array_new_full` heap-allocates it.

**Dynamic array with a good initial capacity.** Removes *regrowth* for short
arrays but not the buffer allocation itself. JSON's capacity 4 is this.

**Fixed array plus count** (`T items[8]; size_t n;`). Zero allocations,
perfect locality, and a hard ceiling. The ninth element is a bug, a silent
drop, or an assert.

**Arena or bump allocator for the buffers.** Allocation becomes nearly free
and frees are batched. It changes *who owns the memory*, needs a lifetime
that matches the arena's, and still puts the elements somewhere other than
the header.

**`alloca` / VLA scratch.** Free and fast, stack-only, unbounded in a bad
way (no failure path), and it cannot outlive the function.

**Linked list.** One allocation per element. Worse on every axis here.

### Side by side

```text
                     allocs     allocs     size of     max     elements
                     n <= N     n > N      container   size    next to hdr
--------------------------------------------------------------------------
array_t today        2          2 (+grow)  40 B        any     no
array_t, small cap   2          2 (+grow)  40 B        any     no
fixed T[N] + count   0 *        -          N*T + 8     N       yes
arena-backed array   ~0         ~0         40 B        any     no
alloca / VLA         0          0          n*T         stack   n/a
small vector         0 or 1 **  +1 (+grow) 40 + N*T    any     while n<=N

 *  0 if embedded or on the stack, 1 if you heap-allocate the container
 ** depends on whether the container itself lives on the heap
 sizes assume 64-bit and today's five-field array_t
```

The footnotes are the honest part. In C++, an `llvm::SmallVector` usually
sits on the stack or inside another object, so "no allocation" really means
zero. RCL's `array_t` is always heap-allocated by `array_new_full` and
handed out as a pointer. If you keep that, a small vector takes a short
array from **two** allocations to **one**, not to zero. That is still worth
having, but it is a different headline, and it is the heart of the "variant
or option" question.

---

## 2. The mental model

### An analogy

Think of a coat with a few pockets and a backpack at home. For a short
errand, everything fits in the pockets: nothing to fetch, nothing to carry
separately, and your hands know where things are. On the day you need more
than the pockets hold, you bring the backpack, move *everything* into it,
and from then on you reach into the backpack for all of it. The pockets are
still sewn on. They are just not where the things are any more.

Three questions fall out of the analogy, and they are most of the design:

- How does the caller, reaching for "item 2", know whether to try the pocket
  or the backpack? Do they have to know at all?
- What happens to the pockets' contents on the day you switch?
- When the coat is handed to someone else, does what is in the pockets go
  with it? Does any note you wrote about where things are still make sense?

### The state, drawn

N = 4, element type `int`. Two observable states of one array:

```text
  state A: length 3, nothing on the heap

  +--------------------------- container ----------------------------+
  | header: length=3  item_size=4  free_func  ...                    |
  | inline area:  [ 10 ][ 20 ][ 30 ][ -- ]                           |
  +------------------------------------------------------------------+
                 elements live here: inside the container


  state B: length 6, spilled

  +--------------------------- container ----------------------------+
  | header: length=6  item_size=4  free_func  ...                    |
  | inline area:  [ ?? ][ ?? ][ ?? ][ ?? ]   (what it means now is   |
  |                                           your decision)         |
  +------------------------------------------------------------------+

  heap block:     [ 10 ][ 20 ][ 30 ][ 40 ][ 50 ][ 60 ][ -- ][ -- ]
                 elements live here: in a separate allocation
```

What the picture deliberately leaves out is how the header records *where*
the elements are, and what (if anything) the inline area is used for in
state B. Those are the decisions you are here to make.

### The invariants

These must hold after every public operation. They say *what* must be true.
*How* you keep them true is your job.

1. **One home.** At any moment, the live elements `0 .. length-1` are in
   exactly one place, either the inline area or one heap block, contiguous
   and in order. Never split across both.
2. **Same contents across a spill.** A push that causes a spill leaves
   elements `0 .. length-2` with the same values, in the same order, as
   before the push.
3. **Readers do not care.** Every public way of reaching element `i` gives
   the same answer whichever home is in use. In RCL this includes the
   *non-function* ways: `arr->data`, `ARRAY_OF(T)` casts, and the
   `array_push`, `array_map` and `array_filter_copy` macros, which all
   expand in the caller.
4. **Ownership is unambiguous.** The array can always tell whether it owns
   a heap block, and destroy frees exactly what was allocated, never the
   inline area.
5. **free_func semantics are unchanged.** Each stored element is passed to
   `free_func` exactly once over its lifetime (on delete or destroy), no
   matter how many times storage moved.
6. **Capacity is honest.** `length <= capacity`, and `capacity` is the
   number of elements that fit in the *current* home.

Invariant 3 is the one that makes this ticket interesting in RCL rather than
in a fresh library. Read `array.h` with it in mind before you design
anything.

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation                cost             notes
----------------------------------------------------------------------
create                   O(1)             no buffer allocation needed
push, n < N              O(1)             no allocator call
push that spills         O(N)             one allocation, happens once
push, n > N              amortized O(1)   as today
get element i            O(1)             may or may not cost a branch,
                                          depending on your design
delete / remove          O(n)             as today (memmove)
destroy, never spilled   O(n) free_func   no buffer free
```

The spill is the only new cost. It happens at most once per array life,
unless you decide to move back inline when an array shrinks (section 7).

### Memory

- Every container grows by `N * item_size` (plus any padding), **whether or
  not it is ever used**. Empty arrays and long arrays both pay it. That is
  the fixed cost of the bet.
- After a spill, the inline bytes are dead weight unless your design puts
  them to work.
- Today `sizeof(array_t)` is 40 bytes on a 64-bit ABI (`array.h:41-54`:
  one pointer, three `size_t`, one function pointer). Notice that 40 bytes
  of header plus three 8-byte pointers is exactly 64 bytes, one cache line
  on most current CPUs, if the allocator happens to line it up.
- Allocators round up. A 40-byte request and a 128-byte request each use
  more than they ask for. Folding two small requests into one usually
  wastes less than either did alone, but measure it on macOS and glibc
  rather than assuming.

### Cache behaviour

- **Inline:** the header and the first elements share cache lines. Reading
  `length` and then element 0 is often one miss, not two.
- **Spilled:** exactly today's behaviour, header in one place and elements
  in another.
- **Scanning many small arrays** (all the arrays inside a JSON tree, say) is
  where the effect compounds: each array visited costs one pointer chase
  fewer.

### Stability

- Today, pointers into `arr->data` are invalidated by any push that grows
  the buffer. That rule stays, and gains a new trigger: the spill at N.
- **New:** while inline, the elements live *inside the container*. Anything
  that moves the container moves the elements. With today's
  heap-allocated, pointer-only `array_t`, nothing moves the container. With
  a by-value variant type that can live on the stack or inside a struct,
  copying, returning or `realloc`ing the struct that holds it does.

Here is a trace of the observable behaviour of one plausible design, with
N = 4 and a test-only counter of heap allocations made for element storage
(yours may differ):

```text
create                    length 0  cap 4   storage allocs 0
push 10, 20, 30           length 3  cap 4   storage allocs 0
push 40                   length 4  cap 4   storage allocs 0
push 50                   length 5  cap 8   storage allocs 1   <- spill
read 0..4                 10 20 30 40 50
push 60, 70, 80, 90       length 9  cap 16  storage allocs 2   <- regrow
delete index 0 ... x8     length 1  cap 16  storage allocs 2   (no shrink)
destroy                   storage frees 1
```

Ask yourself what the line after `push 50` would read if your design
spilled to a heap block of exactly N+1 elements instead, and what that does
to the next few pushes.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, whether inline
> capacity is a runtime option or part of a type, and what the caller can
> observe are all yours to decide. It follows RCL's
> `array_new(type, .opt = ...)` idiom where that makes sense.

### 4.1 As an option on the existing array

```c
/* Up to 8 ints with no separate buffer. */
array_t *a = array_new(int, .inline_capacity = 8);

for (int i = 0; i < 5; i++)
  array_push(a, i * 10);

ARRAY_OF(int) *ints = (void *)a;      /* existing idiom, must keep working */
assert(ints->length == 5);
assert(ints->data[4] == 40);

array_destroy(&a);
```

Existing call sites that never pass `.inline_capacity` should not be able
to tell anything changed. Whether that is even achievable is a good test of
the "option" design.

### 4.2 As a variant type living inside another struct

```c
typedef struct {
  float x, y;
  SMALL_ARRAY(entity_t, 4) touching;   /* type-generated, like ARRAY_OF */
} contact_scratch_t;

contact_scratch_t s;
small_array_init(&s.touching);

small_array_push(&s.touching, 12);
small_array_push(&s.touching, 40);

for (size_t i = 0; i < s.touching.length; i++)
  handle(s.touching.data[i]);

small_array_clear(&s.touching);        /* reuse next frame */
small_array_fini(&s.touching);         /* frees only if it spilled */
```

Here the container is not heap-allocated at all, so a short list really
costs zero allocations. In exchange, notice what this API no longer is: it
is not an `array_t *`, so `array_sort`, `array_filter` and friends do not
accept it unless you decide how they should.

### 4.3 The JSON shape

```c
/* Parsing "[1, 2, 3]" today: 1 header + 1 buffer + 3 values. */
json_value_t *v = json_parse_assert("[1, 2, 3]");
array_t *arr = json_value_get_array(v);
/* With inline storage sized for 4 values: 1 container + 3 values. */
```

Keep the arithmetic honest. Every JSON value is its own allocation too
(`json.c:562` mallocs the wrapping `json_value_t`), so inline storage removes
one allocation of several. Whether that shows in `mixed_100.json` is what
the ticket's benchmark should tell you. Choose N from a histogram of real
final lengths, not from a round number.

### 4.4 A trap to keep in mind

```c
SMALL_ARRAY(int, 4) a;
small_array_init(&a);
small_array_push(&a, 1);
int *first = &a.data[0];

SMALL_ARRAY(int, 4) b = a;   /* struct copy: who owns what now?   */
small_array_push(&b, 2);     /* which array did that write into?  */
*first = 99;                 /* is first still a's element 0?     */
```

With today's `array_t`, a struct copy aliases one heap buffer, which is bad
but easy to reason about. With inline storage, the answer depends on
details of your design, and for some designs a byte-wise copy is not a
valid container at all. Decide what you promise, document it, and test it.

A second, quieter trap: `array_push` is a macro (`array.h:92-102`). Its
growth logic is expanded into every caller (`json.c:546`, Z's
`tilemap.c:232`). Whatever spilling requires, it has to happen inside that
macro or behind a function it calls. Read lines 96-99 and ask what the C
standard requires of the pointer you pass to `realloc`.

---

## 5. Relation to Z

All references are to the working tree of `/Users/paulo/Projects/z` as I
read it (branch `feature/per-40`, with local changes in `demo/` and
`engine/src/tilemap.c`). Line numbers may drift. I propose no edits here,
only a map of where arrays are and how their shape matches the idea.

### 5.1 Which RCL arrays Z uses

Z vendors RCL through `subprojects/rcl.wrap` at tag `v0.4`. Its
`subprojects/rcl/src/rcl/array.h` is byte-identical to
`rcl/src/rcl/array.h` (I diffed them). So anything that changes `array_t`'s
layout or `array_push` reaches Z's compiled code directly, because those
are in the header.

**Tilemap layers.** `ztilemap_new` builds `parsed_layers` with
`array_new(ztilemap_layer_t*, .capacity = layers->length, .free_func = ...)`
(`engine/src/tilemap.c:222-224`) and pushes each parsed layer
(`tilemap.c:232`). It is stored as `ztilemap_t.layers`
(`engine/src/tilemap.h:50`) and destroyed in `ztilemap_destroy`
(`tilemap.c:287`). The capacity is exact, so this array never regrows.

**Reading layers every frame.** `render_system` casts the array with
`ARRAY_OF(ztilemap_layer_t*)` and loops over `layers->data`
(`demo/src/render.c:35-38`). This is the one per-frame array read I found.
It reads `length` and `data` directly from the struct, so it depends on
invariant 3 and on `ARRAY_OF` mirroring `array_t`'s layout.

**JSON arrays during load.** The tilemap loader reaches into four JSON
arrays via `json_value_get_array` and direct `->data` casts: tile `data`
(`tilemap.c:28`, cast at `:40`), `objects` (`:75`, cast at `:84`), `layers`
(`:198`, cast at `:226`) and `tilesets` (`:213`, cast at `:236`). The
whole JSON tree is destroyed once the map is built (`tilemap.c:272`, a
local change in the working tree), so these arrays are short-lived.

**Engine callbacks.** `zengine_context_private_t` has
`array_t* callbacks_by_type[LAST_CALLBACK_TYPE]`
(`engine/src/zengine.priv.h:20`), destroyed in `zengine.c:147-149`. I
found no code in `engine/` or `demo/` that creates or pushes to them, so I
could not tell how long these lists get. "A few callbacks per event type"
is the textbook small-vector shape, if they get used.

### 5.2 What the data says

I counted the arrays in the demo's map, `demo/tilemaps/level1_notinfinite.tmj`
(loaded at `demo/src/main.c:47`):

```text
array                  length    today (header + buffer)
--------------------------------------------------------
layers                 3         2 allocs, capacity 4
tilesets               1         2 allocs, capacity 4
objects (one layer)    1         2 allocs, capacity 4
tile data (x2)         600       2 allocs + regrowth from 4
parsed_layers (Z)      3         2 allocs, capacity 3
```

Three of the five JSON arrays hold three elements or fewer, which supports
the "most arrays are short" bet. The absolute win in Z is small, though: a
handful of allocations, once, at load. The 600-element tile arrays pay for
1200 value nodes and several regrowths, and inline storage does nothing for
them except add the fixed inline cost. That is a useful sanity check for
your N.

### 5.3 Where it does not apply today

- **The ECS uses no RCL arrays.** `zworld_t` is fixed-size arrays sized by
  `MAX_ENTITIES` (`engine/src/zecs.h:37-48`), and component data is one
  `calloc` per type at world creation (`engine/src/zecs.c:28-32`). Nothing
  there is a short variable-length list.
- **No per-frame allocation in the systems I read.** `physics_system`
  (`demo/src/physics.c:161-182`), `collision_system` (`physics.c:64-86`),
  `tilemap_collision_system` (`physics.c:88-159`) and `render_system`
  allocate nothing. `collision_system` resolves each overlap immediately
  instead of collecting a contact list.

So in today's Z, a small vector would be a load-time and locality
improvement, not a frame-time one.

### 5.4 Shapes in Z where it would fit later

These are shapes, not proposals.

- **Per-entity short lists** such as children, attached effects or tags.
  Most entities have zero to three. A by-value variant could sit inside a
  component, and that is where the "container moves with its owner" rule
  bites: component arrays are fixed today (`zecs.c:30`), but a sparse-set
  ECS (see `sparse-set.md`) moves component data on removal.
- **Per-frame scratch** such as "everything this mover overlapped", if
  `collision_system` ever collects before resolving. Stack-resident, reset
  each frame, almost always short.
- **Arrays of pointers vs arrays of values.** `ecs_layer`
  (`tilemap.c:229-230`) points at a layer *object*, not into the array's
  storage, so moving the storage cannot invalidate it.

What I could not verify: how `callbacks_by_type` is populated, whether
other maps the game will load look like the demo map, and whether the
allocator on your target platforms makes the saved allocation measurable.

---

## 6. Guiding questions

Work through these in order before writing any code.

1. Where can element bytes live without any allocator call? List every
   place, including ones that only exist because of how `array_new_full`
   already works.
2. Every reader in RCL and Z evaluates `arr->data` and indexes it. For all
   of those readers to stay unchanged, what must be true of `data` in each
   mode? Is that compatible with every design you are considering?
3. The array has to know which mode it is in, for push and for destroy.
   What does it already store that differs between the two modes? Do you
   need a new field at all?
4. `array_push` passes `data` to `realloc`. What does the C standard
   require of that argument? What has to happen differently on the push
   that crosses N, and on the pushes after it?
5. `item_size` is a runtime value in `array_t`, but the inline area has a
   size. Where does that size come from in an "option" design, and where in
   a "variant" design? What does `sizeof` mean in each?
6. After a spill, the inline bytes hold stale copies. Is anything allowed
   to read them? Could they be put to work?
7. Copy the container to a new address with `memcpy`. Draw every pointer
   before and after. Is the copy a valid array? Is the original?
8. An array shrinks back below N. Should it move back inline? What could
   oscillate if it does, and how would you prevent that?
9. Inline elements sit after the header fields. What alignment does an
   element of type `double`, or a 16-byte SIMD type, need, and what does
   the header layout give it?
10. `ARRAY_OF(T)` is a hand-written mirror of `array_t`. What happens to
    every `ARRAY_OF` cast in RCL and Z if one of them changes and the other
    does not?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**The ticket's question: variant or option**

- An option on `array_t` (runtime N in `array_init_t`) keeps one type and
  one set of functions. Can the header stay a fixed-size struct, or does
  the allocation size become `f(N, item_size)`? What does that do to
  `ARRAY_OF`, to anyone who embeds an `array_t` by value, and to
  `sizeof(array_t)`?
- A variant type (compile-time `T` and N through a macro, like
  `ARRAY_OF`) can live on the stack or inside a struct and reach zero
  allocations. How does it interoperate with `array_sort`, `array_filter`,
  `array_delete`, `array_map`? Is a small array an `array_t` with a
  different allocation, or a different type that some functions accept?
- Would a hybrid be cleaner: one common header that both kinds start with,
  so functions take the header? What has to be true of the layout for that
  to be safe in C?

**Semantics**

- Is N in elements or in bytes? Bytes keep the container size predictable
  across element types. Elements are easier to reason about for callers.
- What is the default N, and is it zero (today's behaviour exactly)?
- What does `capacity` report while inline? Does a caller ever need to know
  which mode the array is in? If you expose it, is it API or test-only?
- What capacity does a spill go to: N+1, 2N, `ARRAY_DEFAULT_CAPACITY`, or
  the next power of two?
- Do you ever move back inline: on shrink, on an explicit call, never?
- Do you offer a "steal into heap" operation, or a way to hand the buffer
  to someone else (like `string_new_steal`)? What does it mean for an
  array that never spilled?

**Copy and move**

- Is a struct copy supported, forbidden by documentation, or made
  impossible to do by accident?
- If a variant container lives in a struct that gets `realloc`ed (a
  component array, a growing array of structs), what do you promise?

**The macros**

- Does `array_push` stay a macro? If its growth path becomes more complex,
  does that logic belong in a function, and what does that cost in a tight
  push loop?
- What do `array_map` and `array_filter_copy` produce from a small array:
  a small array, or a plain one?

**Debugging**

- Should dead inline bytes be poisoned in debug builds so stale reads fail
  loudly? Should ASan or Valgrind be able to see misuse of the inline area?

**Benchmark placement**

- `bench/` today holds only JSON fixtures; the harness is `src/json_bench.c`,
  built when `-Dbenchmarks=true` (`meson.build`), and driven by
  `scripts/bench-*.sh`. Does an array benchmark become a second executable
  beside `json_bench`, and do its inputs or results go in `bench/` as the
  ticket says?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points in a direction and stops there.

> **Hint A, where the bytes go.** `array_new_full` already makes one
> allocation that has nothing to do with elements. Ask how big it has to
> be.

> **Hint B, readers.** If every reader already goes through one field, the
> cheapest design is one where readers never learn there are two modes.

> **Hint C, mode.** Before you add a flag, ask which existing value would
> have to be different in the two states anyway.

> **Hint D, the spill.** Two kinds of memory, and one standard function
> that only understands one of them.

> **Hint E, copies.** Draw the container at address A, copy it to address
> B, and redraw every arrow. Any arrow that still points into A is your
> answer.

> **Hint F, dead space.** A field that means nothing in one mode is space
> you could reuse in that mode. Decide whether the saving is worth the
> complexity.

> **Hint G, the boundary.** Test the push that fills the inline area
> exactly, and the one right after it, separately.

---

## 9. Test ideas

These are behaviours to check, not test code. They follow the existing RCL
pattern: Unity cases as `static void name(void)` in `src/array_test.c` (or a
new `src/small_array_test.c`), each run with `RUN_TEST` in `main`, with the
file registered in `meson.build` under `if get_option('tests')`.

**Regression first**

- Every existing case in `array_test.c` (`array_basic_usage` through
  `array_test_filter_copy2`) passes unchanged with the default settings.
  If you go the "option" route, also run them with inline capacity
  switched on.
- `json_test.c` passes unchanged. JSON arrays are the biggest existing user.

**Inline behaviour**

- A new array is empty and has the documented capacity.
- Pushing up to N elements makes no storage allocation. Decide how a test
  observes this: a test-only counter, an allocation hook, or a mode query.
- Elements read back correctly through `->data` and through an `ARRAY_OF`
  cast while inline.

**The spill**

- Push N, then N+1: all N+1 values present, in order.
- Push far past N (the `array_grows` shape with 1000 elements).
- N = 1 and N = 0 behave.
- Large element types (a 64-byte struct) and strictly aligned ones
  (`double`) store and read back correctly; check alignment of `->data`.

**Ownership**

- `free_func` runs exactly once per element on destroy, for an array that
  never spilled and for one that did (count calls with a test free func).
- `array_delete` and `array_remove` at index 0, the middle, and the end,
  both inline and spilled.
- Destroying an inline array frees nothing it did not allocate. Run the
  suite under ASan or Valgrind; a free of a non-heap pointer is the bug to
  catch.

**Other operations**

- `array_sort`, `array_filter`, `array_map` and `array_filter_copy` on an
  inline array give the same results as on a heap one.
- Shrinking below N behaves as documented (moves back or does not).
- Copy semantics behave as documented, including the documented failure
  mode if copying is forbidden.

**Stress**

- A randomized sequence of push/delete/remove/sort against a plain C array
  reference with a fixed seed, comparing contents after every step, for N
  in {0, 1, 4, 8}.

**Benchmark (for the ticket's last bullet)**

- Create, push k, read, destroy, for k from 0 to 64, with and without
  inline storage. Plot time per array and allocations per array against k.
  The win should be visible for k <= N and roughly flat beyond.
- The same loop with long arrays (10,000 elements) to show no regression.
- `json_bench` on `bench/mixed_100.json` and `bench/500_keys.json`, before
  and after, since those are dominated by small containers.
- Run on both macOS and Linux (your Fedora box), since allocator costs
  differ.

---

## 10. Further reading

Named for reading after your own attempt, since most of these show the core
trick directly.

- **LLVM `SmallVector`** (`llvm/ADT/SmallVector.h`) and the *LLVM
  Programmer's Manual* section on it. The best-known small vector, and a
  good discussion of why it has a size-independent base class.
- **Abseil `absl::InlinedVector`**, **Boost.Container `small_vector`** and
  **Folly `small_vector`**. Three C++ takes with different choices on size
  overhead and on what the inline space is used for after a spill.
- **Rust `smallvec` crate.**
- **Small string optimization** in `std::string` implementations
  (libstdc++, libc++). The same idea applied to strings, and directly
  relevant to RCL's `string_t`.
- **Chandler Carruth, "Efficiency with Algorithms, Performance with Data
  Structures"**, CppCon 2014. Argues for contiguous, allocation-light
  containers, with `SmallVector` as an example.
- **Ulrich Drepper, "What Every Programmer Should Know About Memory"**
  (2007). Background for the cache-line arithmetic in section 3.

---

## If you only remember three things

1. **A small vector moves the first N elements into the container.** It
   trades a fixed size cost on every array for one less allocation, free
   and pointer chase on short ones. Measure the length distribution before
   picking N.
2. **In RCL the hard part is compatibility, not the idea.** `arr->data`,
   `ARRAY_OF` casts and the `array_push` macro all expand in the caller, so
   whatever you build has to look the same to them in both modes. That
   decides "variant or option" more than anything else.
3. **Inline elements live where the container lives.** Spilling moves
   them, and moving or copying the container moves them too. In Z that
   matters most for future per-entity and per-frame lists, not for today's
   load-time arrays.
