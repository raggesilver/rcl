# Binary Heaps and Priority Queues: A Learning Guide

> For RCL-7 (`rcl_heap`) and, later, A* pathfinding, timers and scheduled
> events in Z. This guide explains the idea and leaves the implementation to
> you. It has no push/pop/update code and no step-by-step algorithms. The C
> snippets show how a *caller* might use a heap, against an API that is only
> a sketch.

## Summary

A **priority queue** is a collection where you can always ask "which element
is the smallest?" and take it out, while new elements keep arriving in any
order. "Smallest" is whatever a **comparator** you supply says it is: the
earliest fire time, the lowest path cost, the highest urgency.

A **binary min-heap** is the classic way to build one. It supports:

- **peek** at the minimum in **O(1)**;
- **push** a new element and **pop** the minimum in **O(log n)**;
- with some extra bookkeeping, **update** an element's priority or **remove**
  an arbitrary element in **O(log n)**;
- building a heap from n unordered elements in **O(n)**, which is better
  than n pushes.

It does this with a single flat array and **no pointers**. The structure is
"a tree", but you never allocate a node. Its secret is that it is only
*partially* ordered: it keeps exactly enough order to find the minimum
cheaply, and no more. Most of the interesting design work is in what "just
enough order" means, and in what the heap must promise its callers when
elements move around inside it.

---

## 1. The problem

