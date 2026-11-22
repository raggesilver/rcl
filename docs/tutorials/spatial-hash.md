# Spatial Hashing: A Learning Guide

> For RCL-5 (a spatial hash / uniform grid) and, later, the collision
> broadphase in Z's demo. This guide explains the idea and leaves the
> implementation to you. It has no insert/remove/move/query code and no
> step-by-step algorithms. The C snippets show how a *caller* might use a
> spatial hash, against an API that is only a sketch.

## Summary

A **spatial hash** (or **uniform grid**) answers one question fast: *"which
objects are near this region?"* It does so by cutting the plane into square
**cells** of a fixed size and remembering, for each cell, which objects touch
it. A query looks only at the cells the query region covers, so its cost
depends on how crowded that neighbourhood is, not on how many objects exist
in the world.

It supports:

- **insert** an object with an axis-aligned bounding box (AABB);
- **remove** it;
- **move** it to a new AABB;
- **query** by AABB ("everything that might overlap this box") or by point
  ("everything whose box might contain this point").

The difference between a *grid* and a *hash* is only in how cells are
stored. A dense grid is a 2D array covering a known, bounded area. A spatial
hash stores only the cells that are occupied, looked up by their integer
coordinates, so the world can be unbounded and mostly empty for free.

The structure is a **broadphase**: it returns *candidates*, and an exact
test like Z's `aabb_overlap` still decides. The design work is in what "in
a cell" means, objects that straddle cells, and cheap bookkeeping as things
move.

---

## 1. The problem

You have `n` objects with boxes, and every frame you want every overlapping
pair. Testing all pairs costs `n(n-1)/2`: 1,225 tests at 50 objects, about
two million at 2,000, and nearly all of them answer "no".

The observation behind every broadphase is that **overlap is local**. If you
could cheaply ask "who is near me?", total work would scale with the number
of *close* pairs, not all pairs. So you want a structure that answers that
question in time proportional to what is nearby, copes with mixed sizes,
updates cheaply as things move, and **never misses** a real overlap (false
positives are fine; false negatives are a bug).

### The usual suspects

- **Brute force.** No memory, no bookkeeping, cache-friendly. Loses only on
  count, and below a few dozen objects it often *wins*.
- **Dense uniform grid.** A 2D array of cells over a bounded area. Cell
  lookup is arithmetic. Memory scales with *area*, known up front.
- **Spatial hash.** The same cells, but only occupied ones exist, found by
  cell coordinates. Unbounded worlds, memory scales with occupancy, one
  hash lookup per cell visited.
- **Quadtree.** Splits space where it is crowded. Adapts to uneven density
  and mixed sizes, at the cost of pointer-chasing and harder updates.
- **Sweep and prune.** Sort intervals on one axis and sweep. Excellent with
  coherent motion; degrades when objects line up on that axis.
- **Dynamic AABB tree.** Fattened boxes in a tree, as in Box2D. Handles any
  mix well. The most complex to write and tune.

### Side by side

```text
                   build/update    query (local)    memory        sizes
---------------------------------------------------------------------------
brute force        none            O(n)             none          any
dense grid         O(c) per obj    O(cells + k)     O(area)       similar
spatial hash       O(c) per obj e  O(cells + k) e   O(occupied)   similar
quadtree           O(log n)-ish    O(log n + k)     O(n)          mixed ok
sweep and prune    O(n) resort*    O(n + pairs)     O(n)          any
dynamic AABB tree  O(log n)        O(log n + k)     O(n)          any

 n = objects, k = candidates returned, c = cells an object covers,
 cells = cells the query covers, e = expected (hash), * = near-sorted input
```

None of these is "bad". A grid or hash shines when objects are of **similar
size**, roughly the cell size, and spread fairly evenly. That describes a lot
of 2D platformers: tile-sized enemies, pickups, projectiles. It struggles when
one object is huge (it lands in many cells) or when everything piles into one
spot (one cell holds everyone and you are back to brute force inside it).

---

## 2. The mental model

### Mailboxes on a map

Picture a city map with a square grid drawn over it. Each grid square has a
**mailbox**. When someone moves into the city, you drop a card with their
name into the mailbox of every square their house touches. A big house that
straddles a street corner gets a card in four mailboxes.

