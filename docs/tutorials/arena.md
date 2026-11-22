# Arena Allocators: A Learning Guide

> For RCL-2 (`rcl_arena`) and, later, scratch and scene memory in Z.
> This guide explains the idea and leaves the implementation to you. It has
> no alloc/reset/mark code and no step-by-step algorithms. The C snippets
> show how a *caller* might use an arena, against an API that is only a
> sketch.

## Summary

An **arena** (also called a *region*, *bump allocator*, *linear allocator*
or, in GNU land, an *obstack*) hands out memory by moving a single cursor
forward through a buffer it owns. It supports:

- allocation of any size, with any power-of-two alignment, in **O(1)** and
  usually in a handful of instructions;
- **no individual free**. You give memory back all at once: `reset` makes
  the whole arena reusable, `free` (or `destroy`) returns it to the system;
- optionally, **marks**: remember where the cursor is now, allocate freely,
  then rewind to that point, giving back everything allocated since.

The thing that makes an arena useful is not speed, although it is fast. It
is that it lets you **group allocations by lifetime**. Everything that lives
exactly as long as a frame goes in the frame arena. Everything that lives as
long as a level goes in the level arena. Teardown stops being a tree of
`free` calls and becomes one call per lifetime.

The ticket adds one hard requirement that shapes the whole design: the arena
must be able to **grow without moving anything it has already handed out**.
Most of the interesting design work is in what "grow" means under that
constraint.

---

## 1. The problem

Look at how a typical C program frees a parsed level. Every layer was
`malloc`ed, every layer's name was `strdup`ed, every layer's tile data was
`malloc`ed, and the container holding them was `malloc`ed too. To tear it
down you need a destructor per type, called in the right order, and every
one of them must agree about who owns what. Forget one and you leak; call
one twice and you corrupt the heap.

Now notice something about those allocations: **they all die at the same
moment**. Nobody frees a single layer name in the middle of a level. The
general-purpose allocator is doing a lot of work (tracking each block's
size, coalescing free neighbours, staying thread-safe) to support a
capability, freeing things one at a time in any order, that this code never
uses.

What you actually want here is:

1. allocate many objects of different sizes and alignments, quickly;
2. never think about freeing them individually;
3. release them all in O(1) or close to it, when their shared lifetime ends;
4. keep every pointer you were given valid until then, no matter how much
   more you allocate.

### The usual suspects

**`malloc`/`free` per object.** Fully general. Each call has real cost
(headers, size classes, sometimes locks), same-lifetime objects land far
apart, and teardown means walking your structures to free each piece.

**One big buffer, grown with `realloc`.** Allocation is a bump, teardown is
one `free`. But `realloc` may move the buffer, and then **every pointer you
handed out dangles**. This is what RCL's `array_push` does
(`src/rcl/array.h`): fine for an array of values, fatal for an allocator.

**Fixed-size pool.** O(1) alloc and O(1) individual free, no fragmentation,
but only for one object size. Great for "many bullets", poor for "a level's
worth of assorted stuff".

**Stack memory (`alloca`, VLAs, locals).** The fastest bump allocator there
is, freed for you. But its lifetime is a C call frame, its size is limited
and unchecked, and you cannot return it to your caller. A frame arena is a
stack you control.

**Reference counting or a GC.** Answers "who frees this?" at runtime, with
per-object cost, for a question that grouped lifetimes already answer at
design time.

### Side by side

```text
                    alloc    free one  free all   stable    sizes   waste
                                                  ptrs?
---------------------------------------------------------------------------
malloc/free         O(1)~    O(1)~     O(n)       yes       any     header
single realloc buf  O(1)*    no        O(1)       NO        any     slack
fixed-size pool     O(1)     O(1)      O(1)**     yes       one     slack
stack / alloca      O(1)     no        auto       scope     any     none
refcount / GC       O(1)~    auto      auto       yes       any     header
chunked arena       O(1)     no***     O(blocks)  yes       any     tail

 ~ = depends on the allocator, not a hard bound   * = amortized
 ** = if the pool owns all slots in one buffer     *** = except via marks
 "tail" = the unused end of each block, plus alignment padding
```

None of these is "bad". `malloc` is the right answer when lifetimes are
genuinely individual: a texture cache entry that may be evicted at any
time, a long-lived object with its own destructor. A pool is the right
answer for many same-sized objects with independent lifetimes. The arena
wins when **many allocations share one lifetime**, and that lifetime has a
clear start and end.

