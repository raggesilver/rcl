# Sparse Sets: A Learning Guide

> For RCL-1 (`rcl_sparse_set`) and, later, component storage in Z's ECS.
> This guide explains the idea and leaves the implementation to you. It has
> no insert/remove/lookup code and no step-by-step algorithms. The C snippets
> show how a *caller* might use a sparse set, against an API that is only a
> sketch.

## Summary

A **sparse set** stores a set of small non-negative integers taken from a
known universe `0 .. N-1`. It supports:

- membership test, add, remove, and clear, all in **O(1)**;
- iteration over the members in **O(number of members)**, not O(N), over a
  **packed, contiguous** array with no holes.

It does this with **two arrays** that point at each other. One is indexed by
the integer itself (the *sparse* side, size N). The other holds the members
packed together (the *dense* side, size = member count). Most of the
interesting design work is in what must be true about how those two arrays
relate, and in how cheaply you can keep it true.

You can attach a value to each member as well, which turns the set into a
*sparse map*: integer key to value, with values stored packed. That is the
form an ECS uses for component storage.

---

## 1. The problem

Say your keys are small integers: entity ids, register numbers in a compiler,
vertex ids in a graph, slot numbers. You know an upper bound N. You want a
collection of some of these integers that can:

1. answer "is `x` in the set?" quickly;
2. add and remove `x` quickly;
3. empty itself quickly, even when it is large;
4. **visit every member quickly**, ideally by walking a plain array in
   memory order.

The fourth point is what makes this hard. Each of the obvious structures does
three of these well and fails at the fourth, or at one of the others.

### The usual suspects

**Bitset (or `bool[N]`).** Membership, add and remove are one bit operation
each. Iteration has to scan all N bits (or N/64 words) however few members
there are. Clear is also O(N): you have to zero the whole thing. With 10
members out of a million possible, both of those mostly touch zeros.

**Array indexed by key, with holes** (`T data[N]` plus an "occupied" flag).
This is the bitset with a payload. Lookup is perfect. Iteration still walks
all N slots, and every slot you skip still gets pulled into cache, payload
included.

**Packed array of members** (an RCL `array_t` of ints). Iteration is ideal:
contiguous, exactly `count` items. But "is `x` here?" needs a linear scan,
and removing `x` needs to find it first. You could keep it sorted, which gives
O(log n) lookup but O(n) insert.

