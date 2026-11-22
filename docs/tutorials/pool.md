# Pools with Generational Handles: A Learning Guide

> For RCL-3 (`rcl_pool`) and, later, entity and resource handles in Z.
> This guide explains the idea and leaves the implementation to you. It has
> no alloc/free/get code and no step-by-step algorithms. The C snippets show
> how a *caller* might use a pool, against an API that is only a sketch.

## Summary

A **pool** is a fixed set of equally sized **slots**. You ask it for a slot,
it gives you one; you give the slot back, and it becomes available for the
next request. Allocation and release are **O(1)**, there is no per-object
`malloc`, and nothing ever moves.

A pool *with generational handles* adds one idea on top. Instead of giving
out a pointer or a bare index, it gives out a **handle**: a small value made
of `{index, generation}`. The index says *which slot*. The generation says
*which occupant of that slot* the handle was issued for. When a slot is
freed and later reused, the new occupant has a different generation, so a
handle that still refers to the old occupant can be **recognised as stale**
when you try to use it. `get` returns NULL instead of handing you somebody
else's object.

That is the whole promise: references that can outlive their target and
fail *loudly and safely* instead of silently pointing at the wrong thing.
Most of the design work is in deciding what a slot must remember, what a
handle must carry, and what has to be true between the two.

---

## 1. The problem

A game is full of objects that refer to other objects. A projectile knows who
fired it. An enemy knows its current target. A sprite knows its texture. A
UI element knows which entity it is tracking. These objects have different
lifetimes: the target dies while the enemy still remembers it; a level
unloads its textures while a cached sprite still points at one.

You want a way to name an object such that:

1. looking it up is fast, ideally one indexed load;
2. creating and destroying objects is fast and does not fragment memory;
3. a name that outlives its object **does not quietly resolve to a different
   object**, and does not crash;
4. the name is small and plain: copyable, storable in a struct, comparable,
   serialisable, with no ownership semantics attached.

The third point is the hard one. It is the difference between a bug that
shows up as "`get` returned NULL, the enemy lost its target" and a bug that
shows up as "the enemy is now chasing the player's own bullet, sometimes".

### The usual suspects

**Raw pointer to a `malloc`ed object.** Lookup is free; creation and
destruction cost an allocator call, and objects scatter across the heap. A
pointer that outlives its object is use-after-free, and if the allocator
reuses the address, it *looks* valid and names the wrong thing. Nothing
about the pointer can tell you.