---

## 2. The mental model

### A notepad, not a filing cabinet

`malloc` is a filing cabinet: every folder has a label, you can pull any
one out and put it back, and a clerk keeps track of the gaps. An arena is a
**notepad**. You write on the next free line. You never erase one line; when
the notes are no longer needed, you tear off the used pages (reset) or bin
the pad (free).

Two refinements turn the notepad into the structure in the ticket:

- **More pads.** When the current pad is full you do not copy your notes
  onto a bigger pad (that would be `realloc`, and everyone holding a page
  reference would be pointing at the old pad). You start writing on a
  **new** pad, and the old one stays exactly where it is.
- **Sticky notes as bookmarks.** A *mark* is a sticky note on the current
  line. Later you can tear everything back to the sticky note and continue
  from there. Think about what the sticky note has to record once there may
  be more than one pad.

### The state, drawn

An arena that has grown into three blocks. `#` is memory handed out, `.` is
alignment padding, a space is unused.

```text
  block A (oldest)          block B                   block C (current)
  +--------------------+    +--------------------+    +------------------+
  |####..###.######### |    |######..############|    |#####..##         |
  +--------------------+    +--------------------+    +------------------+
                    ^                                           ^
                    unused tail, never reused              cursor: the next
                    (in some designs)                      allocation starts
                                                           here (after any
                                                           padding it needs)

  Pointers into A and B stay valid while C fills up.
  How the arena keeps track of A, B and C is yours to decide.
```

Read it from the caller's side:

- Every `#` region is some earlier allocation. None of them will ever move.
- The `.` regions exist because some allocation asked for an alignment the
  cursor did not already satisfy. Those bytes are wasted on purpose.
- Block A has an unused tail. An allocation did not fit there, so the arena
  moved on. Whether that tail can ever be used again is a design decision.
- A reset makes all three blocks reusable. A free returns all three to the
  system.

### The invariants

These must hold after every public operation. They are *what* must be true.
*How* you keep them true is your job.

1. **Disjointness.** No two live allocations overlap, and every live
   allocation lies entirely inside one block the arena owns. No allocation
   straddles two blocks.
2. **Stability.** An allocation's address never changes for as long as it
   is live. Growth must not move, copy or free memory that is live.
3. **Alignment.** Every pointer returned by an aligned allocation is a
   multiple of the requested alignment. Every pointer returned by the plain
   allocation is aligned for any object type the caller could reasonably
   store (you will have to define "reasonably" for C99).
4. **Size.** Every allocation provides at least the number of bytes asked
   for, all of them writable, none of them shared with another allocation.
5. **Liveness.** After a reset, no earlier allocation is live. After a
   restore to a mark, every allocation made before the mark is still live
   and every allocation made after it is not.
6. **Ownership.** Every block the arena has obtained from the system is
   either still owned by the arena or has been returned. After `free`, the
   arena owns nothing.