- To ask "who lives near this park?", you open only the mailboxes of the
  squares the park touches, and read the cards.
- Most of the city has no residents. In a *dense grid*, you build every
  mailbox anyway. In a *spatial hash*, a mailbox exists only once somebody
  needs one, and you find it through a directory keyed by
  "(column, row)".
- When someone moves, their old cards must stop claiming they live there,
  and new ones must appear where they now are.

Two things should bother you. If the park touches three mailboxes and the
big house has a card in all three, its owner appears three times, yet the
ticket wants results **deduplicated**. How will you know you have already
seen someone without a search costlier than the query? And when someone
moves, how do you find their *old* cards?

### The state, drawn

Cell size 16. Four objects (coordinates are world units, `y` grows down, as
in Z):

```text
   A: [ 2, 2] .. [10,10]       small, inside one cell
   B: [12, 4] .. [28,12]       straddles two cells horizontally
   C: [40,20] .. [44,24]       small, alone
   D: [10,10] .. [38,26]       large, spans six cells

        x:  0      16      32      48
   y:  0   +-------+-------+-------+
           | A  B--|--B    |       |
           |    D--|-------|-D     |
      16   +----|--+-------+-|-----+
           |    D--|-------|-D  C  |
           |       |       |       |
      32   +-------+-------+-------+

   occupied cells and who touches them:

     (0,0): A B D     (1,0): B D     (2,0): D
     (0,1): D         (1,1): D       (2,1): D C
```

Read it as a mapping from **cell coordinates** to a **list of objects**. Only
those six cells exist; cell `(5,9)` or `(-3,0)` is simply absent.

Now some observable answers:

- Query point `(5,5)` lands in cell `(0,0)`. Candidates: A, B, D. (B and D
  do not actually contain that point. They are candidates, not hits.)
- Query box `[30,18] .. [42,22]` covers cells `(1,1)` and `(2,1)`.
  Raw cell contents: D, then D, C. **Deduplicated result: {C, D}**.
- Query box `[100,100] .. [110,110]` covers cells with no entry. Result:
  empty, and it should cost about as much as looking up one or four empty
  keys, not as much as scanning everything.

### The invariants

These must hold after every public operation. They say *what* must be true.
*How* you keep them true is your job.

1. **Coverage.** For every live object `o` and every cell `c` that `o`'s
   current AABB touches (by your chosen boundary rule), `o` is listed in
   `c`.
2. **No stale entries.** No cell lists an object for a cell its *current*
   AABB does not touch, and no cell lists an object that has been removed.
3. **No duplicates within a cell.** Each object appears in a given cell at
   most once.
4. **Completeness of queries.** If an object's AABB overlaps the query
   region (by your boundary rule), it is in the result. The result may also
   contain objects that do not overlap.
5. **Uniqueness of results.** Each object appears in a query's result at
   most once, however many of the covered cells list it.
6. **Empty cells are invisible.** Whether or not an empty cell's storage
   still exists internally, it must not change any query's answer.