**Bare index into a fixed array** (what Z's `entity_t` is today). Lookup is
one indexed load, memory is contiguous, nothing dangles in the crash sense.
But indices are recycled, so an old index silently comes to mean the new
occupant. This is the same failure as the reused pointer, made safe for the
allocator and unsafe for your logic.

**Reference counting.** Nothing dangles, but "the enemy remembers its
target" now *keeps the target alive*, usually the wrong meaning. Every copy
costs a count update, and cycles need breaking. Weak references fix the
meaning at the price of extra machinery (a control block or a registry).

**Never-reused ids plus a hashtable.** Hand out a 64-bit counter that never
repeats and keep `id -> object` in a map. Stale ids are detected for free,
since an id that was removed simply is not found. You pay hashing and
probing on every lookup, iteration goes through buckets and tombstones, and
the ids are large. (RCL's hashtable is also keyed by strings, so it would
need a sibling keyed by integers.)

**Pool with generational handles.** Lookup is an indexed load plus a cheap
check. Alloc and free are O(1). Stale handles are detected. Handles are
small values. The cost is that the *pool* has to be the owner of the
storage, the capacity is bounded (or growth has to be designed), and
iteration has to deal with free slots.

### Side by side

```text
                      lookup   create  destroy  stale ref     ref size
-----------------------------------------------------------------------
raw pointer           O(1)     malloc  free     UB / silent   8 B
bare index (Z today)  O(1)     O(1)    O(1)     silent        8 B (size_t)
refcount (strong)     O(1)     malloc  free     cannot occur  8 B + count
weak ref              O(1)+    malloc  free     detected      8-16 B
counter id + hashmap  O(1)e    O(1)e   O(1)e    detected      8 B
pool + generations    O(1)     O(1)    O(1)     detected      4-8 B
pool + gen + dense    O(1)     O(1)    O(1)     detected      4-8 B
 (a "slot map")

 e = expected, not worst case.  "+" = an extra indirection.
 "silent" = resolves to the wrong object without any signal.
```

None of these is wrong everywhere. Strong refcounting is right when sharing
means "keep this alive". The pool wins when there are **many objects of one
type, a clear owner (the engine), and many long-lived, non-owning
references** to them: entities, textures, sounds. The last row pairs the
pool with a packed array of live objects, so iteration skips nothing; if
that recalls the sparse-set guide, keep the thought for section 7.

---

## 2. The mental model

### Hotel rooms and key cards

Picture a hotel with a fixed number of rooms (the slots). Each room has a
small counter on its door lock (the generation). When a guest checks in,
the front desk gives them a key card printed with the **room number** and
**the counter value at check-in** (the handle).

- To open a door, the lock compares what is printed on the card with its own
  state. Same room, matching state: the door opens.
- When a guest checks out, the hotel changes something about the lock, so
  that the old card no longer opens it. It does not chase the guest to
  collect the card. It does not need to know where the card went.
- Later another guest gets the same room. Their card works. The previous
  guest's card, found in a coat pocket a week later, does not.

Notice what the hotel never does: it never tracks the cards. Any number of
copies can exist anywhere, in any struct, saved to disk, and none of them
needs to be found and cleared when the room is vacated. The detection
happens entirely **at the door**, at the moment of use.

Two things in the analogy are deliberately vague. First, *how the front desk
knows which rooms are empty* without walking the corridor. Second, *what the
lock remembers* that lets it reject an old card and also say "nobody is in
this room right now". Both are yours to design.

### The state, drawn

Capacity 6. Three live objects: A, B and D. Slot 1 and slot 4 have been used
before and freed. Slot 5 has never been used.

```text
  slot index:     0       1       2       3       4       5
                +-------+-------+-------+-------+-------+-------+
  generation:   |   1   |   2   |   1   |   3   |   1   |   ?   |
  state:        | live  | free  | live  | live  | free  | never |
  payload:      |   A   |  ---  |   B   |   D   |  ---  |  ---  |
                +-------+-------+-------+-------+-------+-------+

  free slots available for reuse: {1, 4, 5}   (how this set is kept,
                                                and in what order, is
                                                up to you)

  handles held by the game:
     h_a = {0, 1}   -> A
     h_b = {2, 1}   -> B
     h_d = {3, 3}   -> D
     h_x = {1, 1}   -> stale: slot 1 was freed since this was issued
     h_y = {3, 2}   -> stale: slot 3 has been reused since
```

Read it slowly. `h_y` and `h_d` name the same slot. Only one of them is
current, and nothing about the slot's payload could tell you which; only the
generation can. `h_x` names a slot that is empty right now. Both must come
back as NULL from `get`.

The numbers in this picture (starting at 1, going up by 1) are one possible
convention, used here only so the drawing has values in it.

### The invariants

These must hold after every public operation. They say *what* must be true.
*How* you keep them true is your job.

1. **Partition.** Every slot index below capacity is either live or free,
   never both. (A "never used" slot counts as free for this purpose, unless
   you decide to distinguish it.)
2. **Free set is exact.** The set of slots that `alloc` may hand out next is
   exactly the set of free slots: no live slot in it, no free slot missing
   from it, no slot in it twice.
3. **Resolution.** `get(h)` returns a non-NULL pointer **if and only if** `h`
   was returned by `alloc` on this pool and the slot it names has not been
   freed since. When it returns non-NULL, the pointer is to that slot's
   payload.
4. **Uniqueness over time.** Two handles issued at different times for the
   same slot index are never equal, at least within whatever horizon you
   choose to promise (see wraparound in section 7).
5. **Stability.** While a slot stays live, the address of its payload does
   not change.
6. **Count.** The live count equals the number of slots in the live state.

Invariant 3 is the one the ticket is about. Ask yourself which of the others
it depends on. In particular: if invariant 2 were violated by a single
duplicate entry, how would invariant 3 eventually fail, and how many
operations later?

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation          time            notes
-------------------------------------------------------------------
alloc()            O(1)            no search for a free slot
free(h)            O(1)
get(h)             O(1)            worst case; no hashing
is_valid(h)        O(1)            same work as get
count()            O(1)
iterate live       O(capacity)     walks free slots too (see below)
create(cap)        O(cap) or O(1)  depends on what you initialise
```

The interesting line is `alloc`. A first attempt often scans for a free slot,
which is O(capacity) in the worst case and gets slower as the pool fills.
The ticket says "a free list", and the question of where that list lives
and what it costs is one of the nicer puzzles here.

### Memory

- Payload: `capacity x item_size`, allocated up front. A pool trades memory
  for the absence of allocator calls; it pays for capacity, not population.
- Per slot: whatever the slot must remember besides the payload. At minimum
  a generation. Possibly more, depending on your answers in section 6.
- The free list: anywhere from "a whole extra array" to "no extra memory at
  all". Working out whether the second is possible, and what it forces on
  `item_size`, is part of the exercise.
- Handles: two integers. With 32-bit index and 32-bit generation a handle is
  8 bytes, the same as a pointer. Packed into 32 bits (say 20 + 12) it is
  half that, at the price of a smaller capacity and a faster wraparound.

### Cache behaviour

- `get(h)` touches one slot. If the generation and the payload are in the
  same cache line, a lookup is roughly one miss when cold. If generations
  live in a separate array, it is two loads in two places, though the
  generation array is small and tends to stay hot.
- **Iteration** walks every slot, live or not. In a pool that is mostly
  full, that is nearly ideal: a linear walk. In a pool that is mostly empty
  (a burst of particles that died), you pull many dead slots through the
  cache to find a few live ones. This is exactly the cost the sparse-set
  guide warned about for "array with holes". It is why the slot-map variant
  exists.
- Reuse order (most recently freed first, or oldest first) affects cache
  warmth and how fast one slot's generation advances.

### Stability

- A live object never moves. A pointer from `get` stays valid **until that
  slot is freed** or the pool is destroyed (or reallocates to grow).
- A handle only becomes stale because someone freed its slot, and then it
  says so.
- Iteration order is slot order, not insertion order once slots are reused.

### A trace

Observable behaviour of one plausible design. Generations here start at 1.
Yours may differ in the exact numbers, in which slot is reused first, and in
what a handle prints as; those are your decisions.

```text
a = alloc()                  a = {0,1}   count 1
b = alloc()                  b = {1,1}   count 2
c = alloc()                  c = {2,1}   count 3
free(b)                                  count 2   get(b) -> NULL
d = alloc()                  d = {1,2}   count 3   reuses slot 1
get(b)                       -> NULL     (same slot as d, older gen)
get(d)                       -> d's payload
free(a); free(c)                         count 1
e = alloc()                  e = {?,2}   slot 0 or 2, your choice
iterate                      -> d and e, in slot order
free(d)                                  count 1
free(d)  (again)             -> ?        (decide: no-op, error, assert)
```

Look at the last line. Whatever you decide it does, ask whether it can
possibly corrupt invariant 2. If it can, that is a bug no matter what the
documentation says.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, the handle layout,
> whether `alloc` returns a pointer as well as a handle, and how errors are
> reported are all yours to decide. It is loosely styled after RCL's
> `array_new(type, .opt = ...)` idiom and its `T **` destroy convention.

### 4.1 Allocate, use, free

```c
typedef struct { float x, y, dx, dy; float ttl; } bullet_t;

pool_t *bullets = pool_new(bullet_t, .capacity = 256);

pool_handle_t h = pool_alloc(bullets);
bullet_t *b = pool_get(bullets, h);
*b = (bullet_t){ .x = 10.f, .y = 20.f, .dx = 300.f, .ttl = 2.f };

assert(pool_count(bullets) == 1);

pool_free(bullets, h);
assert(pool_get(bullets, h) == NULL);   /* the whole point */
assert(pool_count(bullets) == 0);

pool_destroy(&bullets);                 /* RCL style: takes T**, nulls it */
```

### 4.2 A non-owning reference that outlives its target

```c
typedef struct {
  pool_handle_t target;     /* not a pointer: may outlive the target */
  float         speed;
} homing_t;

void homing_update(pool_t *enemies, homing_t *m, float dt) {
  enemy_t *t = pool_get(enemies, m->target);
  if (!t) {                 /* target died, maybe long ago */
    m->target = POOL_HANDLE_NULL;
    return;
  }
  steer_towards(m, t->x, t->y, dt);
}
```

Nobody tells the missile its target died, and nobody keeps a list of
"missiles aimed at enemy 7": the hotel does not track key cards. Whether a
`POOL_HANDLE_NULL` exists is a decision for section 7.

### 4.3 Iterating live slots

```c
/* Visit every live bullet. Order is slot order, not creation order. */
pool_iter_t it = pool_iter(bullets);
bullet_t *b;
while ((b = pool_next(&it))) {
  b->x += b->dx * dt;
  b->ttl -= dt;
}
```

The loop runs over at most `capacity` slots, however many are live. Whether
the iterator yields pointers, handles, or both is up to you. Think about
what a caller who wants to *free* during the loop needs from it.

### 4.4 Freeing while iterating

```c
it = pool_iter(bullets);
while ((b = pool_next(&it))) {
  if (b->ttl <= 0.f) {
    pool_free(bullets, pool_iter_handle(&it));   /* allowed? */
  }
}
```

Because a pool never moves live objects, freeing the *current* slot during
iteration is far less dangerous than it was for the sparse set's swap-remove.
But "less dangerous" is not "defined". Decide whether this is legal, and
whether *allocating* during iteration is legal too (could the iterator visit
the new object? should it?), then write it down and test it.

### 4.5 Handles as keys in other structures

```c
/* Components keyed by entity, where entities come from a pool. */
pool_handle_t e = pool_alloc(entities);
sparse_set_insert(positions, pool_handle_index(e), &(position_t){ 0, 0 });

/* Later, with a handle that may be stale: */
if (pool_is_valid(entities, e)) {
  position_t *p = sparse_set_get(positions, pool_handle_index(e));
  /* ... */
}
```

This is the shape most ECS libraries use: the pool owns *identity* (which
entities exist and which generation is current), and per-component storage
is keyed by the *index* part only. Notice the guard. The sparse set knows
nothing about generations, so without the `is_valid` check, a stale handle
would happily read the component of whatever entity now lives at that
index.

### 4.6 The trap

```c
bullet_t *b = pool_get(bullets, h);
spawn_explosion(bullets, b->x, b->y);    /* allocates; maybe frees? */
b->ttl = 0.f;                            /* is b still h's bullet? */
```

Handles protect you. **Pointers obtained from handles do not.** If anything
between the `get` and the last use of `b` can free `h`'s slot (directly, or
by calling code that frees things), `b` now points at a free slot or at a
new occupant, and no generation check will ever run. The rule most engines
settle on is "re-resolve the handle after anything that might free", or
"never hold a resolved pointer across a call you do not control". Decide
what RCL promises, and what Z's style will be.

There is a second trap if you allow growth: if the pool reallocates its
storage to grow, *every* outstanding pointer moves, even for objects that
were never freed. Section 7 asks you to choose.

---

## 5. Relation to Z

All references are to the working tree of `/Users/paulo/Projects/z` as I
read it (branch `feature/per-40`, with uncommitted changes in several demo
files and `engine/src/tilemap.c`). Line numbers may drift.

### 5.1 Entities today

`entity_t` is a plain `size_t` (`engine/src/zecs.h:12`) with no generation
bits. The world (`zecs.h:37-48`) keeps, sized by `MAX_ENTITIES` (1024,
`zecs.h:8`):

- `bool alive[MAX_ENTITIES]`
- `component_mask_t masks[MAX_ENTITIES]`
- `size_t free_list[MAX_ENTITIES]` and `free_list_count`
- `entity_count` (a high-water mark) and `alive_count`

Read that list again with this guide in mind. **Z already has most of a
pool.** It has fixed slots (entity indices), a liveness flag, a free list,
and a live count. What it does not have is a generation, so its ids are the
"bare index" row of the comparison table.

`zworld_create_entity` (`engine/src/zecs.c:60-75`) pops from the free list
if it is non-empty (`zecs.c:64-65`), otherwise takes `entity_count++`
(`zecs.c:67`). The free list is used as a stack, so the most recently
destroyed id is the first one reused. That makes stale-id aliasing as fast
as it can be: destroy entity 7, create a new one, and the new one is 7.

`zworld_destroy_entity` (`zecs.c:77-93`) runs destructors for components in
the mask (`zecs.c:79-84`), sets `alive[entity] = false` (`zecs.c:86`), pushes
the id on the free list (`zecs.c:91`) and decrements `alive_count`
(`zecs.c:92`). It does not check whether the entity is alive first. As far
as I can read it, destroying the same id twice would push it onto the free
list twice and decrement `alive_count` twice, and then two later
`zworld_create_entity` calls would both return that id. That would break
invariant 2 from section 2 ("free set is exact"), silently. I am
describing current behaviour, not proposing a change.

Lookups never check liveness or identity. `ENTITY_HAS_MASK` (`zecs.h:10`)
reads `masks[e]` only. `zworld_get_X` (`zecs.h:72-75`) returns
`&data[entity]` unconditionally. The mask is deliberately not cleared on
destroy (comment at `zecs.c:88-90`), which is fine for today's systems
because they all check `alive[i]` before the mask, but means "has
component" on a dead id can answer yes.

### 5.2 Where entity ids are held today

I looked for places that store an `entity_t` beyond the scope of one
function, since those are the places a stale id could bite.

- Every system iterates `i = 0 .. entity_count` and checks `alive[i]`
  fresh each frame: `movement_system` (`demo/src/input.c:35-69`),
  `collision_system` (`demo/src/physics.c:64-86`), `render_ecs_entities`
  (`demo/src/render.c:5-24`), `start_level` (`demo/src/scene.c:50-95`).
- `entity_overlap` and `resolve_collision` (`physics.c:26-62`) take
  `entity_t` parameters, but only within one call.
- `zscene_t` (`demo/src/scene.h:7-10`), `player_t` and `player_spawner_t`
  (`demo/src/player.h:10-19`) hold no entity ids.
- Nothing in the demo calls `zworld_destroy_entity` or `zworld_remove_*`;
  the only caller of `zworld_destroy_entity` is `zworld_destroy`
  (`zecs.c:44-47`).

So today **no entity id outlives a frame and no entity dies during play**.
The stale-id problem is latent, not live. It appears the moment Z gets its
first "X refers to Y" component (a target, a parent, an owner, a camera
follow) together with anything that destroys entities mid-game.

### 5.3 Textures today

`ztexture_t` (`engine/src/zengine.h:47-51`) is a heap struct wrapping an
`SDL_GPUTexture *`. Everyone refers to textures by raw pointer:

- `zengine_texture_load` (`engine/src/zengine.c:239-328`) `malloc`s a
  `ztexture_t` (`zengine.c:315`) and gives it to the caller, who owns it.
- `zengine_texture_load_cached` (`zengine.c:330-348`) stores that pointer in
  a string-keyed RCL hashtable (`zengine.c:343`) and returns it, documented
  as not owned by the caller (`zengine.h:104-121`). The cache frees every
  entry at exit (`zengine.c:153-156`).
- `zengine_texture_destroy` (`zengine.c:356-362`) frees the texture and
  nulls **the caller's copy** of the pointer. Any other copy (in the cache,
  in a sprite) is left dangling.
- `sprite_t` holds a `ztexture_t *` (`zecs.h:90`). The demo's player sprite
  gets the cached skull texture (`demo/src/scene.c:76-86`).
- The tilemap owns its tileset texture (`engine/src/tilemap.c:255-267`) and
  destroys it in `ztilemap_destroy` (`tilemap.c:288`).
- `sprite_t` also has a `free_texture` flag (`zecs.h:94`). I found no code
  that reads it.

This works because textures die only at shutdown. The dangling case is easy
to picture: a level unload destroys a cached texture while a sprite, or the
cache itself, still holds the pointer, and the next draw reads freed memory.
With textures in a pool and sprites holding handles, the same mistake
becomes "`get` returned NULL, draw a placeholder": a debuggable symptom.

### 5.4 How a pool maps onto it

- **Entities.** The pool's slots are entity indices; the payload might be
  nothing at all (identity only), or the per-entity record (alive state,
  mask). A handle is `{index, generation}`; the index is what component
  storage is keyed by (example 4.5). This is the EnTT and flecs style. It
  also gives you `free`'s exactness for free, if your pool guarantees
  invariant 2 against double frees.
- **Textures, and later sounds, fonts, shaders.** A pool per resource type,
  owned by the engine context. Sprites store handles. The string-keyed cache
  can remain as a *name to handle* map in front of the pool.
- **The ABI question.** `entity_t` is `size_t`, and systems use it as a loop
  counter and array index (`for (size_t i ...)`, then `get_X(world, i)`).
  A handle type is not a loop counter. Whether systems iterate *slots*
  (indices) and only external references use *handles*, or everything uses
  handles, is an architecture choice for Z. It is worth making before the
  first cross-entity reference is written, not after.

### 5.5 What I could not verify

- I did not build or run Z, so the double-destroy behaviour in 5.1 is read
  from the code, not observed.
- I checked the demo and engine sources listed above for stored entity ids
  and texture destruction. I did not check tests, tools, or any code outside
  `engine/src` and `demo/src`.
- Line numbers in `demo/src` and `engine/src/tilemap.c` come from files with
  uncommitted changes and may move when those changes land.
- I do not know Z's plans for level unloading or texture hot-reload, which
  are where texture handles would first matter.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. `alloc` must be O(1) and must not scan. So at the moment of allocation,
   you must already *know* a free slot. What is the minimum you need to
   remember to always know one, after any sequence of allocs and frees?
2. That memory has to live somewhere. List every region of memory the pool
   owns, and for each one ask: is all of it in use at every moment?
3. If your answer to 2 works, what does it require of `item_size`? What
   happens with a pool of `uint8_t`?
4. A handle carries a generation. A slot carries a generation. When exactly
   should the slot's generation change: on alloc, on free, or both? Trace
   section 3's example with each choice. Which choice makes a handle to a
   *currently free* slot fail, without any extra state?
5. Iteration has to skip free slots. Do you need a separate "is live" bit,
   or can liveness be read off state the slot already has?
6. A handle whose index is beyond capacity, from a different pool, or made
   up by the caller: which of these can `get` detect, and which can it not?
   Is "could not detect" acceptable, and where would you document it?
7. A 32-bit generation incremented on every reuse of one slot wraps after
   about four billion reuses. A 12-bit one wraps after 4096. What happens to
   a handle held across a wrap? Which allocation order (section 3, cache
   behaviour) makes one slot's generation climb fastest?
8. Is there a handle value that can *never* be returned by `alloc`? Would it
   be useful as a "null handle" that zero-initialised structs get for free?
9. What does `free` do with a handle that is already stale? With one that
   was never valid? Whatever the answer, can it ever leave the free set with
   a duplicate in it?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Handle layout**

- Two fields in a struct (`{uint32_t index; uint32_t gen;}`) or one packed
  integer with a bit split? What does each mean for comparison with `==`,
  for printing in logs, for hashing, and for storing in a JSON save file?
- If packed, how many bits for the index and how many for the generation?
  Is the split fixed, or a parameter of the pool?
- Is the handle type shared by all pools, or does each pool get its own type
  (via a macro, like Z's `DEFINE_SIMPLE_COMPONENT_API`) so that a texture
  handle cannot be passed where an entity handle is expected?

**The null handle and generation start**

- Do you reserve a value as "null"? Is it all-zero, so `= {0}` gives it?
  What does that imply about the first generation a slot can have?
- Do generations of fresh slots all start at the same value? Is there any
  reason to start them differently?

**Wraparound**

- Let it wrap and accept a tiny chance of a false "valid"? Retire a slot
  permanently once its generation saturates? Assert? Which of these keeps
  invariant 4 honest, and what does each cost over a very long session?

**The free list**

- Separate array, or stored in the free slots themselves? What does each
  cost in memory, and what does each require of `item_size` and alignment?
- LIFO or FIFO reuse? LIFO tends to keep the working set warm in cache.
  FIFO spreads reuse across slots and slows generation growth per slot,
  which matters if your generation is small. Is the order part of your
  documented contract, or an implementation detail?

**Liveness and iteration**

- Separate liveness bit or flag array, or derived from existing state?
- Plain slot-order iteration over capacity, or a packed list of live slots
  (the slot-map variant) at the price of extra bookkeeping on alloc and free?
  You already have a sparse set in RCL by the time you write this. Could the
  pool reuse it, or would that be the wrong layering?
- What is legal during iteration: freeing the current slot, freeing others,
  allocating? What does the iterator yield?

**Capacity and growth**

- Fixed capacity (the ticket says "fixed-size slot pool") or growable? If
  growable, by `realloc` (pointers from `get` move) or by adding chunks
  (pointers stay put, lookup does one more step)?
- What does `alloc` do when the pool is full: return a null handle, assert,
  or grow?

**Payload and ownership**

- Runtime `item_size` with `void *` (like `array_t`), or typed via macros?
- Does `alloc` zero the payload, leave it as whatever was there, or take an
  initial value? Does it return the pointer as well as the handle, to save
  the immediate `get`?
- A `free_func` like RCL's other containers? Called on `free`, on destroy,
  on both? With what argument?

**Error model**

- `get` on a stale handle returns NULL (the ticket says so). What about
  `free` on a stale handle, `get` with an out-of-range index, or a handle
  from another pool? Assert, silently ignore, or return a status?
- Do you offer `is_valid` separately from `get`, or is `get != NULL` enough?

**Debug aids**

- Poison freed payloads in debug builds, so trap 4.6 shows up quickly? Does
  that conflict with your free-list choice?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, finding a free slot.** You never need to *search* for something
> you were told about when it became free. Who knows, at the moment of
> `free`, exactly which slot just became available?

> **Hint B, where the list lives.** Ask whether the free list needs any
> memory of its own, and revisit guiding question 2.

> **Hint C, the empty list.** Whatever you use to mean "no next free slot"
> must be a value that can never be a real slot index.

> **Hint D, staleness.** A handle is stale when the slot has changed in some
> way since the handle was issued. Ask what the *smallest* piece of slot
> state is that is guaranteed to differ, and when it must change for
> "freed" and "reused" to both be caught.

> **Hint E, liveness.** Before adding a flag, list every value the slot
> already holds in the live state and in the free state. Is any of them
> enough on its own to tell the two apart?

> **Hint F, double free.** Think about what a second `free` of the same
> handle would do to the free list, and whether the check you already
> perform in `get` could protect `free` too.

> **Hint G, null handle.** Ask which generation values a slot can hold while
> live, and whether some value is never among them.

---

## 9. Test ideas

Behaviours to check, not test code. They fit the existing RCL pattern: a
`src/pool_test.c` using Unity (`setUp`/`tearDown`, `static void name(void)`
cases, `RUN_TEST` in `main`, as in `src/array_test.c`), registered in
`meson.build` under `if get_option('tests')` like the others.

**Basics**

- A new pool is empty: count 0, iteration yields nothing.
- Alloc several: handles are distinct, all resolve, and each resolves to
  its own payload (write a distinct marker into each, read them all back).

**Stale-handle detection (the ticket's core)**

- Alloc, free, `get` the same handle: NULL.
- Alloc A, free A, alloc B into the same slot (fill the pool to force it if
  reuse order is unspecified). `get(A)` is NULL, `get(B)` is B's payload,
  and the handles compare unequal.
- Reuse one slot many times: every older handle stays stale.
- A null handle (if you have one) and a zero-initialised handle never
  resolve. An out-of-range index behaves as documented.

**Slot reuse**

- Fill to capacity, free one, alloc one: succeeds, with the freed index.
- Fill, free everything, fill again: every alloc succeeds, every
  first-round handle is stale, every second-round handle resolves.
- Alloc on a full pool behaves as documented.
- Live payloads survive other slots being freed and reused (this catches a
  free list that scribbles on the wrong slot).

**Free semantics**

- Double free behaves as documented, and afterwards two allocs return two
  *different* slots. This catches a corrupted free list.
- Free with a stale handle leaves the slot's current occupant alive.

**Iteration**

- Visits exactly the live slots, each once, never a freed one, after any
  mix of allocs and frees (compare as sets).
- Freeing or allocating during iteration behaves as documented.

**Stress and invariants**

- A fixed-seed randomized run of thousands of alloc/free operations against
  a reference model (a list of expected live handles and of freed ones).
  After each step: expected handles resolve, freed ones do not, count and
  iteration match.
- A test-only invariant checker for partition and free-set exactness: no
  live slot in the free set, no slot in it twice, sizes add up.
- If generations are small or configurable, drive one slot to the limit and
  check the documented wraparound behaviour.
- If there is a `free_func`: called once per freed object and once per live
  object on destroy, never for free slots.

---

## 10. Further reading

Named for further reading only. Read them *after* your own attempt, since
most of them show the full design.

- **"Handles are the better pointers"** by Andre Weissflog (floooh), a blog
  post on the design behind the Sokol libraries' resource handles. A clear,
  C-centric argument for index-plus-generation handles over pointers.
- **Allan Deutsch's C++Now 2017 talk on slot maps**, and his related
  proposal **P0661, "slot_map"**, submitted to the C++ standards committee
  through SG14 (the games and low-latency study group). Both describe the
  dense variant and its guarantees.
- **"Game Programming Patterns"** by Robert Nystrom, the *Object Pool*
  chapter. Covers the free-list side, without generations.
- **EnTT** (C++, Michele Caini, "skypjack"). Entity identifiers pack an
  index and a version; his *"ECS back and forth"* series discusses why.
- **flecs** (C, Sander Mertens). Entity ids carry a generation; its entity
  index is a useful real-world reference after you have built yours.
- **Rust's `slotmap` and `generational-arena` crates**, whose documentation
  is a compact statement of the guarantees and trade-offs.

---

## If you only remember three things

1. **A handle names a slot *and* a moment in that slot's life.** The index
   says where; the generation says which occupant. Stale handles are caught
   at the door, at the moment of use, and nobody has to track who holds
   copies.
2. **The free set must be exact.** Every O(1) promise, and the correctness
   of stale detection, rests on the pool knowing precisely which slots are
   free. Double frees are the classic way to break it. Ask where that set
   can live for free.
3. **Handles protect; pointers from handles do not.** In Z, this is the step
   from today's bare `size_t` entity ids and raw `ztexture_t *` to
   references that fail safely, provided resolved pointers are not held
   across anything that might free.