Invariant 5 is the subtle one. It talks about the **caller's** contract: a
pointer that is "not live" is, from the caller's side, exactly as dead as a
freed `malloc` pointer, even though the bytes are still mapped and reading
them may appear to work. Ask yourself how a user could find out they broke
this rule, and what your arena could do to help them find out sooner.

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation            time            notes
---------------------------------------------------------------------
alloc(n)             O(1)            amortized if it has to add a block
alloc_aligned(n, a)  O(1)            same, plus a little padding
reset                O(1) or O(b)    b = blocks; depends on what you keep
free / destroy       O(b)            one system free per block
mark                 O(1)
restore(mark)        O(1) or O(b)    depends on how many blocks it spans
used / capacity      O(1) or O(b)    if you track totals, O(1)
```

Note what is missing: there is no row for "free one object". Its absence
is the feature.

### Memory

- **Per allocation**: zero bytes of header in the simplest design. Compare
  with `malloc`, which typically keeps at least 8 or 16 bytes of
  bookkeeping per block, and rounds small requests up to a size class.
- **Per block**: whatever you need to keep track of the block itself.
- **Padding**: up to `alignment - 1` bytes per aligned allocation.
- **Tail waste**: when an allocation does not fit in the rest of the
  current block, whatever is left there may be lost. The bigger the block
  relative to the typical allocation, the smaller this fraction. A request
  *bigger* than a block is a case you must decide about (section 7).
- **High-water mark**: a reset arena that keeps its blocks keeps its
  memory. After the busiest frame of the game, the frame arena stays at that
  size. That is usually what you want (no allocation at all in steady
  state), but it is a choice.

### Cache behaviour

- Allocations made one after another are **adjacent in memory**. A level's
  layers, names and tile data loaded in sequence end up next to each other,
  instead of wherever `malloc` found a gap. Walking them later touches
  fewer cache lines and pages.
- A reset frame arena **reuses the same addresses every frame**. Those
  lines and pages are hot in cache and TLB, which `malloc` cannot promise.
- The flip side: an arena never compacts. Long-lived arenas with a lot of
  dead data inside (data you no longer use but cannot free individually)
  waste memory and spread live data out. That is a sign the lifetime was
  drawn wrong, not a reason to add `free_one`.

### What an arena does *not* do

It does **not run destructors**: memory comes back, GPU textures and file
handles do not (keep that in mind for section 5). It does not zero memory,
detect use after reset, or lock, unless you decide it should.

### A trace of observable behaviour

Here is one possible design, with 64-byte blocks, observed purely through
the public API (addresses written as `block:offset`; yours may differ):

```text
call                               result     used  blocks
-----------------------------------------------------------
new(.block_size = 64)                          0     0 or 1 (your call)
alloc(10)                          A:0        10    1
alloc_aligned(8, 16)               A:16       24    1   (16 % 16 == 0)
alloc(30)                          A:24       54    1
alloc(24)                          B:0        78    2   (did not fit in A)
m = mark()
alloc(16)                          B:24       94    2
restore(m)                                    78    2
alloc(4)                           B:24       82    2   (same spot again)
reset()                                        0     ?   (your call)
alloc(10)                          A:0        10    ?
```

Look at row 5. The 24 bytes did not fit in the last 10 bytes of A, so they
went to a new block, and everything already in A stayed put. Look at row 8.
After a restore, the next allocation lands where the discarded one was.
Now look at the "used" column. Row 3 counted the 6 padding bytes as used;
row 5 did not count A's 10-byte tail. Are those two choices consistent?
Either answer is defensible, but your `used` function has to pick one and
document it.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, the argument order,
> whether it is `rcl_arena_t` or `arena_t`, what a mark looks like, and what
> errors look like are all yours to decide. It is loosely styled after
> RCL's `array_new(type, .opt = ...)` idiom and its `*_destroy(T **)`
> convention.

### 4.1 Allocate, then free everything

```c
arena_t *a = arena_new(.block_size = 64 * 1024);

int   *nums  = arena_alloc(a, 100 * sizeof *nums);
char  *label = arena_alloc(a, 32);
float *verts = arena_alloc_aligned(a, 256 * sizeof *verts, 16);

assert(((uintptr_t)verts % 16) == 0);
snprintf(label, 32, "%d numbers", 100);

arena_destroy(&a);   /* all three gone at once; a is now NULL */
```

No `free(nums)`, `free(label)`, `free(verts)`. That is the whole point.

### 4.2 Helpers that allocate into a caller's arena

```c
/* Copy a string into the arena. Lives exactly as long as the arena. */
char *arena_strdup(arena_t *a, const char *s);

char *name = arena_strdup(level_arena, json_value_get_string(v));
```

The convention to notice: **the caller chooses the lifetime by choosing the
arena.** The helper does not know or care whether the string lives for a
frame or a level. This is how arenas spread through a codebase: functions
that allocate take an `arena_t *` parameter instead of calling `malloc`.

### 4.3 Per-frame scratch with reset

```c
arena_t *frame = arena_new(.block_size = 256 * 1024);

while (running) {
  arena_reset(frame);                    /* last frame's scratch is gone */

  size_t n = count_visible(scene);
  draw_cmd_t *cmds = arena_alloc(frame, n * sizeof *cmds);
  build_draw_list(scene, cmds, n);
  sort_by_texture(cmds, n);
  submit(cmds, n);
}