**Hashtable.** O(1) expected membership, add and remove. Iteration goes
through buckets (with tombstones, in an open-addressing table like RCL's),
so its cost depends on capacity, not on count, unless you also keep a
separate packed list. You pay for hashing and probing on keys that are
already small dense integers, the one case where hashing gets you nothing.
(RCL's hashtable is also keyed by strings, which makes it an even worse fit
here.)

### Side by side

```text
                      member?  add    remove  clear   iterate      memory
---------------------------------------------------------------------------
bitset / bool[N]      O(1)     O(1)   O(1)    O(N)    O(N)         N bits
array with holes      O(1)     O(1)   O(1)    O(N)    O(N)         N * T
packed array          O(n)     O(1)*  O(n)    O(1)    O(n) dense   n * T
sorted packed array   O(lg n)  O(n)   O(n)    O(1)    O(n) sorted  n * T
hashtable             O(1)e    O(1)e  O(1)e   O(cap)  O(cap)       cap*(K+T)
sparse set            O(1)     O(1)   O(1)    O(1)    O(n) dense   N*I + n*T

 N = universe size, n = current members, T = payload size,
 I = size of an index, e = expected (not worst case), * = amortized
```

None of these is "bad". A bitset is unbeatable when the universe is small
and you mostly ask membership questions, and it is far smaller than a sparse
set. The sparse set wins when you need **both** O(1) random access by key
**and** fast dense iteration, and you can afford memory proportional to N.

---

## 2. The mental model

### Two arrays, two directions

Think of it as a **two-way index**.

- The **dense** array is the *list of members*, packed from position 0 up to
  `count - 1`. If you just want to know "what is in the set?", you read this
  and nothing else.
- The **sparse** array is a *lookup table indexed by the key*. It has one
  slot for every possible key in `0 .. N-1`. For a key that is a member, its
  slot says **where in the dense array that key lives**.

So the dense array answers "which keys?" (position to key) and the sparse
array answers "where is key `x`?" (key to position). Each one is the inverse
of the other, *restricted to the current members*.

### An analogy

Picture a cloakroom with 1000 numbered hooks (the universe) and a short
clipboard list of guests currently in the building (the dense array). The
clipboard is always written top to bottom with no blank lines. Above each
hook is a little card that says "this coat's owner is on line K of the
clipboard" (the sparse array).

- To greet everyone, read the clipboard. You never walk past 1000 hooks.
- To ask "is guest 417 here?", you want an answer without reading the whole
  clipboard. The card on hook 417 is meant to help. But can you trust it?

Now notice something. The cards on hooks belonging to guests who left, or
who never came, may still say anything at all: stale numbers, scribbles.
Does that matter? Think about this before reading on. It is the most famous
property of this structure.

### The state, drawn

Universe N = 10. Members are {7, 3, 9, 5}. One possible state:

```text
                  index:  0   1   2   3   4   5   6   7   8   9
  sparse (size N):       [?] [?] [?] [1] [?] [3] [?] [0] [?] [2]

                  index:  0   1   2   3
  dense  (count = 4):    [7] [3] [9] [5]

  sparse[key] -> a dense position     dense[position] -> a key

  "?" = whatever happens to be there; nothing about it is guaranteed.
```

Read it in both directions:

- `sparse[3] == 1` and `dense[1] == 3`. Key 3 lives at dense position 1.
- `sparse[9] == 2` and `dense[2] == 9`.
- `sparse[0]` is `?`. Key 0 is not a member, and nothing in its slot can be
  trusted.

With payloads (a sparse *map*), you add a third array that runs parallel to
`dense`, one value per member, packed the same way:

```text
  dense (ids):     [  7  ] [  3  ] [  9  ] [  5  ]
  values (T):      [ v7  ] [ v3  ] [ v9  ] [ v5  ]
```

Iterating the values is now a tight loop over a contiguous `T[]`. That is
the property an ECS cares about.

### The invariants

These must hold after every public operation. They are *what* must be true.
*How* you keep them true is your job.

1. **Packing.** Dense positions `0 .. count-1` hold exactly the current
   members, each once. Positions at or beyond `count` mean nothing.
2. **Round trip.** For every member `x`, its sparse slot holds a position
   `p` with `p < count` and `dense[p] == x`.
3. **No false positives.** For every non-member `y`, there must be *no* way
   to read `y`'s sparse slot and conclude that `y` is a member, **whatever
   value that slot holds**.
4. **Payload alignment** (sparse map only). The value for the member at
   dense position `p` is at value position `p`.
5. **Bounds.** `0 <= count <= capacity of dense`, and every key ever stored
   is `< N`.

Invariant 3 is the subtle one. Invariant 2 is about members; invariant 3 is
the promise you need for everyone else. Ask yourself whether invariants 1
and 2 *together* already imply 3, and under what condition.

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation          time           notes
-----------------------------------------------------------------
has(x)             O(1)           worst case, no hashing, no probing
add(x)             O(1)           amortized O(1) if dense grows
remove(x)          O(1)           may change the order of members
clear()            O(1)           independent of count and N (!)
count()            O(1)
iterate            O(count)       contiguous memory
get(x) (map)       O(1)
create             O(N) or O(1)   depends on how you treat sparse memory
```

Constant-time `clear()` surprises most people. It is worth working out why
it is possible, because once you see it, much of the design follows.

### Memory

- The sparse side costs **N x sizeof(index)** no matter how many members
  there are. With N = 1,000,000 and 32-bit indices, that is 4 MB for a set
  that might hold three integers.
- The dense side costs **count x (sizeof(key) + sizeof(T))**.
- So a sparse set suits a universe that is *bounded and not huge*: entity
  ids, registers, slot handles. It does not suit arbitrary 64-bit keys. (Real
  ECS libraries page the sparse array to soften this. See the design
  questions.)

### Cache behaviour

- **Iteration** is the best case: a linear walk of one or two packed arrays.
  The prefetcher loves it.
- **Random lookup** `has(x)` / `get(x)` touches two places that are usually
  far apart: the sparse slot and then the dense slot. That is two likely
  cache misses on a cold lookup, versus one for an array-with-holes. You
  trade some random-access speed for a big iteration win.

### Order

- Iteration order is **not sorted** and is **not insertion order** either,
  once removals happen.
- **Removal can move a different member.** Callers must not assume that an
  element stays at the same dense position, or the same address, across a
  removal.
- Growth of the dense side (if you allow it) can move *everything*.

Here is a trace of the observable behaviour of one common design (yours may
differ; it is one of the decisions you get to make):

```text
add 7, add 3, add 9, add 5     iterate -> 7 3 9 5
remove 3                       iterate -> 7 5 9
add 3                          iterate -> 7 5 9 3
clear                          iterate -> (nothing),  has(7) -> false
add 9                          iterate -> 9
```

Look at the second line. Only one member changed position. Ask yourself why
that is the smallest change that can restore invariant 1.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, the argument order, how
> the payload type is passed, and what errors look like are all yours to
> decide. It is loosely styled after RCL's `array_new(type, .opt = ...)`
> idiom. Note that a C macro cannot take an optional leading type argument, so
> the sketch uses two constructors: one for a plain set of ids and one that
> also takes the payload type. Whether you want one constructor or two, and
> what the ids-only form is called, is your decision.

### 4.1 A plain set of ids

```c
sparse_set_t *s = sparse_set_new_ids(.max_id = 100);

sparse_set_add(s, 42);
sparse_set_add(s, 7);
sparse_set_add(s, 99);

assert(sparse_set_has(s, 7));
assert(!sparse_set_has(s, 8));
assert(sparse_set_count(s) == 3);

sparse_set_remove(s, 7);
assert(!sparse_set_has(s, 7));
assert(sparse_set_count(s) == 2);

sparse_set_destroy(&s);   /* RCL style: takes T**, nulls it */
```

### 4.2 Iterating the members

```c
/* Order is unspecified. Do not rely on it. */
for (size_t i = 0; i < sparse_set_count(s); i++) {
  size_t id = sparse_set_id_at(s, i);
  printf("member: %zu\n", id);
}
```

With members {42, 99}, this prints two lines, in some order. The loop runs
`count` times, never `max_id` times.

### 4.3 Clearing in one call

```c
/* e.g. a "visited" set reused across many graph searches */
for (int run = 0; run < 1000; run++) {
  sparse_set_clear(s);            /* O(1), even if last run filled it */
  dfs(graph, start[run], s);      /* uses add/has on s */
}
```

This is the classic use from the Briggs & Torczon paper: a scratch set that
gets cleared very often. With a bitset, every clear would be O(N).

### 4.4 A sparse map with a payload

```c
typedef struct { float x, y; } position_t;

sparse_set_t *pos = sparse_set_new(position_t, .max_id = 1024);

sparse_set_insert(pos, 3,  &(position_t){ 10.f, 20.f });
sparse_set_insert(pos, 17, &(position_t){  0.f,  5.f });

position_t *p = sparse_set_get(pos, 17);   /* NULL if absent? your call */
p->y += 1.f;

/* Dense iteration over the payload: a tight loop over position_t[] */
position_t *all = sparse_set_data(pos);
for (size_t i = 0; i < sparse_set_count(pos); i++) {
  all[i].x += 1.f;
}
```

### 4.5 Two sets at once (the ECS query shape)

```c
/* Everything that has both a position and a velocity. */
sparse_set_t *small = pos, *other = vel;
if (sparse_set_count(vel) < sparse_set_count(pos)) {
  small = vel; other = pos;
}

for (size_t i = 0; i < sparse_set_count(small); i++) {
  size_t e = sparse_set_id_at(small, i);
  if (!sparse_set_has(other, e)) continue;

  position_t *p = sparse_set_get(pos, e);
  velocity_t *v = sparse_set_get(vel, e);
  p->x += v->dx * dt;
  p->y += v->dy * dt;
}
```

This is caller code. It uses only the public operations, so it gives nothing
away about the internals. It costs O(min(|pos|, |vel|)) membership checks,
and it does not depend on the universe size.

### 4.6 A trap to keep in mind

```c
position_t *p = sparse_set_get(pos, 3);
sparse_set_remove(pos, 17);        /* or: sparse_set_insert(pos, 50, ...) */
p->x = 0.f;                        /* is p still entity 3's position? */
```

Under the usual designs, the answer is "not necessarily". Decide what your
documentation promises and test it.

---

## 5. Relation to Z

All references below are to the working tree of `/Users/paulo/Projects/z`
as I read it (branch `feature/per-40`, with local changes in the demo). Line
numbers may drift.

### 5.1 How storage works today

**The world.** `zworld_t` (`engine/src/zecs.h:37-48`) holds, all sized by
`MAX_ENTITIES` (1024, `zecs.h:8`):

- `bool alive[MAX_ENTITIES]`
- `component_mask_t masks[MAX_ENTITIES]`, a `uint64_t` bitmask per entity
  (`zecs.h:14`)
- `size_t free_list[MAX_ENTITIES]` plus `free_list_count`, for recycling ids
- `void *component_data[MAX_COMPONENT_TYPES]`, one data array per type
  (`MAX_COMPONENT_TYPES` is 64, `zecs.h:9`)

`entity_t` is a plain `size_t` (`zecs.h:12`) with no generation or version
bits.

**Component arrays.** `zworld_new` (`engine/src/zecs.c:23-35`) loops over all
64 type slots and, for every type with a nonzero registered size, does
`calloc(MAX_ENTITIES, component_data_size[i])` (`zecs.c:30`). The built-in
sizes are listed statically (`zecs.c:16-21`). User types get theirs from
`zecs_register_component_full` (`zecs.c:95-107`). The demo registers
`player_spawner_t` and `player_t` in `demo/src/main.c:40-44`, before
`load_world` calls `zworld_new` (`demo/src/scene.c:42`). That order matters,
because arrays are only allocated at world creation.

**Access.** The `DEFINE_SIMPLE_COMPONENT_API` macro (`zecs.h:66-79`)
generates, per component:

- `zworld_add_X`: sets the mask bit and writes `data[entity] = value`
  (`zecs.h:69-70`). The entity id *is* the array index.
- `zworld_get_X`: returns `&data[entity]` (`zecs.h:72-75`) without checking
  the mask. You get a pointer even if the entity does not have the
  component.
- `zworld_remove_X`: clears the mask bit only (`zecs.h:76-79`). The data
  stays, and the destructor is not called here.

**Entities.** `zworld_create_entity` (`zecs.c:60-75`) reuses an id from the
free list, otherwise takes `entity_count++`, and zeroes the mask.
`zworld_destroy_entity` (`zecs.c:77-93`) calls the registered destructor for
every component whose bit is set, marks the entity dead and pushes the id on
the free list. It deliberately leaves the mask alone (comment at
`zecs.c:88-90`). `entity_count` is therefore a high-water mark, not a live
count. `alive_count` is the live count.

**Systems.** Every system I read has the same shape: loop
`i = 0 .. world->entity_count`, skip if `!alive[i]`, skip if the mask
doesn't match, then call `zworld_get_*`:

- `physics_system`: `demo/src/physics.c:161-182`, loop at `:167-178`,
  needs POSITION | VELOCITY
- `tilemap_collision_system`: `physics.c:88-159`, needs
  POSITION | VELOCITY | COLLIDER
- `collision_system`: `physics.c:64-86`, a nested loop over all entities
  twice (`:72` and `:76`)
- `movement_system`: `demo/src/input.c:35-69`, needs PLAYER
- `render_ecs_entities`: `demo/src/render.c:5-24`, needs
  POSITION | SPRITE
- `start_level`: `demo/src/scene.c:57-94`, looks for PLAYER_SPAWNER

### 5.2 What it costs

**Iteration.** Each system costs O(`entity_count`), however many entities
actually match. `movement_system` visits every entity ever allocated to find
the one player. Each skipped entity still costs a read of `alive[i]` and
`masks[i]`. For the ones that match, `zworld_get_position(world, i)` reads
`data[i]`, so access is in id order but with gaps. With 1024 entities and 3
players, you walk 1024 entries to touch 3. `collision_system` is
O(`entity_count`^2) mask checks before any overlap test, whatever the number
of movers and solids.

To be fair to the current design: at 1024 entities, with everything sitting
in L1 or L2, this is fast enough today. A bitmask-per-entity ECS is a good
first ECS. The costs grow with the *universe* (`MAX_ENTITIES`, or the
high-water mark), not with the *population* of a component.

**Memory.** Each registered component costs `MAX_ENTITIES * sizeof(T)` at
world creation, used or not. Approximately, on a typical 64-bit ABI:

```text
position_t        8 B  x 1024  =   8 KiB
velocity_t        8 B  x 1024  =   8 KiB
sprite_t        ~40 B  x 1024  = ~40 KiB   (pointer + ints + floats + bools)
collider_t       16 B  x 1024  =  16 KiB
player_spawner_t 16 B  x 1024  =  16 KiB
player_t         ~8 B  x 1024  =  ~8 KiB   (exactly 1 entity uses it)
```

Not dramatic in absolute terms, but almost all of it is empty for rare
components. It also caps the world at 1024 entities, because raising
`MAX_ENTITIES` grows every array.

**Stability (a hidden benefit).** Component arrays are allocated once and
never move. Data for entity `e` is always at the same address for the
world's lifetime. Current code can hold a `position_t *` across any number
of adds and removes and it stays valid (it might point at stale data, but
never at *another* entity's data). Keep this in mind: a sparse set removes
this guarantee.

### 5.3 How a sparse set maps onto it

- **Key = entity id.** `entity_t` is already a small integer below
  `MAX_ENTITIES`, so the universe is exactly the id space.
- **One sparse set (map) per component type.** It would take the place of
  one `component_data[i]` array. The dense payload array is a packed
  `position_t[]` holding only entities that have a position, with a parallel
  packed list of *which* entities those are.
- **Single-component systems** iterate the dense side directly.
  `movement_system` would visit the player set's members (one entity), not
  1024 slots. `render_ecs_entities` would touch only sprite owners.
- **Multi-component systems** follow example 4.5: pick the smallest of the
  required sets, iterate it, and check membership in the others. For
  `physics_system` that means iterating the smaller of {position, velocity}
  and checking the other. For `collision_system`, the outer loop would run
  over the movers and the inner over the solids, so cost scales with those
  populations, not with `entity_count`^2. The per-entity `masks[]` and
  `ENTITY_HAS_MASK` (`zecs.h:10`) express the same membership a second time.
  Whether Z keeps both is an architecture question, not a sparse-set
  question.
- **Memory** becomes, per component,
  `MAX_ENTITIES * sizeof(index) + count * (sizeof(entity_t) + sizeof(T))`.
  Be honest with yourself here. For `position_t` (8 B), a 4-byte sparse
  index plus an 8-byte `entity_t` in the dense list can cost *more* than
  today if most entities have a position. The saving is for big or rare
  components (`player_t`, `sprite_t`), and the main win is iteration, not
  bytes.
- **Entity destruction.** Today, destroy runs destructors by mask
  (`zecs.c:79-84`) and recycles the id. With sparse sets, a destroyed entity
  has to stop being a member of every set it was in, otherwise a recycled id
  (`zecs.c:64-65`) would "inherit" the old components. Think about how you
  would find those sets cheaply. Today's mask answers that, which is one
  argument for keeping some per-entity record.

### 5.4 Where swap-reordering could bite in Z

These are the places in today's code that rely on things a sparse set would
no longer guarantee. I am not suggesting changes, only pointing at the
seams.

1. **Pointers held while the same set changes.** `start_level` holds
   `spawner` (`scene.c:61`) while it creates an entity and adds position,
   velocity, collider, player and sprite (`scene.c:63-87`). That is safe,
   because none of those writes go to the *player_spawner* set. If a future
   system holds a `position_t *` and then adds or removes *positions*, the
   pointer can end up on another entity's data, or dangle after growth.
   Today that cannot happen (5.2, "Stability").
2. **Removal during iteration.** If a system iterates the dense side of a
   set and removes the current entity from that same set (an enemy dies,
   a pickup is collected), then under a reordering removal some member moves
   into the slot you just visited. A naive forward loop then skips it.
   Today, `zworld_remove_X` only clears a bit (`zecs.h:78`), so iterating
   while removing is harmless.
3. **Indices kept across frames.** Anything that stores "dense position
   `k`" instead of an entity id, for example a cached index into the sprite
   array, goes stale after one removal. Entity ids stay stable. Dense
   positions do not.
4. **`get` without `has`.** `zworld_get_X` never checks membership today
   (`zecs.h:72-75`). With a sparse set, "get on a non-member" has to be
   either defined (return NULL?) or plainly documented as undefined.
   Physics code like `resolve_collision` (`physics.c:36-62`) calls
   `zworld_get_velocity(scene->world, a)` on the assumption that the caller
   already filtered by mask.
5. **Stale ids.** `entity_t` has no generation (`zecs.h:12`), so a stored
   id can come to mean a different entity after recycling. Sparse sets do
   not cause this and do not fix it. EnTT handles it by packing a version
   into the id. It is worth knowing about, because it interacts with
   invariant 3.

What I could not verify: I did not see any code removing components or
destroying entities during gameplay iteration. Nothing in the demo calls
`zworld_remove_*` or `zworld_destroy_entity` outside `zworld_destroy`. So
points 1-3 are risks for future systems, not bugs in the current ones.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. `has(x)` must be O(1) and must not scan. What is the *only* kind of
   memory access that is O(1) for an arbitrary `x`? So what must you store,
   indexed by what?
2. Iteration must be O(count). So where must the members physically be?
3. You now have two arrays. If you trusted only the sparse side for
   membership, what would you have to do to it on every remove and every
   clear? What would that cost?
4. If you *don't* trust the sparse side on its own, what second fact can
   confirm or reject what it says, in O(1)? (Look at the round-trip
   invariant.)
5. Removing an element leaves a hole in the dense side, and invariant 1
   allows no holes. What is the cheapest way to fill a hole in an array when
   order does not matter? Which other piece of state has to agree with the
   result?
6. What single number has to change for the set to become empty? Why is
   that enough, given your answer to question 4?
7. What exactly can go wrong if the sparse slot of a non-member contains a
   huge value? A value that happens to be a valid dense position?
8. For the map form: which arrays must move together when anything moves?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Payload layout**

- Separate parallel payload array, or ids and payload interleaved in one
  array of `{id, value}` records? What does each mean for "iterate only the
  values", for alignment, and for the cost of moving one member?
- Do you offer a pure set (no payload) at all, or always a map where the
  payload size may be zero?

**Genericity**

- Element size at runtime (`void *` plus `item_size`, like RCL's `array_t`),
  or type-generated via macros (like Z's `DEFINE_SIMPLE_COMPONENT_API`)?
  What does `get` return in each case, and how much type safety do you keep?
- Insert by pointer-to-value with `memcpy`, or "emplace": return a pointer
  to an uninitialized slot for the caller to fill?
- A `free_func` like RCL's other containers? When is it called: on remove,
  on clear, on destroy?

**Bounds and growth**

- Fixed `max_id` at creation, or a sparse side that grows when a larger id
  shows up? If it grows, what happens to the "garbage is fine" property for
  the newly added region?
- Does the dense side preallocate to `max_id`, or grow like `array_t`?
- How big should an index be: `size_t`, `uint32_t`, or a type the caller
  picks? This changes the N x sizeof(index) cost directly.
- Paging: real ECS libraries split the sparse side into fixed-size pages
  and allocate a page only when an id in its range appears. Worth it for RCL
  v1, or later?

**Edge semantics**

- `add(x)` when `x` is already a member: no-op, overwrite the payload,
  return an error, or assert?
- `remove(x)` when `x` is absent: no-op, error, or assert?
- `x >= max_id`: assert, return an error, or grow?
- `get(x)` for a non-member: NULL, or a precondition violation?
- Does `remove` report what happened (bool), or return the removed value?

**Order and stability**

- Do you document iteration order as unspecified, or promise something?
- Do you offer an order-preserving remove as a slower alternative? What
  would it cost, and what would it have to update?
- How should a caller remove while iterating? Iterate backwards? A "remove
  later" queue? An iteration API that copes with it? Or just a documented
  rule?
- Do you offer sorting the dense side (for example by id, for deterministic
  output or to help joins)? What else would sorting have to update?

**The uninitialized-memory question** (the famous one)

- Can the sparse side be left uninitialized (`malloc`, not `calloc`)?
  Briggs & Torczon's design, and Russ Cox's well-known write-up of it, rest
  on the answer. Work out for yourself *why* it can be correct, and then
  whether it is correct *in C as the standard and your tools see it*. What
  do Valgrind, MemorySanitizer, or `-fsanitize=memory` think of reading an
  indeterminate value, even if the result is never trusted? Is there any
  difference between `calloc` cost and `malloc` cost for a large allocation
  on Linux and macOS in practice?
- Whatever you choose, does clear stay O(1)?

**Error model**

- RCL style so far is assert-and-return-pointers. Does the sparse set
  follow that, or return status codes?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, membership.** Trust neither array alone. A key is a member when
> the two arrays *agree* about it. Think "round trip", and remember that a
> position is only meaningful when it is below `count`.

> **Hint B, garbage.** If membership needs agreement *and* a bounds check,
> ask whether any garbage value in a non-member's sparse slot can pass both.
> If none can, you never needed to clean that slot.

> **Hint C, clear.** Invariant 1 defines membership in terms of one number.

> **Hint D, remove.** You are allowed to change *which* member sits at a
> dense position. You are not allowed to leave a gap. Only one member needs
> to move, and exactly one sparse slot then has to be told about it.

> **Hint E, the edge case in remove.** Check your reasoning for removing
> the element that is *already* the last one in the dense array, and for
> removing from a set of size one.

> **Hint F, payload.** Whatever you do to the ids array, do the same to the
> payload array at the same positions. If you choose interleaved records,
> you get this for free.

> **Hint G, iterate-while-removing.** Think about which direction of
> traversal makes "the element that moves into my current slot" one I have
> already visited.

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/sparse_set_test.c` using Unity (`setUp`/`tearDown`, `static
void name(void)` cases, `RUN_TEST` in `main`, as in `src/array_test.c`),
registered in `meson.build` under `if get_option('tests')` like the others.

**Basics**

- A new set is empty: count 0, iteration yields nothing, `has` is false for
  every id in range.
- Add one id: `has` is true for it and false for its neighbours; count is 1.
- Add several, then iterate: you get exactly those ids, each once, in any
  order (compare as multisets, not sequences).

**Remove**

- Remove a middle member, the first member, the last member, and the only
  member. Check `has` and count after each.
- After any remove, the remaining ids are exactly the expected set.
- With payloads: after a remove that reorders, `get(x)` still returns *x's*
  value for every remaining `x`. This is the test that catches
  ids-moved-but-payload-didn't.
- Remove then re-add the same id: it is a member again, with the new
  payload.

**Clear**

- Clear a full set: count 0, `has` false for all former members.
- Clear, then add a *subset* of the former members: only that subset
  reports true. (This catches stale sparse entries being trusted.)
- Clear many times in a loop. Optionally time it to confirm it does not
  scale with count.

**Edges and policy** (whatever you decided in section 7)

- Duplicate add behaves as documented, and count does not double.
- Removing an absent id behaves as documented and leaves state unchanged.
- Ids 0 and `max_id - 1` both work. Out-of-range behaves as documented.
- `get` on a non-member behaves as documented.

**Invariants under stress**

- A randomized test: run thousands of mixed add/remove/clear operations
  against a trivially correct reference (a `bool[N]`). After each step,
  compare `has` for every id, compare count, and check that iteration
  matches the reference set. Use a fixed seed so failures reproduce.
- An internal invariant checker (test-only) that asserts round-trip and
  packing after every step of the randomized test.

**Uninitialized memory** (if you choose `malloc` for the sparse side)

- Fill the sparse buffer with deliberately hostile garbage before use
  (all 0xFF, all zeros, values that happen to be valid positions), if your
  design lets a test do that, and confirm that membership is still correct.
- Run the suite under Valgrind or a sanitizer and decide whether the
  reports are expected.

**Growth** (if dense or sparse can grow)

- Push past the initial capacity. All earlier members and payloads survive.
- Document and test what happens to previously returned pointers.

**ECS-shaped**

- Two maps over the same id space. A "join" over the smaller one, checking
  membership in the other, yields exactly the intersection.
- Remove-while-iterating using your documented pattern visits every member
  that should be visited, exactly once.

---

## 10. Further reading

These are named for further reading only. Read them *after* you have made
your own attempt, since each one reveals the core trick.

- **Preston Briggs and Linda Torczon, "An Efficient Representation for
  Sparse Sets"**, ACM Letters on Programming Languages and Systems (LOPLAS),
  1993. The original paper, motivated by register allocation in compilers.
- **Russ Cox, "Using Uninitialized Memory for Fun and Profit"**, on his blog
  *research.swtch.com* (2008). A short, very readable explanation of the
  Briggs-Torczon structure and why it may leave memory uninitialized.
- **EnTT** (C++, by Michele Caini, "skypjack"). Its component storage is
  built on sparse sets with paged sparse arrays and versioned entity ids.
  His blog series *"ECS back and forth"* discusses sparse-set-based ECS
  design and how it compares with archetypes.
- **flecs** (C, by Sander Mertens). Mainly an *archetype* (table) ECS, which
  is a useful contrast. It also uses sparse-set-style structures internally,
  for example in its entity index.
- **Bevy ECS** (Rust). Each component chooses a storage type: the default
  `Table` (archetype-style) or `SparseSet`. The docs explain when each is
  the better choice (sparse sets for components added and removed often),
  which is a good lens for later decisions in Z.

---

## If you only remember three things

1. **Two arrays, each the inverse of the other on the members.** Dense says
   *which* keys are present, packed. Sparse says *where* a key sits in
   dense. The invariants that tie them together are what make every
   operation O(1).
2. **You pay O(N) memory to get O(1) everything plus O(n) packed
   iteration.** Clear can be O(1), and the sparse side may not even need
   initializing. Work out why.
3. **Order and addresses are not stable.** Removal (and growth) moves
   members. In Z, that is the main new hazard compared with today's
   fixed `calloc` arrays: pointers held across changes, removal during
   iteration, and dense indices kept across frames.