Invariant 1 is what makes queries correct. Invariant 2 is what keeps them
fast and keeps removed objects from coming back as ghosts. Ask yourself
which of these the `move` operation is most likely to break, and in which
direction (missing entries, or extra ones).

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation         time                     notes
-----------------------------------------------------------------------
insert(o)         O(c) expected            c = cells o's AABB covers
remove(o)         O(c * cell_len) or O(c)  depends on what you store
move(o)           O(c_old + c_new)         O(1)-ish if cells unchanged?
query_aabb(r)     O(cells(r) + raw)        raw = entries before dedup
query_point(p)    O(cell_len)              one cell, one lookup
clear             O(occupied) or O(1)      depends on your layout
```

Two numbers drive everything: **how many cells an object covers** and
**how many objects a cell holds**. Both depend on the cell size you pick
relative to your object sizes.

Cells much smaller than objects mean many lookups and many duplicates per
object. Cells much larger mean many candidates per cell; at one cell, you
have rebuilt brute force. A common start is a cell about one typical object
wide, then measure. Work out why that bounds a typical object's cell count
to a small constant.

### Memory

- A **dense grid** costs `cols * rows * sizeof(cell)` whether or not
  anything is there: 10,000 cells for a 200 x 50 tile level, which is
  small, but an endless level is impossible.
- A **spatial hash** costs about *occupied cells* x (key + bookkeeping)
  plus *total entries* x entry size. Total entries sums the cells each
  object covers, so one enormous object can dominate.
- How you store per-cell lists is the biggest memory and speed decision in
  the design.

### Cache behaviour

- A query does one hash lookup per covered cell, then walks that cell's
  list. A separate heap allocation per list means at least one likely miss
  per cell.
- Candidates are usually **ids**, so the narrowphase must then fetch each
  candidate's box. A copy of the box in the entry avoids that fetch, at the
  cost of bytes and one more thing to update on `move`.
- String keys cost formatting, hashing and comparing per lookup (see 5.4).

### Order and stability

- Query result order is **unspecified**, and order inside a cell may change
  on removal.
- If cells or pools grow, pointers into them move. Do not promise addresses.

Here is a trace of observable behaviour, cell size 16, using the objects
from the drawing. Your design may differ in order but not in content:

```text
insert A, B, C, D           query_aabb([0,0]..[47,31]) -> {A, B, C, D}
query_point(20, 8)          -> {B, D}        (cell (1,0))
move B to [50,2]..[60,8]    query_point(20, 8) -> {D}
                            query_point(52, 4) -> {B}   (cell (3,0))
remove D                    query_aabb([16,16]..[47,31]) -> {C}
remove C                    query_aabb([16,16]..[47,31]) -> {}
```

Look at the `move` line. B left cells `(0,0)` and `(1,0)` and entered
`(3,0)`. Invariants 1 and 2 together say exactly which entries must appear
and disappear, and say nothing about how you find them.

---

## 4. Usage examples

> **The API below is illustrative only. It is a sketch, not a
> prescription.** The names, the argument order, what an "object" is (an id,
> a pointer, a handle you return), how results come back and what errors look
> like are all yours to decide. It is loosely styled after RCL's
> `array_new(type, .opt = ...)` idiom and its `T**` destroy convention.

### 4.1 Insert and query a point

```c
typedef struct { float x, y, w, h; } aabb_t;

spatial_hash_t *sh = spatial_hash_new(.cell_size = 16.f);

spatial_hash_insert(sh, 1, (aabb_t){  2,  2,  8,  8 });
spatial_hash_insert(sh, 2, (aabb_t){ 12,  4, 16,  8 });

array_t *out = array_new(size_t);
spatial_hash_query_point(sh, 5.f, 5.f, out);
/* out holds {1, 2}: both are listed in the cell containing (5,5).
   Object 2 does not contain the point. It is a candidate only. */

spatial_hash_destroy(&sh);
array_destroy(&out);
```

### 4.2 An AABB query across several cells

```c
array_t *out = array_new(size_t);
spatial_hash_query_aabb(sh, (aabb_t){ 0, 0, 48, 32 }, out);

for (size_t i = 0; i < out->length; i++) {
  size_t id = ((size_t *)out->data)[i];
  /* Each id appears once, even if it spans six of the cells. */
}
```

Whether `out` is cleared by the call or appended to is a decision for you
(section 7). Whether the caller passes the array or gets one back is another.

### 4.3 Move every frame

```c
/* After integrating velocity: tell the structure about new boxes. */
for (size_t i = 0; i < mover_count; i++) {
  spatial_hash_move(sh, movers[i].id, movers[i].box);
}
```

Most frames, most movers stay inside the same cells they were already in.
A good `move` should be cheap in that case. Ask what it needs to know to
notice that.

### 4.4 A broadphase pass

```c
/* For each mover, gather candidates near it, then test exactly. */
array_t *cand = array_new(size_t);

for (size_t i = 0; i < mover_count; i++) {
  array_clear(cand);                       /* if RCL had one; sketch */
  spatial_hash_query_aabb(sh, movers[i].box, cand);

  for (size_t k = 0; k < cand->length; k++) {
    size_t other = ((size_t *)cand->data)[k];
    if (other == movers[i].id) continue;   /* you will find yourself */
    if (exact_overlap(movers[i].id, other)) {
      resolve(movers[i].id, other);
    }
  }
}
```

Notice two things. Every mover finds *itself*. And two overlapping movers
are found twice, once from each side. Whether that is fine depends on what
`resolve` does.

### 4.5 A trap to keep in mind

```c
spatial_hash_insert(sh, 7, box7);