arena_destroy(&frame);
```

After the first few frames, a design that keeps its blocks across resets
never calls `malloc` again in this loop.

### 4.4 Scoped scratch with marks

```c
/* A function that needs temporary memory but returns nothing in it. */
void layout_text(arena_t *scratch, const char *text, glyph_t *out) {
  arena_mark_t m = arena_mark(scratch);

  size_t len = strlen(text);
  int *breaks = arena_alloc(scratch, len * sizeof *breaks);
  find_line_breaks(text, breaks);
  place_glyphs(text, breaks, out);

  arena_restore(scratch, m);   /* breaks[] is gone; the caller's data is not */
}
```

Marks nest like stack frames: a function can mark, call another that marks
and restores, then restore its own. Ask yourself what should happen if
they do *not* nest properly (section 7).

### 4.5 Two lifetimes in one function

```c
/* Parse a level: the JSON tree is temporary, the layers are not. */
level_t *level_load(arena_t *level, arena_t *scratch, const char *path) {
  arena_mark_t m = arena_mark(scratch);

  char *text = read_file_into(scratch, path);
  parsed_t *tree = parse_into(scratch, text);

  level_t *lv = arena_alloc(level, sizeof *lv);
  lv->name   = arena_strdup(level, tree_get_string(tree, "name"));
  lv->tiles  = copy_tiles_into(level, tree);

  arena_restore(scratch, m);   /* text and tree gone, lv survives */
  return lv;
}
```

This is the pattern that replaces "parse, `strdup` what you want to keep,
destroy the tree". The copies still happen, but into an arena whose
lifetime matches what they are copied for.

### 4.6 A trap to keep in mind

```c
arena_t *frame = arena_new(.block_size = 4096);

const char *last_hit = NULL;