You have a stream of work items, each with a priority: timers ("fire at
tick 4810"), a search frontier ("this node has estimated cost 17.5"),
scheduled events ("spawn wave 3 at 90 s"). Items arrive in arbitrary
order, and the consumer repeatedly wants **the best one right now**. You
want to:

1. add an item quickly;
2. see the best item quickly (often every tick, just to ask "is anything
   due yet?");
3. remove the best item quickly;
4. sometimes change an item's priority, or cancel it, after it was added.

You do *not* need the items in fully sorted order at any moment. That is the
opening a heap exploits.

### The usual suspects

**Unsorted array** (an RCL `array_t`). Push is an append. But peek and pop
both scan everything. If the consumer peeks once per tick to ask "anything
due?", you pay O(n) every tick, even when nothing is due.

**Sorted array.** Peek and pop are trivial (keep it sorted so the minimum
sits at the cheap end). Push has to find a spot and shift everything after
it: O(n) moves. Fine if items arrive rarely and are consumed often, poor
for a search frontier, where pushes outnumber pops.

**Balanced binary search tree** (red-black, AVL, skip list). Everything is
O(log n), you get ordered iteration, and arbitrary removal falls out
naturally. The price: a node allocation and about three pointers per
element, pointer chasing on every step, and a lot more code. Right if you
need "all timers between t1 and t2"; overkill if you only want the minimum.

**Bucket queue / timing wheel.** If priorities are small integers in a
bounded range, keep one list per priority value and push in O(1). Timing
wheels (kernel and network-stack timeouts) are this idea with wrap-around.
They depend on key range and granularity; they are not general-purpose.

### Side by side

```text
                    push     pop-min  peek     update   build      memory
---------------------------------------------------------------------------
unsorted array      O(1)*    O(n)     O(n)     O(1)a    O(1)       n*T
sorted array        O(n)     O(1)b    O(1)     O(n)     O(n lg n)  n*T
balanced BST        O(lg n)  O(lg n)  O(1)c    O(lg n)  O(n lg n)  n*(T+3P)
binary heap         O(lg n)  O(lg n)  O(1)     O(lg n)d O(n)       n*T
bucket queue        O(1)     O(C)e    O(C)e    O(1)     O(n)       C*P+n*T

 n = elements, T = element size, P = pointer size, C = number of distinct
 priority values, * = amortized (growth)
 a = once you know where the element is; pop still has to scan
 b = if the minimum is kept at the end, so removing it shifts nothing
 c = if you cache the leftmost node; O(lg n) otherwise
 d = only if you can find the element in O(1); a search is O(n)
 e = scanning for the next non-empty bucket
```

None of these is "bad". The heap's niche is: **you only ever need the
minimum**, keys are arbitrary (floats, 64-bit ticks, composite keys), you
want one contiguous allocation, and you want the code to stay small. That
describes A*'s open set and a game's timer queue very well.

Footnote d is the one to keep an eye on. The ticket says "ideally a way to
update or remove an element". That word "ideally" hides most of the API
design in this guide.

---

## 2. The mental model

### A strict org chart

Picture an org chart where the only rule is: **every boss is at least as
senior as each of their direct reports**. Each boss has at most two
reports. Follow the rule up any chain and the person at the top is the
most senior in the company. You can name them instantly.

Notice what the chart does **not** tell you. It does not rank two peers
against each other, or someone in sales against someone three levels down
in engineering. Most comparisons were never made, because nobody needed
them to find the top.

Now two things happen:

- **Someone is hired** (push). They must be placed somewhere, and the rule
  must hold afterwards. How many people do they need to be compared with,
  and which ones?
- **The top person retires** (pop). There is a hole at the top. Who is even
  eligible to fill it? How many candidates are there?

If the chart is well balanced, the answer to "how many comparisons" is
never much more than the number of levels.

### The state, drawn

Push 5, 3, 8, 1, 9, 2 into an empty min-heap. Here is one valid state
afterwards (your implementation may produce a different, equally valid
one):

```text
                 [1]
               /     \
            [3]       [2]
           /   \     /
         [5]   [9] [8]
```

Read what is and is not true:

- Every parent is `<=` each of its children: 1 <= 3, 1 <= 2, 3 <= 5,
  3 <= 9, 2 <= 8.
- The minimum, 1, is at the top.
- Siblings are in no particular order (3 is left of 2).
- Levels are not sorted either: 5 is one level below 2 and 5 > 2, but 8
  sits on the same level as 5. Nothing relates different branches.
- The tree is **full on every level except the last**, and the last level is
  **filled from the left** with no gaps.

Two counterexamples:

```text
   not a heap (order)            not a heap (shape)

          [1]                           [1]
         /   \                         /   \
      [7]     [2]                   [3]     [2]
     /   \                                 /   \
   [5]   [9]                             [8]   [9]

   7 > 5: a parent is greater          a gap on the left of the last
   than its child                      level; the shape is not "complete"
```

The second one satisfies the ordering rule, so why care about its shape?
The shape rule is what lets the whole thing live in **one flat array with
no pointers**, and what keeps the height at about log2(n). The diagram is a
tree; your memory will hold an array. Working out how one maps onto the
other is your first job (section 6, questions 1 and 2).

### The invariants

These must hold after every public operation. They are *what* must be true.
*How* you keep them true is your job.

1. **Heap order.** For every element `x` that has a parent `p`,
   `cmp(p, x) <= 0`. Consequently the root is a minimum of the collection.
2. **Shape.** The elements form a *complete* binary tree: every level full
   except possibly the last, which is filled left to right.
3. **Storage.** Exactly `count` elements are stored, contiguously, with no
   holes. Slots at or beyond `count` mean nothing.
4. **Comparator consistency.** The comparator is a strict weak ordering and
   does not change its mind about two elements while both are in the heap.
   This one is a precondition on the *caller*, not something the heap can
   enforce. Everything else silently breaks if it is violated.
5. **Position tracking** (only if you offer update/remove by handle). For
   every live element, whatever the caller holds to identify it resolves to
   that element's *current* location, even though elements move on every
   push and pop.

Invariant 4 is the one that bites in practice (section 4.6). Invariant 5 is
the one that turns a 60-line heap into a real design problem.

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation            time         notes
-----------------------------------------------------------------------
peek()               O(1)         the root
push(x)              O(log n)     amortized if the array grows
pop()                O(log n)
count(), empty()     O(1)
update(h, ...)       O(log n)     needs O(1) lookup of h's position
remove(h)            O(log n)     same requirement
build(array of n)    O(n)         not O(n log n); worth proving to yourself
contains(x)          O(n)         a heap is not a search structure
iterate              O(n)         in no useful order
```

Each O(log n) comes from the height of the tree: an operation never touches
more than a constant number of elements per level. With n = 600 (every tile
of Z's current level at once, see section 5), that is about 10 levels.

The O(n) build is surprising. Most people's first guess is O(n log n),
because "n elements, each moved at most log n levels". Ask yourself how many
elements are near the bottom of a complete tree, and how far each of them
can possibly have to travel.

### Memory

- **n x sizeof(T)** plus growth slack. No per-element pointers, no per-node
  allocation, one `malloc` for the whole thing.
- Handles (invariant 5) add whatever your handle scheme costs.
- Elements are **moved** on every push and pop (O(log n) moves each). A
  64-byte `T` means a lot of `memcpy`; a pointer `T` means cheap moves but
  a pointer chase per comparison (section 7).

### Cache behaviour

- **peek** touches one element, which is almost always hot.
- **push and pop** walk one path between the top and the bottom. The top
  levels are tiny and stay in cache. Deeper on the path, elements tend to
  be far apart in memory, so large heaps miss cache near the bottom. Once
  you have decided on a layout, work out *how* far apart.
- A few thousand small elements sit in L1/L2 and the heap is very fast.
  Variants with more than two children per node (4-ary heaps) trade more
  comparisons per level for fewer levels. Worth knowing; not needed for v1.

### Order and stability

- Iterating the storage gives **no useful order**. Only repeated pops give
  sorted order.
- A heap is **not stable**: equal elements come out in an unspecified
  order that depends on the history of pushes and pops.
- Elements **move** on every push and pop, including unrelated ones. A
  pointer into the heap's storage is not a stable reference.

A trace of the observable behaviour:

```text
push 5, 3, 8, 1, 9, 2      peek -> 1     count -> 6
pop                        -> 1          peek -> 2
pop                        -> 2
pop                        -> 3          count -> 3
push 0                     peek -> 0
pop, pop, pop, pop         -> 0, 5, 8, 9
pop                        -> (empty: your documented behaviour)
```

And a trace with ties, using timers ordered by fire tick only:

```text
push {tick 10, "a"}, {tick 10, "b"}, {tick 5, "c"}
pop  -> {5, "c"}
pop  -> {10, "a"} or {10, "b"}   (unspecified!)
pop  -> the other one
```

If "a" was scheduled before "b" and your game logic assumes it fires first,
you have a nondeterminism bug that may only appear after an unrelated
change shuffles the heap. Section 4.3 shows the usual caller-side fix.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, the argument order, how
> the element type is passed, what pop does on an empty heap, and what a
> handle looks like are all yours to decide. It is loosely styled after
> RCL's `array_new(type, .opt = ...)` idiom (`src/rcl/array.h:70`) and
> reuses the shape of `array_compare_func` (`src/rcl/array.h:30`).

### 4.1 A min-heap of ints

```c
static int cmp_int(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);          /* not x - y; see 4.6 */
}

heap_t *h = heap_new(int, .cmp = cmp_int);

heap_push(h, &(int){5});
heap_push(h, &(int){3});
heap_push(h, &(int){8});

assert(heap_count(h) == 3);
assert(*(int *)heap_peek(h) == 3);

int out;
heap_pop(h, &out);                   /* out == 3 */
heap_pop(h, &out);                   /* out == 5 */

heap_destroy(&h);                    /* RCL style: takes T**, nulls it */
```

### 4.2 A max-heap is the same heap

```c
static int cmp_int_desc(const void *a, const void *b) {
  return cmp_int(b, a);              /* swap the arguments */
}

heap_t *top = heap_new(int, .cmp = cmp_int_desc);
/* peek now returns the largest element. Nothing else changes. */
```

A "min-heap with a user comparator" is therefore also a max-heap and a
priority-by-anything heap.

### 4.3 A timer queue, drained once per tick

```c
typedef struct {
  uint64_t fire_tick;    /* when, in simulation ticks */
  uint64_t seq;          /* insertion counter: tie-breaker */
  int      kind;         /* what to do */
  size_t   target;       /* e.g. an entity id */
} timer_ev_t;

static int cmp_timer(const void *a, const void *b) {
  const timer_ev_t *x = a, *y = b;
  if (x->fire_tick != y->fire_tick)
    return (x->fire_tick > y->fire_tick) - (x->fire_tick < y->fire_tick);
  return (x->seq > y->seq) - (x->seq < y->seq);
}
/* (Not named timer_t: POSIX already uses that name.) */

/* Every simulation tick: */
timer_ev_t *next;
while ((next = heap_peek(timers)) && next->fire_tick <= now_tick) {
  timer_ev_t t;
  heap_pop(timers, &t);
  dispatch(&t);          /* may push new timers, including ones due now */
}
```

Points worth noticing, all on the caller's side:

- "Anything due?" is one O(1) peek, even with thousands of pending timers.
- The `seq` field makes equal fire times come out FIFO, deterministically.
  The heap itself did not change.
- `dispatch` may push a timer that is already due. The loop above fires it
  in the same tick. That is a policy decision, and also a way to write an
  infinite loop.
- `next` is only used before the pop. After any push or pop, it may point
  at a different timer.

### 4.4 Cancelling and rescheduling: handles

```c
heap_handle_t hh =
    heap_push(timers, &(timer_ev_t){ .fire_tick = now + 240 });

/* The player picked up a power-up: extend the effect. */
timer_ev_t *t = heap_get(timers, hh);
t->fire_tick += 120;
heap_update(timers, hh);       /* "I changed its key; fix yourself up" */

/* The entity died: cancel its timer. */
heap_remove(timers, hh, NULL);
```

This is the ticket's "update or remove" as a caller sees it. Everything
interesting hides in `heap_handle_t`: it must identify the same timer after
hundreds of unrelated pushes and pops have moved it. What it is, and what
happens when it outlives its element, are open questions (section 7).

### 4.5 The A* open set

```c
typedef struct { float f; uint32_t node; } open_t;

static int cmp_open(const void *a, const void *b) {
  float x = ((const open_t *)a)->f, y = ((const open_t *)b)->f;
  return (x > y) - (x < y);
}

heap_push(open, &(open_t){ .f = h(start), .node = start });
g[start] = 0;

open_t cur;
while (heap_count(open) > 0) {
  heap_pop(open, &cur);
  if (cur.node == goal) break;
  if (closed[cur.node]) continue;          /* see the note below */
  closed[cur.node] = true;

  for (each neighbour nb of cur.node) {
    float g2 = g[cur.node] + cost(cur.node, nb);
    if (g2 < g[nb]) {
      g[nb] = g2;
      came_from[nb] = cur.node;
      heap_push(open, &(open_t){ .f = g2 + h(nb), .node = nb });
    }
  }
}
```

This version never updates anything. When a node's cost improves, it pushes
a second, better entry, and the `closed` check discards the outdated one
when it is popped. Textbooks usually use "decrease-key" instead, which is
the update-by-handle of 4.4. Both are correct (given a consistent
heuristic). Which one you want decides whether A* needs handles at all: how
many extra entries can duplicates create, and what does each approach cost
in memory and in code?

### 4.6 Traps

**The subtraction comparator.**

```c
static int cmp_bad(const void *a, const void *b) {
  return *(const int *)a - *(const int *)b;    /* overflows */
}
```

With `INT_MIN` and `1`, the subtraction overflows (undefined behaviour; in
practice it wraps), so `INT_MIN` compares as *greater* than 1. The heap
silently violates invariant 1 and every later pop is suspect. The same goes
for `(int)(x - y)` on `uint64_t` ticks.

**NaN in a float comparator.** If an A* cost becomes NaN, the comparator in
4.5 reports it "equal" to everything, which is not a consistent ordering
(invariant 4). The heap will not crash. It will return wrong minimums.

**Changing a key behind the heap's back.**

```c
timer_ev_t *t = heap_peek(timers);
t->fire_tick += 1000;          /* heap was not told */
/* peek() now returns an element that is not the minimum. */
```

A mutable `heap_peek` makes this easy to write. Decide whether it returns
`const`, whether you offer an "update the top" shortcut, and how a debug
build could detect this.

---

## 5. Relation to Z

All references are to the working tree of `/Users/paulo/Projects/z` as I
read it (branch `feature/per-40`, with uncommitted changes in `demo/src`
and `engine/src/tilemap.c`). Line numbers may drift. This section describes
what exists and how a heap would map onto it; it does not propose changes.

### 5.1 How time and scheduling work today

**The loop.** `main` (`demo/src/main.c:33-98`) runs a fixed-timestep loop.
`FIXED_DT` is 1/240 s (`main.c:12`). Wall-clock time comes from
`SDL_GetTicks` (`main.c:53`, `:58`), each frame's elapsed time is clamped to
0.25 s (`main.c:62`) and added to an accumulator (`main.c:63`). The inner
loop (`main.c:79-83`) runs `movement_system` and `physics_system` once per
`FIXED_DT` step. Systems are called directly in a fixed order; there is no
scheduler or system registry.

**Periodic work.** The only time-based action I found is `log_fps`
(`main.c:16-31`): two `static` variables and a per-frame "has a second of
wall time passed?" check. A hand-written repeating timer on wall-clock
milliseconds, not simulation time.

**State instead of timers.** Jumping is a flag, not a timer. A press is
detected as an edge between `keys` and `prev_keys` (`demo/src/input.c:46-47`),
sets `player->jumping` (`input.c:48-51`), and landing clears it in
`tilemap_collision_system` (`demo/src/physics.c:150-153`). Movement is
integrated with `dt` (`input.c:60-67`, `physics.c:174-177`), but nothing
counts down.

**What I did not find.** Searching `engine/src` and `demo/src` for timer,
delay, cooldown and similar terms found no timers, cooldowns, delayed
events, spawn schedules, coyote time or jump buffering, and no enemies, AI
or pathfinding. There is no current consumer for a heap in Z. Everything
below is about where one would plug in.

### 5.2 Timers and scheduled events

- **What the key would be.** Two clocks exist: wall time (`SDL_GetTicks`)
  and simulation time (the number of `FIXED_DT` steps taken, not stored
  anywhere today). Gameplay timers belong to simulation time: the
  accumulator runs zero or several steps per frame, and the 0.25 s clamp
  (`main.c:62`) deliberately lets the game fall behind wall time after a
  stall. An integer tick counter is a clean key: exact comparisons, no
  float drift.
- **Where the drain would go.** Inside the fixed-step loop
  (`main.c:79-83`), once per step, as in 4.3. Whether timers fire before or
  after movement and physics is a game decision.
- **Payloads that reference entities.** A scheduled event usually names an
  entity ("remove this pickup", "end this player's invulnerability").
  `entity_t` is a plain `size_t` with no generation bits
  (`engine/src/zecs.h:12`). `zworld_destroy_entity` pushes the id onto the
  free list (`engine/src/zecs.c:91`), and `zworld_create_entity` hands
  recycled ids out first (`zecs.c:64-65`). So an event for entity 7 can
  fire after 7 died and the id went to a new entity. A heap neither causes
  nor fixes this. Either the timer is cancelled on destroy (remove by
  handle, 4.4), or validity is checked when it fires, which needs an id
  generation that Z does not have yet.
- **When a heap is overkill.** For a handful of per-entity cooldowns, a
  countdown component decremented by a system every step is simple and
  cheap. A heap pays off with many pending events, mostly far in the
  future, where "nothing is due" is the common answer and should be O(1).

### 5.3 A* on the tilemap

- **The graph.** The obvious grid is the tilemap's ECS layer:
  `ztilemap_layer_t` has `width`, `height` and an `int *data`
  (`engine/src/tilemap.h:34-43`), and the map points at the first layer
  named "ecs" (`tilemap.h:57`, chosen in `engine/src/tilemap.c:229-230`).
  Collision reads it as `data[h * width + w]` and treats tile id 0 as empty
  (`demo/src/physics.c:121-123`). The level the demo loads,
  `level1_notinfinite.tmj`, is 30 x 20 tiles of 16 px, so 600 nodes.
- **What the heap would hold.** The open set: `{f, node}` pairs as in 4.5,
  where `node` is the tile index. Because node ids are small dense integers
  (0 .. 599), the g-scores, `came_from` and the closed set can be plain
  arrays indexed by node, or a sparse set from RCL-1 if you want an O(1)
  reset between searches. Only the open set needs a priority queue.
- **Sizes.** With 600 nodes the open set stays in the hundreds of entries
  (more with duplicates): about 10 levels, a few KiB, all in cache. Heap
  speed will not be the bottleneck; comparator correctness and the update
  story matter more.
- **A caveat outside the heap.** The demo is a platformer with gravity
  (`physics.c:11`, `:174`). For a walking or jumping enemy, "neighbouring
  empty tile" is not "reachable", so a real graph would model ledges and
  jump arcs. That is a Z question, not a heap question, but it decides the
  node and edge counts.

### 5.4 Seams to watch

1. **Pointers into the heap.** Z code holds pointers to component data
   across other calls (`spawner` in `demo/src/scene.c:61` while five
   components are added). That is safe today because component arrays never
   move. A pointer from `heap_peek` is not safe across any push or pop.
2. **Determinism.** Replays or lockstep networking would need equal-tick
   timers to fire in a fixed order: a tie-breaker in the key (4.3).

What I could not verify: how many timers or events Z will eventually have,
whether enemies or any pathing are planned, and whether simulation ticks
will be counted anywhere. Nothing in the current tree consumes a priority
queue, so the mapping above is about future systems.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. A complete binary tree has no gaps when you read it level by level, left
   to right. Number the slots in that reading order. Given a slot's number,
   can you compute the numbers of its children and its parent without
   storing any pointers? Try it on paper with the drawing in section 2.
   Does it matter whether you start numbering at 0 or at 1?
2. Why does the shape invariant matter for question 1? What breaks if the
   tree had a gap, like the second counterexample?
3. Push must keep invariant 2. Given that, how many places in the tree can
   a new element possibly start in? After it lands there, which single
   relationship might now be violated, and with whom?
4. If you fix that one relationship, can a new violation appear? Where? How
   many times can this repeat at most?
5. Pop removes the root, but invariant 2 says the *shape* after a pop is
   completely determined. Which slot must disappear from the shape? How
   could that help you fill the hole at the top?
6. Suppose an element sits in some slot and may be larger than what is
   below it. Which elements could legitimately take its place? How do you
   choose among them without breaking invariant 1 somewhere else?
7. Question 3/4 and question 5/6 describe two "repairs" that move in
   opposite directions. Now suppose an element in the *middle* gets a new
   key (update). Which repair does it need, and how do you know which one
   from the old and new keys? Can it ever need both?
8. Arbitrary remove: you want an element in the middle gone, and the shape
   must shrink by exactly the same slot as in question 5. What does that
   reduce to, given your answer to question 7?
9. For update and remove, the caller must name an element. Elements move on
   every operation. What piece of code is the only one that knows when an
   element moves? So who has to record where it went?
10. Build from n elements: n pushes cost O(n log n). Could one of your two
    repairs turn an arbitrary array into a heap in place? Which one, what
    does it assume about the part of the tree it works on, and what order
    of visiting slots satisfies that assumption? Then count the work.

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Element storage**

- Store elements by value (`item_size` plus `memcpy`, like `array_t`), or
  store `void *`? What does each mean for the cost of a move, for the cost
  of a comparison, and for who owns the memory?
- Build on RCL's `array_t` (`src/rcl/array.h`), or own the buffer? Note
  that `array_push` grows with `realloc` and can move everything.
- A `free_func` like the other RCL containers? On destroy only, or also on
  pop, when the element is usually handed to the caller?

**The comparator**

- Reuse `array_compare_func` (`int(const void *, const void *)`), or take
  an extra `void *user_data` argument so a comparator can consult external
  state (A* f-scores kept in a separate array)? What does each mean for a
  heap of plain node ids?
- Do you document "min-heap, flip the comparator for max", or offer both?

**Pop and peek semantics**

- Pop on an empty heap: assert, return `false`, return `NULL`?
- Does pop copy out into a caller buffer, return a pointer that is valid
  until the next call, or both?
- Does peek return `const`? (Section 4.6.)
- A "replace top" or "push then pop" combined operation? What could it save
  compared with doing the two separately?

**Update and remove (the "ideally" in the ticket)**

- Offer them at all in v1, or ship push/pop/peek and let A* use duplicate
  entries (4.5)? Is a timer cancel then expressible some other way, for
  example a "cancelled" flag checked on pop?
- If you offer them, what is a handle? An index into the heap (which
  changes), an index into a stable side table, a pointer to a caller-owned
  slot that the heap writes into, a callback the heap calls whenever it
  moves an element? Each has a different memory cost and a different
  failure mode.
- Is a handle still valid after its element is popped? After its slot is
  reused by a later push? How would a debug build detect a stale handle?
  (Compare with Z's unversioned `entity_t`.)
- Does update take the new value, or does the caller mutate in place and
  then say "I changed it"? Should update know whether the key went up or
  down, or work it out?

**Growth and bounds**

- Growable, or fixed capacity chosen at creation (no allocation during
  gameplay)? If growable, what happens to pointers from peek?
- Should `heap_new` accept an existing array and build in O(n)?

**Extras**

- Unordered iteration, for debugging or "cancel every timer that targets
  entity 7"? A debug-only invariant checker exposed to tests?

**Error model**

- RCL so far is assert-and-return-pointers. Does the heap follow that, or
  return status codes?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, layout.** Draw a complete tree of 7 or 15 slots, number them in
> reading order, and write each child's number next to its parent's. Look
> for a pattern with doubling in it. Then try again with the numbering
> starting at the other value.

> **Hint B, push.** The shape tells you where the new element goes. After
> that, draw every element the newcomer could conceivably need to be
> compared with. The drawing is smaller than you might expect.

> **Hint C, pop.** Promoting a child into the hole and repeating downwards
> is tempting. Check what it does to the shape, then reread question 5.

> **Hint D, the edge cases.** Test your reasoning on a heap of size 1, size
> 2, and on a node that has exactly one child. The last one is where most
> off-by-one bugs live.

> **Hint E, update.** Compare the element with its neighbours after the
> change. Only one of the two directions can be wrong.

> **Hint F, handles.** Every movement of an element inside the heap goes
> through a small number of places in your code. If those places also
> told someone the element's new position, finding it later would be O(1).

> **Hint G, build.** About half of the slots in a complete tree are leaves.
> What is true of a single element on its own?

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/heap_test.c` using Unity (`setUp`/`tearDown`, `static void
name(void)` cases, `RUN_TEST` in `main`, as in `src/array_test.c`),
registered in `meson.build` under `if get_option('tests')` like the others.

**Basics**

- A new heap is empty; peek and pop on empty behave as documented.
- One element: peek returns it, pop returns it, the heap is empty again.
- Ascending, descending, and all-equal input all drain sorted.

**Heap order against a sorted reference** (the ticket's "done when")

- Push N random ints (fixed seed), also into an `array_t`, and sort that
  with `array_sort` (`src/rcl/array.h:199`, a `qsort` wrapper). Popping
  all N must match the sorted reference element by element.
- Same with many duplicates (values from 0..9). Equal ints are
  indistinguishable, so the match must still be exact.
- Interleaved random pushes and pops against a sorted reference: after
  every operation, peek equals the reference minimum and counts agree.
- A test-only invariant checker that verifies invariant 1 for every
  parent/child pair, run after every step of the randomized test.

**Comparator and growth**

- A max-heap via a reversed comparator pops in descending order.
- Composite keys (tick, seq): equal ticks come out in `seq` order.
- Extreme values: `INT_MIN`, `INT_MAX`, 0, -1 together. (Catches a
  subtraction comparator in your own test helpers.)
- Push well past the initial capacity. Everything still pops in order.

**Update and remove** (if you offer them)

- Decrease a deep element's key below the minimum: peek returns it.
  Increase the minimum's key: peek no longer returns it, and a full drain
  is still sorted.
- Remove the minimum, a leaf, the last element, and a middle element via
  handles. Each time, a full drain matches the reference minus it.
- Randomized push/pop/update/remove against a reference, checking after
  each step that every live handle resolves to the right element. This
  catches "moved the element but not its handle".
- A handle used after its element is gone behaves as documented.

**Build and ownership**

- Build from N random values: passes the invariant checker, drains sorted.
  Optionally count comparisons and check they grow linearly with N.
- With a `free_func`, destroying a non-empty heap frees exactly the
  remaining elements, once each (a counting free function, or ASan).

---

## 10. Further reading

These are named for further reading only. Read them *after* you have made
your own attempt, since each one reveals the core trick.

- **J. W. J. Williams, "Algorithm 232: Heapsort"**, Communications of the
  ACM, 1964. The original binary heap, introduced as the data structure
  behind heapsort.
- **Robert W. Floyd, "Algorithm 245: Treesort 3"**, Communications of the
  ACM, 1964. The linear-time construction.
- **Cormen, Leiserson, Rivest and Stein, *Introduction to Algorithms***,
  the heapsort chapter, including the proof of the O(n) build bound.
- **Robert Sedgewick and Kevin Wayne, *Algorithms* (4th edition)**, the
  section on priority queues, including the *indexed* priority queue, which
  is one answer to the handle question in section 7.
- **Peter Hart, Nils Nilsson and Bertram Raphael, "A Formal Basis for the
  Heuristic Determination of Minimum Cost Paths"**, IEEE Transactions on
  Systems Science and Cybernetics, 1968. The A* paper.
- **Amit Patel's "Red Blob Games"** articles on A* and grid pathfinding.
  Very practical, game-oriented, and they discuss the duplicate-entry
  approach versus decrease-key.
- **George Varghese and Tony Lauck, "Hashed and Hierarchical Timing
  Wheels"**, ACM SOSP 1987. The main alternative to a heap for timers.
- **C++'s `std::priority_queue`** (and `std::push_heap` / `std::pop_heap`).
  A useful reference for an API that deliberately offers *no* update or
  remove.

---

## If you only remember three things

1. **Just enough order.** A heap only promises that every parent beats its
   children, so the minimum is on top. Everything else is unordered, which
   is why push and pop cost only O(log n). The complete shape is what lets
   it live in a flat array with no pointers; work out how.
2. **The comparator is part of the data structure.** It must be a
   consistent ordering and must not change its mind while elements are
   inside. Overflowing subtraction, NaN, and keys mutated behind the heap's
   back all fail silently. Ties come out in unspecified order unless the
   key itself breaks them.
3. **Elements move, so "update or remove" is a contract, not a function.**
   Pointers from peek die on the next push or pop. If callers need to name
   an element later (cancel a timer, decrease an A* cost), someone has to
   track where it went. In Z, that meets the same unversioned-id problem
   that recycled `entity_t` values already have.