box7.x += 40.f;          /* the caller moved the object ...            */
                         /* ... but never called spatial_hash_move      */

spatial_hash_query_point(sh, box7.x + 1, box7.y + 1, out);
/* Is 7 in out? Not necessarily. The structure still files it under
   the old cells. And a query at the OLD spot still returns 7.       */
```

The structure only knows what it was told. Storing *pointers* to the
caller's boxes is worse, not better: edits become visible but the cell
lists are wrong, so queries go silently incomplete. Write the contract in
the header.

A quieter trap: `remove(7)` then `insert(7, ...)` with a recycled id. If any
stale entry for the old 7 survived, the new 7 inherits a ghost.

---

## 5. Relation to Z

All references below are to the working tree of `/Users/paulo/Projects/z`
as I read it (branch `feature/per-40`, with uncommitted local changes in
the demo and in `engine/src/tilemap.c`). Line numbers may drift.

### 5.1 How collision works today

**Entry point.** The main loop calls `physics_system(&scene, FIXED_DT)`
(`demo/src/main.c:81`). `physics_system` (`demo/src/physics.c:161-182`)
integrates gravity and velocity for every entity with POSITION | VELOCITY
(`physics.c:167-178`), then runs `tilemap_collision_system` and
`collision_system`, in that order (`physics.c:180-181`).

**The exact test.** `aabb_overlap` (`physics.c:14-24`) is a strict
inequality test on `x, y, w, h`. Two boxes that only *touch* along an edge
do not overlap. `entity_overlap` (`physics.c:26-34`) feeds it each entity's
position plus collider `w, h`. Note that `collider_t` has `offset_x` and
`offset_y` (`engine/src/zecs.h:96-100`) that the overlap test does not use.
The box is anchored at the position.

**Entity versus entity.** `collision_system` (`physics.c:64-86`) is the
O(n^2) loop the ticket wants to replace. The outer loop (`:72`) runs over
every entity id up to `world->entity_count`, keeping "movers"
(POSITION | VELOCITY | COLLIDER). The inner loop (`:76`) runs over every id
again, keeping "solids" (POSITION | COLLIDER), skipping `j == i` (`:77`).
Each overlap goes to `resolve_collision` (`physics.c:36-62`), which pushes
`a` out along the axis of least penetration and zeroes that velocity
component. Only `a` is moved, and `b` is read but not changed.

Two consequences matter for a broadphase. Every mover is also a solid, so
two overlapping movers are visited as `(i, j)` and `(j, i)`, each pushing
itself out: the "found twice" case of example 4.4, which today's resolution
*relies* on. And positions change mid-loop, so anything built from
positions at the start of the pass is slightly stale by its end.

**Entity versus tiles.** `tilemap_collision_system` (`physics.c:88-159`)
uses no pairs. For each mover it computes the tile columns and rows its box
covers (`physics.c:105-110`), clamps them to the layer (`:112-117`) and
reads `ecs_layer->data[h * width + w]` (`:121-122`), with `tile_id == 0`
meaning empty (`:123`). That is already a **dense uniform grid queried by
AABB**, cell size `tile_size`, over cells that never move.

**The tile grid itself.** `ztilemap_layer_t` (`engine/src/tilemap.h:34-43`)
is `width x height` ints, row-major, filled once from the Tiled JSON
(`engine/src/tilemap.c:38-41`). `tile_size` comes from `tilewidth` only
(`tilemap.c:204`, `:265`), so tiles are assumed square. A `tile_properties`
table with `solid` and `hazard` exists (`tilemap.h:28-32`, `:59`), but I
found nothing that fills or reads it.

### 5.2 What it costs today

- `collision_system` does about `entity_count^2` mask checks every fixed
  step, before any overlap test, whatever the number of movers and solids.
  `entity_count` is a high-water mark (dead ids are skipped but still
  visited), capped at `MAX_ENTITIES` (1024, `zecs.h:8`).
- Today the only entity given a collider is the player, created in
  `start_level` (`demo/src/scene.c:70-73`). Player spawners get a position
  but no collider (`scene.c:16-25`). So with the current demo there is at
  most one solid, and the inner loop never calls `entity_overlap`. The cost
  is all in the loop scaffolding.

To be fair: with one collider, brute force is the right answer. The ticket
is about enemies, projectiles and pickups later, which is why the optional
benchmark matters: it shows *where* the crossover is.

### 5.3 How a spatial hash maps onto it

- **Objects = entity ids.** An `entity_t` is a small `size_t`
  (`zecs.h:12`). It is the natural "object" to store, and it is what
  `resolve_collision` already takes.
- **The box = position + collider size.** That is the box `entity_overlap`
  builds (`physics.c:28-33`). Whether the stored box should include the
  collider offset is a question for Z, not for RCL.
- **Who goes in.** Every entity matching the "solids" mask, since that is
  the set the inner loop searches. Movers then *query* with their own box,
  replacing the inner loop. The outer loop over movers remains.
- **When to update.** Boxes change during integration
  (`physics.c:176-177`) and tile correction. A `move` per mover naturally
  follows those; static solids would be inserted once.
- **Two grids, two jobs.** The tile layer is already a dense, static grid.
  An entity hash is a separate, dynamic one. Matching its cell size to
  `tile_size` or to typical entity sizes are both defensible.
- **Dedup and pair symmetry are different problems.** Dedup (invariant 5)
  stops one *query* returning B twice. It does not stop the pair (A, B)
  being found from both A's and B's queries, which Z's current resolution
  actually relies on (5.1).

### 5.4 The existing hashtable, honestly

The ticket says "built on the existing hashtable where it makes sense".
Here is what that hashtable is, so you can judge "makes sense" yourself:

- Keys are **C strings**. `hashtable_set` copies the key with `strdup`
  (`rcl/src/hashtable.c:172-173`). `hashtable_set_steal` takes ownership
  instead (`hashtable.c:135`). Values are `void *`.
- The hash is FNV-1a over the key bytes (`hashtable.c:20-37`), with linear
  probing (`hashtable.c:49-65`) and tombstones on removal
  (`HASHTABLE_TOMBSTONE_MARKER` in `rcl/hashtable.h`).
- There is no way to set a custom hash function yet. A comment notes that
  one would require rehashing (`hashtable.c:122-123`).
- Default capacity is 769 slots (`hashtable.c:17`), and it grows at about
  70% load (`hashtable.c:142`). Iteration via `hashtable_foreach` walks the
  whole capacity.

So a cell key would have to become a string. That works and is a fine
first version to get correct. Whether it is fast enough, or whether RCL
needs integer keys, is a section 7 decision. I am not recommending one.

### 5.5 Where things could bite in Z

These are seams, not bugs, and I am not proposing changes.

1. **Positions edited behind the structure's back.** Several places write
   `pos->x`/`pos->y` directly: integration (`physics.c:176-177`), tile
   resolution (`physics.c:144`, `:147`) and entity resolution
   (`physics.c:56`, `:59`). That is the trap in example 4.5: any of them
   leaves the hash stale until the next `move`.
2. **Mutation during the pass.** `resolve_collision` moves `a` mid-pass, so
   a push could carry `a` into a cell it was never queried against.
   Whether that matters at one fixed step is for measuring, not assuming.
3. **Recycled ids.** `entity_t` has no generation bits (`zecs.h:12`), and
   ids are recycled. An entity destroyed but not removed from the hash
   would haunt its successor (4.5, second trap).
4. **Negative coordinates.** Tile indices come from a plain `(int)` cast
   (`physics.c:107-110`) followed by a clamp to zero (`:112-113`). A
   spatial hash has no clamp to hide behind (see section 6).

What I could not verify: nothing I found fills or reads `tile_properties`;
no entity other than the player gets a collider, so I could not observe a
real multi-entity collision; I did not run or measure the demo; and nothing
destroys entities during play, so I did not trace that path.

---

## 6. Guiding questions

Work through these in order before writing any code.

1. A point has real coordinates. A cell has integer coordinates. What
   function takes you from one to the other? Now try it on `x = -0.5` with
   cell size 16, using C's integer conversion. Is that the cell you
   expected? Is cell 0 now twice as wide as the others?
2. An AABB has a min corner and a max corner. Which cells does it cover?
   What if its max edge lies *exactly* on a cell boundary: does it touch the
   next cell? Compare with `aabb_overlap`'s strict `<`. Do the two rules
   have to agree for invariant 4 to hold?
3. The hash needs a key per cell. You have two integers. What are your
   options for turning them into one key, and what does each cost when the
   key type is a C string? What property must the encoding have so that two
   different cells never share a key?
4. An object spans three cells, so a query covering all three sees it three
   times. What is the cheapest piece of information that would let you say
   "already seen" in O(1), without clearing anything large between queries?
   Where could that information live: in the query, or in the object? (You
   met a relative of this problem in the sparse-set guide.)
5. To remove an object, you must find every cell entry that names it. What
   would you have to remember at insert time to avoid recomputing or
   searching? What would it cost in memory?
6. A `move` where the object stays in exactly the same cells: what is the
   least work that keeps all invariants? How do you know, cheaply, that the
   cells did not change?
7. A cell becomes empty. Must its storage go away? What happens to memory
   over a long session if it never does, and to speed if it always does
   while an object oscillates across a boundary?
8. Does order inside a cell matter to anyone? If not, what does that allow
   when removing from the middle?
9. An object covering a thousand cells: is there a size beyond which it
   should not be in the grid at all?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Grid or hash**

- Dense grid, sparse hash, or both behind one API? Does RCL want the
  bounded version, given Z's tile layer already is one?
- Fixed cell size, or changeable later (and at what cost)?
- Integer or float coordinates, and what that means for section 6's
  boundary questions?

**Keys and the hashtable**

- Use RCL's string-keyed hashtable as is, add an integer-keyed variant, or
  give the spatial hash its own table? What does each do to RCL's API
  surface and to lookup cost?
- If cell keys are strings, who owns them and when are they freed? Does
  `hashtable_set_steal` change the picture?
- How do you hash two signed integers well? What happens with a poor mix
  when objects line up along one row?

**Per-cell storage**

- A growable array per cell, linked nodes from a shared pool, or a single
  packed array of entries rebuilt each frame? What does each mean for
  allocation count, cache behaviour and removal cost?
- Does an entry store just the id, or the id plus a copy of the box? What
  does the copy buy the narrowphase, and what does it cost on `move`?

**Objects and handles**

- Caller-supplied ids (Z's entity ids) or handles the structure hands out?
  Duplicate insert of the same id?
- A per-object record (where it is, what box it had)? How is it found from
  an id, and what does that imply about id ranges?

**Queries**

- Results into a caller-provided `array_t`, a callback, or an iterator?
  Cleared or appended? May a callback insert or remove?
- Dedup always on, or optional? Candidates only, or an exact filter too?
- Direct pair enumeration? If so, `(a, b)` and `(b, a)`, or one of them?

**Edge semantics and lifecycle**

- `remove` or `move` of an unknown id: no-op, insert, error, or assert?
- Zero-sized boxes, negative widths, NaN: assert, reject, or undefined?
- A `free_func` like the other containers, or no ownership at all?
- `clear` that keeps cells for reuse, or releases them?
- RCL so far asserts and returns pointers. Does this follow suit?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, the cell function.** The problem with `x = -0.5` is the
> direction C rounds. Look up the difference between truncation and the
> other rounding mode in `<math.h>`, and test both on negative inputs.

> **Hint B, the boundary.** Write down, for a box `[0, 16)` with cell size
> 16, how many cells you want it to cover. Then make your cell-range rule
> produce that answer and test it.

> **Hint C, dedup.** You do not need to remember *who* you have seen, only
> *whether*, and only for *this* query. Ask what would make forgetting free.

> **Hint D, removal.** The cells an object covers are a pure function of its
> box. Think about what is cheaper: remembering the box, or remembering the
> answer.

> **Hint E, cheap move.** Two boxes can differ and still cover the same set
> of cells. Compare what the cells *are*, not what the boxes are.

> **Hint F, string keys.** If you go through the existing hashtable first,
> measure before optimizing. A correct slow version is a great reference
> implementation for the randomized test in section 9.

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/spatial_hash_test.c` using Unity (`setUp`/`tearDown`,
`static void name(void)` cases, `RUN_TEST` in `main`, as in
`src/hashtable_test.c` and `src/array_test.c`), registered in `meson.build`
under `if get_option('tests')` like the others.