while (running) {
  arena_reset(frame);
  char *msg = arena_alloc(frame, 64);
  snprintf(msg, 64, "hit %d", enemy_id);
  if (hit) last_hit = msg;           /* a frame pointer escapes the frame */

  if (last_hit) draw_text(last_hit); /* what does this print next frame? */
}
```

It will often print the right thing, because nothing has overwritten those
bytes *yet*. Then one day a different allocation lands there first. This
is use-after-free that no allocator, sanitizer or `free` call will report
unless you build something to catch it. The same trap exists with marks: a
pointer allocated after `arena_mark` and stored somewhere that outlives
`arena_restore`. Decide what your debug builds do about it.

---

## 5. Relation to Z

All references below are to the working tree of `/Users/paulo/Projects/z`
as I read it (branch `feature/per-40`, with local changes in the demo and
in `engine/src/tilemap.c`). Line numbers may drift.

### 5.1 How allocation works today

Every CPU-side heap allocation in the engine and demo goes through plain
`malloc`/`calloc`/`strdup`/`free`, or through RCL containers that do. There
are three groups, and they have different lifetimes.

**Engine context (lifetime: the program).** `zengine_init` mallocs the
private context (`engine/src/zengine.c:35`) and creates the texture cache
with `hashtable_new()` (`zengine.c:45`). `zengine_exit`
(`zengine.c:141-170`) destroys the callback arrays, walks the texture cache
destroying each texture (`zengine.c:153-156`), releases GPU objects, and
finally frees the context (`zengine.c:169`).

**Textures (lifetime: mixed).** `zengine_texture_load` mallocs a small
`ztexture_t` (`zengine.c:315`) that wraps a GPU texture. Freeing it has two
halves: `SDL_ReleaseGPUTexture` and then `free`
(`zengine_texture_free`, `zengine.c:350-354`). There are two owners:

- the tilemap owns its tileset texture, loaded uncached
  (`engine/src/tilemap.c:256`) and destroyed in `ztilemap_destroy`
  (`tilemap.c:288`);
- the context's cache owns cached textures such as the player sprite,
  loaded in `start_level` via `zengine_texture_load_cached`
  (`demo/src/scene.c:77`, `zengine.c:330-348`), and those live until
  `zengine_exit`.

**Scene data (lifetime: the level).** The scene is a `zscene_t` holding a
tilemap and a world (`demo/src/scene.h`). Loading it allocates:

- the world: `malloc` for `zworld_t` (`engine/src/zecs.c:25`) and one
  `calloc(MAX_ENTITIES, size)` per registered component type
  (`zecs.c:30`);
- per tile layer: an `int[width * height]` (`tilemap.c:38`), the layer
  struct (`tilemap.c:43`), and two `strdup`s for its name and type
  (`tilemap.c:50-51`);
- the layer list: an RCL `array_t` with a `free_func`
  (`tilemap.c:222-224`), which grows by `realloc` when pushed past capacity
  (`array_push` in RCL's `src/rcl/array.h`);
- the tilemap struct itself (`tilemap.c:261`), which embeds a
  `tile_properties[MAX_TILE_TYPES]` table (`engine/src/tilemap.h`).

**Load-time scratch (lifetime: one function call).** `ztilemap_new` loads
the whole file with `SDL_LoadFile` (`tilemap.c:158`) and frees it right
after parsing (`tilemap.c:170`, `:173`). The JSON tree built by RCL's
parser is a forest of small `malloc`s (RCL `src/json.c`, for example
`:354`, `:455`, `:484`, `:562`) that is destroyed in one call before
returning (`tilemap.c:272`). The `strdup`s at `tilemap.c:50-51` exist
precisely because the strings inside the tree die at that point. Likewise,
`tile_object_t` (`tilemap.h`) carries `name` and `type` pointers that
borrow from the tree, so they are only valid during the
`object_callback` (`tilemap.c:113`).

**Teardown.** At the end of `main`, three destroy calls run in order
(`demo/src/main.c:94-96`): world, tilemap, engine. `zworld_destroy`
(`zecs.c:37-57`) first destroys every live entity (`zecs.c:43-47`) so
registered component destructors run (`zecs.c:79-84`), then frees each
component array, then the world.
`ztilemap_destroy` (`tilemap.c:282-291`) destroys the layer array (whose
`free_func`, `ztilemap_layer_free` at `tilemap.c:138-145`, does four
`free`s per layer), then the tileset texture, then the struct.

**Per frame.** I found **no CPU heap allocation in the frame loop**
(`main.c:55-91`). The systems only read and write existing arrays. There is
per-frame, per-draw *GPU* allocation: `zengine_draw_texture` creates and
releases a transfer buffer and a vertex buffer for every quad
(`zengine.c:421-454`), and `render_system` calls it once per visible tile
(`demo/src/render.c:42-57`).

### 5.2 What it costs

To be fair first: at this size, none of this is slow. A level loads once,
and the frame loop does not touch `malloc`. The cost today is mostly
**structural**:

- **Teardown is a tree of destructors.** Freeing a level means knowing that
  a layer owns three heap blocks, that the array owns the layers, and that
  the tilemap owns the array and a texture. Each new field that points to
  heap memory needs a matching line in the right destructor.
- **Copies to escape a dying lifetime.** The `strdup`s at
  `tilemap.c:50-51` and the `int` copy loop at `tilemap.c:38-41` exist
  because the JSON tree (scratch) and the layers (scene) have different
  lifetimes but both live on the same general heap.
- **Error paths.** `ztilemap_new`'s `cleanup:` label
  (`tilemap.c:275-279`) must undo exactly what was done so far, on every
  early exit.
- **Scattered placement.** Layers, names and tile arrays sit wherever
  `malloc` put them, interleaved with JSON nodes freed a moment later.

### 5.3 How an arena maps onto it

Think in lifetimes first, allocator second. Z already has three clean ones:

```text
lifetime      begins                    ends                   arena?
------------------------------------------------------------------------
program       zengine_init              zengine_exit           maybe
level/scene   load_world                destroy world+tilemap  yes
load scratch  start of ztilemap_new     its return             yes
frame         top of the while loop     bottom of the loop     later
```

- **Scene arena.** Everything in "scene data" above has the same lifetime:
  the world struct and its component arrays, the tilemap struct, every
  layer, every layer's tiles, name and type. Put them in one arena and
  level teardown becomes one `free` (plus the non-memory resources, see
  below). Most of the per-layer destructor and the `cleanup:` bookkeeping
  would have nothing left to do.
- **Scratch arena.** The file contents and the JSON tree are a textbook
  scratch lifetime: built, read, discarded within one call. A mark at the
  start of `ztilemap_new` and a restore at the end express that directly.
  Note what this requires: RCL's JSON parser would have to *allocate from
  an arena you pass in* instead of calling `malloc`. That is an RCL API
  question (section 7), not a Z one.
- **Copy-out becomes copy-into.** The `strdup`s and the tile copy do not
  disappear, but they become "copy from scratch into the scene arena", as
  in example 4.5.
- **Frame arena.** Nothing in Z needs one today. The first candidates will
  be things like a sorted draw list, a batched vertex array, or the list of
  collision pairs a broadphase produces. The per-draw GPU buffers at
  `zengine.c:421-454` are the same *idea* (memory that lives for one frame)
  but in GPU memory; a CPU arena does not manage them. A common answer
  there is one big per-frame upload buffer filled by bumping an offset,
  which is the arena pattern applied to a GPU buffer.

### 5.4 Where an arena could bite in Z

These are the seams where the current code relies on things an arena would
not do. I am not suggesting changes, only pointing at them.

1. **Resources that are not memory.** `ztexture_t` wraps a GPU texture
   that must be released with `SDL_ReleaseGPUTexture`
   (`zengine.c:352`). An arena free would reclaim the small struct and leak
   the texture. Anything with a release call still needs an explicit
   teardown, and in the right order: the tileset texture must be released
   before the scene arena goes and before `zengine_exit` destroys the GPU
   device (`main.c:94-96`).
2. **Shared ownership across lifetimes.** The player sprite comes from the
   context's texture cache (`scene.c:77`), so the scene *borrows* it, and
   `sprite_t` stores the pointer. If the cached texture were allocated in a
   scene arena, the cache would keep a dangling pointer after the level
   ends. Each allocation must go in the arena of its **longest** owner.
3. **Component destructors.** `zworld_destroy` runs per-component
   destructors before freeing the arrays (`zecs.c:43-47`, `:79-84`),
   registered via `zecs_register_component_full` (`zecs.c:95-107`). An
   arena does not replace that call; it only replaces the `free`s after it.
4. **`calloc` semantics.** Component arrays are zeroed today (`zecs.c:30`),
   and the code relies on `*self = (zworld_t) { }` for the struct
   (`zecs.c:27`). If those move into an arena, someone must still zero
   them. Arena memory reused after a reset is full of last level's bytes.
5. **Containers that `realloc`.** The layer list is an `array_t` that
   doubles with `realloc` (RCL `array_push`). That does not mix with an
   arena: the old buffer cannot be freed back into it, so every doubling
   leaves the previous copy as dead weight until the arena dies. Here it
   does not happen (capacity is preset to the layer count at
   `tilemap.c:223`), but any arena-backed growable container has to think
   about it.
6. **Borrowed strings outliving their arena.** `tile_object_t.name` and
   `.type` already borrow from the JSON tree. If the tree moves into a
   scratch arena, the rule "do not keep these after the callback" becomes
   "do not keep these after the restore". A callback that stashes
   `object->type` in a component would compile, run, and read garbage
   later: trap 4.6 again.

What I could not verify: I did not build or run Z or RCL, so I have no
measurements of allocation counts or load times. I did not read every line
of RCL's `json.c`, only enough to see that it mallocs per value and per
error message; I do not know whether `hashtable_set` copies its keys, which
matters for whether object keys in the tree would also need to live in the
arena. And
`load_world` does not check whether `ztilemap_new` returned `nullptr`
(`scene.c:43`), so I could not tell what partial-failure behaviour Z
intends for a scene arena to reproduce.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. The cheapest possible allocator keeps a single position and moves it
   forward. What is the smallest amount of state that describes "where the
   next allocation goes" for *one* block? Which of those numbers change on
   an allocation and which never do?
2. Alignment: you have a position and a requested alignment `a`, a power of
   two. What is the smallest position at or after the current one that is
   a multiple of `a`? Can you express it without a loop or a division? Why
   does it matter that `a` is a power of two? Should you align the
   *offset* or the *address*, and when do those differ?
3. The ticket forbids moving old allocations. When the current block
   cannot fit a request, what are your options that keep every earlier
   pointer valid? What does the arena need to remember about the old block
   so that `free` can still find it later?
4. What should happen when a single request is larger than your block size?
   Is "give it a block of its own" enough? Where does that block go in
   relation to the others, and does that choice affect the waste in the
   block you were filling?
5. Reset has to make all memory reusable. Do you keep every block, keep
   only one, or keep up to some budget? What does each choice do to the
   first frame after a reset? To a frame that is ten times bigger than
   usual, once?
6. A mark has to be enough to restore the arena to an earlier state. Once
   there is more than one block, is a single number enough? What exactly
   must a mark capture, and what has to happen to blocks that were added
   after the mark was taken?
7. Overflow: `size` and `alignment` come from the caller. Where in your
   arithmetic could a huge `size` wrap around and make a request look like
   it fits? What should the arena do then?
8. Zero-size allocations: what should `alloc(a, 0)` return? Is it allowed
   to return the same pointer twice in a row? Is it allowed to return
   `NULL`?

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Naming and shape**

- The ticket says `rcl_arena`, but RCL's existing types are `array_t`,
  `hashtable_t`, `string_t` with unprefixed functions. Which convention
  wins, and does this change the others?
- Is the arena struct opaque (only a pointer in the header), or public like
  `array_t` so callers can embed one by value or inspect its fields?
- Do you support an arena that lives on the stack or inside another struct,
  with `init`/`deinit` instead of `new`/`destroy`?

**Blocks and growth**

- Fixed block size, or growing block sizes (each new block bigger than the
  last)? What does each do to the number of blocks after a big load, and to
  the waste at the end of each?
- Where does a block's own bookkeeping live: in the same allocation as its
  memory, or separately? What does that do to the alignment of the first
  usable byte?
- Can a new block be created from caller-provided memory (a static buffer,
  a stack array), so an arena works with no `malloc` at all?
- When a request does not fit, do you ever go back and try the tails of
  older blocks, or only ever move forward?

**Alignment**

- RCL is built as C99 (`c_std=c99` in `meson.build`), which has neither
  `max_align_t` nor `_Alignof`. What alignment does plain `alloc` use? A
  fixed constant? `sizeof(long double)`? A union of the "big" types? Z is
  C23 and has `alignof`; does that matter if the header must stay C99?
- Do you validate that the alignment is a power of two (assert), round it
  up, or document it as undefined behaviour?
- Is there a maximum alignment you support? What about alignments larger
  than your block's own alignment from `malloc`?
- Do you offer a typed helper macro taking `(arena, type, count)`, and how
  does it learn the type's alignment in C99?

**Reset, free, marks**

- What does reset keep, and is the policy configurable?
- What is a mark: an opaque struct returned by value, a pointer, an
  integer? Can it be copied?
- What happens on restore to a mark that is newer than the current state
  (already restored past), or from a different arena? Assert, ignore, or
  undefined?
- Do marks have to nest strictly? Can you detect when they do not?
- Does restore return blocks to the system or keep them for reuse?

**Zeroing, failure and overflow**

- Is memory zeroed? Always, never, or a separate `alloc_zeroed`? Z's
  `calloc` habit (5.4, point 4) suggests one of these.
- On `malloc` failure inside growth: return `NULL`, call an
  out-of-memory hook, or abort? RCL so far mostly asserts and returns
  pointers. Is "an allocator that asserts" acceptable in a game engine?
- On size overflow: same question.

**Extras worth deciding about now or later**

- **Resize the last allocation.** If the most recent allocation is at the
  end of the used region, growing it in place is cheap. Do you offer it?
  This is the escape hatch for arena-backed growable arrays (5.4, point 5).
- **Cleanup callbacks.** Should an arena be able to register "call this on
  free/reset" for non-memory resources such as GPU textures (5.4, point 1)?
  Or is that a different abstraction?
- **Allocator interface.** Should RCL containers (`array_t`, the JSON
  parser) accept an allocator, so they can draw from an arena? That is a
  larger change than RCL-2, but the arena's API shapes it.
- **Debug aids.** Fill reset memory with a recognisable byte pattern?
  Poison it for AddressSanitizer (`ASAN_POISON_MEMORY_REGION` in
  `<sanitizer/asan_interface.h>`)? Which statistics (`used`, `capacity`,
  block count, peak) are public, and which are test-only?
- **Thread safety.** One arena per thread, documented, or a lock?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, alignment.** Write a few positions and alignments in binary.
> Look at what the low bits of a multiple of 8 have in common, and what
> "round up" means for those bits.

> **Hint B, offset versus address.** If the first byte of a block is only
> 8-aligned, then "offset 16" is not necessarily "16-aligned". Decide which
> one the invariant is about.

> **Hint C, growth.** You are not allowed to move memory, but nothing stops
> you from owning more than one piece of it. Ask how `free` will find every
> piece later, and whether that needs any memory beyond the pieces.

> **Hint D, oversize requests.** A request can be served by a block that is
> exactly as big as it needs to be. Think about whether it should become
> the "current" block.

> **Hint E, marks.** A position inside a block only means something
> together with *which* block it is in.

> **Hint F, reset.** Ask what is cheaper on the next frame: a block you
> kept, or a block you have to ask the system for again.

> **Hint G, overflow.** In your fit test, some quantities are bounded by
> the block size and one is not. Which operations on the unbounded one can
> wrap?

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/arena_test.c` using Unity (`setUp`/`tearDown`, `static
void name(void)` cases, `RUN_TEST` in `main`, as in `src/array_test.c`),
registered in `meson.build` under `if get_option('tests')` like the
others.

**Basics**

- Allocations are non-NULL and writable over their full size, and two
  allocations never overlap: fill each with a different byte and check
  neither was disturbed.
- Destroy sets the caller's pointer to NULL, like `array_destroy`.

**Alignment**

- For every power of two from 1 up to your maximum, an aligned allocation
  returns a multiple of that alignment, even right after an odd-sized
  allocation (1, 3, 7 bytes) that leaves the cursor misaligned.
- Plain `alloc` results are aligned to whatever you documented as the
  default.
- An invalid alignment (0, 3, 12) behaves as documented.

**Growth and stability**

- Allocate far more than one block's worth in small pieces, writing a
  unique pattern into each. At the end, every earlier allocation still
  holds its pattern. This is the test for "growth does not invalidate
  earlier pointers", and it is the one the ticket asks for.
- A request larger than the block size succeeds, is writable over its full
  size, and does not corrupt anything allocated before or after it.
- A request that does not fit in the current block's tail lands somewhere
  that does not overlap any earlier allocation.

**Reset**

- After reset, `used` is zero (or whatever you defined), and new
  allocations succeed.
- After reset, a design that keeps blocks does not grow: allocate the same
  amount as before the reset and check the block count or capacity is
  unchanged.
- Reset many times in a loop with the same workload; capacity stays flat
  (no leak per cycle).