**Basics (the ticket's "insert")**

- A new structure returns nothing for any point or box query.
- Insert one object inside one cell. A point query inside it returns it. A
  point query in a far cell does not.
- Insert two objects in the same cell. Both come back from a point query
  there.

**Move across cells (the ticket's "move")**

- Move an object from one cell to a distant one. Queries at the old spot no
  longer return it, and queries at the new spot do.
- Move an object within its cell. It is still returned, exactly once.
- Move an object from one cell to straddling two. Both cells now return it,
  and the original single cell still does.
- Move it back and forth across a boundary many times. The total number of
  entries does not grow (if your design lets a test observe that).

**AABB queries spanning cells (the ticket's dedup requirement)**

- A large object spanning a 3 x 2 block of cells, queried with a box that
  covers all six. It appears exactly once.
- A query box that spans several cells and several objects, some of which
  span several cells themselves. The result equals the expected set, with
  no repeats (compare as sets, not sequences).
- A query box entirely in empty space returns nothing.

**Remove**

- Remove an object that spans several cells. No query anywhere returns it.
- Remove, then re-insert the same id somewhere else. It is returned only at
  the new spot (the ghost test).
- Remove the only object in a cell, then query that cell.

**Boundaries and coordinates**

- Negative coordinates, including boxes straddling `x = 0` and `y = 0`.
- Edges and points exactly on a cell boundary follow your documented rule.
- Coordinates far from the origin still work.

**Invariants under stress**

- A randomized test: thousands of mixed insert/move/remove/query operations
  against a brute-force reference (a plain array of boxes). After each query,
  the structure's result must be a *superset* of the reference's true
  overlaps (no misses) and must contain no duplicates. Use a fixed seed so
  failures reproduce.
- A test-only invariant checker that confirms coverage and no-stale-entries
  after every step of the randomized test.

**Benchmark (optional, from the ticket)**

- Build it the way `src/json_bench.c` is built: under
  `if get_option('benchmarks')` in `meson.build`. Note that today `bench/`
  holds only JSON input files, while the benchmark source lives in `src/`,
  so decide where the new one belongs.
- Compare against brute-force pair checks for, say, 50, 500 and 5,000
  objects of similar size, and for a clustered distribution where everything
  piles into a few cells. Report where the lines cross.

---

## 10. Further reading

These are named for further reading only. Read them *after* you have made
your own attempt, since several of them show the core techniques directly.

- **Christer Ericson, *Real-Time Collision Detection*** (Morgan Kaufmann,
  2004). Chapter 7, "Spatial Partitioning", covers uniform grids, hashed
  storage of grid cells, object-to-cell mapping and avoiding repeated tests.
  The standard reference.
- **Robert Nystrom, *Game Programming Patterns*** (2014), the "Spatial
  Partition" chapter. A short, friendly introduction using a grid, with the
  trade-offs against other partitions.
- **Matthias Teschner et al., "Optimized Spatial Hashing for Collision
  Detection of Deformable Objects"** (Vision, Modeling, and Visualization,
  2003). The classic paper on hashing grid cells for collision, including a
  discussion of hash functions for cell coordinates.
- **Box2D** (C, by Erin Catto). Its broadphase is a dynamic AABB tree, a
  useful contrast for when a uniform grid is the wrong choice.
- **Sweep and prune** is described in Ericson's book as well, and is worth
  knowing as the other common answer to the same problem.

---

## If you only remember three things

1. **Cells turn "who is near?" into a few lookups.** Map boxes to integer
   cell coordinates, file each object under every cell it touches, and a
   query only reads the cells it covers. The cell size relative to your
   objects decides whether this beats brute force at all.
2. **It is a broadphase.** It returns candidates, never verdicts. It must
   never miss a real overlap, it must not return the same object twice in
   one query, and the exact test still runs afterwards.
3. **The hard part is bookkeeping, not geometry.** Keeping cells free of
   stale entries through `move` and `remove`, finding an object's old cells
   cheaply, and deduplicating without clearing anything large. In Z, the
   main new hazard is positions changed without telling the structure.