**Marks**

- Mark, allocate, restore: `used` returns to its value at the mark, and the
  next allocation reuses the same address as the first one after the mark.
- Allocations made before the mark keep their contents after a restore.
- Mark, allocate enough to add blocks, restore: the extra blocks are
  handled as you documented (kept or released), and nothing from before the
  mark is lost.
- Nested marks restore in the right order. Misuse (restoring an outer mark
  then an inner one) behaves as documented.

**Edges and policy** (whatever you decided in section 7)

- `alloc(0)` behaves as documented.
- A size near `SIZE_MAX` fails cleanly instead of returning a pointer into
  too little memory.
- Zeroed allocation (if offered) is all zeros, including after a reset
  that reuses dirty memory.

**Under tools**

- Run the suite under Valgrind or AddressSanitizer: no leaks after
  destroy, no invalid accesses.
- If you poison reset memory in debug builds, a test (perhaps a separate
  executable expected to fail) shows that touching a pointer after reset is
  reported.

**Z-shaped**

- Simulate a frame loop: reset, allocate a varying amount, repeat a
  thousand times with a fixed seed. Peak capacity is bounded by the largest
  frame, not by the sum.

---

## 10. Further reading

These are named for further reading only. Read them *after* you have made
your own attempt, since most of them show a complete implementation.

- **David R. Hanson, "Fast Allocation and Deallocation of Memory Based on
  Object Lifetimes"**, *Software: Practice and Experience*, 1990. The
  classic paper on lifetime-grouped arenas in C.
- **David R. Hanson, *C Interfaces and Implementations***, Addison-Wesley,
  1996. The "Arena" chapter is a compact, carefully reasoned C interface,
  in a style close to RCL's.
- **Ryan Fleury, "Untangling Lifetimes: The Arena Allocator"** (2022). A
  long essay on why arenas change how you structure a C program, with
  scratch arenas and lifetime thinking at the centre.
- **Chris Wellons, "Arena allocator tips and tricks"**, on his blog
  *nullprogram.com* (2023). Practical C details: alignment, overflow
  checks, passing arenas by value, scratch arenas.
- **Ginger Bill, "Memory Allocation Strategies"**, a series on
  *gingerbill.org*. The linear (arena) and stack allocator parts are the
  relevant ones.
- **The GNU C Library manual, "Obstacks"**. A long-lived, real-world
  growable arena with object resizing, worth comparing against your design.
- **Jason Gregory, *Game Engine Architecture***. The memory management
  section covers stack allocators, double-buffered (per-frame) allocators
  and pools from a game engine's point of view.

---

## If you only remember three things

1. **Allocate by lifetime, not by object.** An arena turns "free each thing"
   into "end this lifetime". Z already has clear lifetimes (program, level,
   load-time scratch, frame), and that is what makes it a good fit.
2. **Bumping is the easy part; growth, alignment and marks are the design.**
   The ticket's real constraint is that growth must never move live
   memory. Work out what that forces on blocks, on oversized requests, and
   on what a mark has to remember.
3. **An arena frees memory, nothing else, and it frees it silently.** GPU
   textures and other resources still need their release calls, shared
   objects belong in their longest owner's arena, and a pointer kept past
   a reset or restore is a use-after-free nobody will report for you.
