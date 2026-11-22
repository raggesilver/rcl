# Ring Buffers: A Learning Guide

> For RCL-6 (`rcl_ring`) and, later, input queues, replays and rolling
> history in Z. This guide explains the idea and leaves the implementation to
> you. It has no push/pop/index code and no step-by-step algorithms. The C
> snippets show how a *caller* might use a ring buffer, against an API that is
> only a sketch.

## Summary

A **ring buffer** (also called a circular buffer, and, when both ends are
open, a **deque**: double-ended queue) stores a sequence of elements in one
contiguous block of memory and supports:

- push and pop at the **back** in **O(1)**;
- push and pop at the **front** in **O(1)**, with no shifting of the other
  elements;
- **indexed access** to the i-th element of the sequence, counted from the
  front, in **O(1)**;
- a fixed memory footprint, or amortized O(1) growth if you allow it.

It does this by letting the sequence **start anywhere** in the block and
**continue past the end by coming back around to the beginning**. The
logical order (front to back) and the physical order (slot 0 to slot
capacity-1) are two different things, and the whole structure is about
keeping a cheap, exact relationship between them.

The interesting design work is in three places: how a logical index becomes a
physical slot, how you tell "full" from "empty", and what growing means when
the contents currently wrap around the end.

---

## 1. The problem

Say you have a stream: input events, frame times, recorded ticks. New items
arrive at one end; old ones leave at the other, or fall off once there are
too many. You want to append at the back and remove from the front quickly,
**without moving everything else**, maybe at both ends; read any element by
position; and live in one contiguous allocation, ideally sized once.

Contiguous storage usually means "remove from the front costs a shift", and
cheap front removal usually means "pointers between separate nodes". Having
both is the interesting part.

### The usual suspects

**A plain dynamic array** (RCL's `array_t`). Push at the back is amortized
O(1) (`array_push` doubles with `realloc`). Removing index 0 with
`array_delete` or `array_remove` runs a `memmove` over the rest of the array
(`src/array.c:87-89` and `:106-108`), so a queue built on it costs O(n) per
dequeue. For a queue of 8 events that is invisible. For a replay buffer of
14,000 ticks, popping the oldest every tick, it is not.

**An array with occasional compaction.** Stop shifting on every pop; slide
everything down once when the dead space at the front gets large. Amortized
O(1) and simple, but memory creeps between compactions and push at the
front is still O(n).

**A linked list.** O(1) at both ends, no shifting. Indexing is O(n), every
element is an allocation (or a pool you now write) with pointer overhead,
and iteration chases pointers.

**Two stacks** (push onto one, pop from the other, reverse when it runs
dry). Amortized O(1) at the ends; indexing is awkward and one unlucky pop
costs O(n).

**A block deque** (C++'s `std::deque`): a table of fixed-size chunks. O(1)
at both ends and for indexing (one extra indirection), and growth never
moves elements. It is also much more machinery.

### Side by side

```text
                    push    pop     push    pop     index   memory    moves
                    back    front   front   back                      on grow
------------------------------------------------------------------------------
dynamic array       O(1)*   O(n)    O(n)    O(1)    O(1)    n*T       all
array + compaction  O(1)*   O(1)*   O(n)    O(1)    O(1)    up to 2n  all
linked list         O(1)    O(1)    O(1)    O(1)**  O(n)    n*(T+P)   none
two stacks          O(1)    O(1)*   -       -       awkward n*T       all
block deque         O(1)*   O(1)    O(1)*   O(1)    O(1)    n*T+map   none
ring buffer         O(1)*   O(1)    O(1)*   O(1)    O(1)    cap*T     all

 n = elements, T = element size, P = pointer size, cap = capacity,
 * = amortized if growable (worst case O(1) when fixed and not full),
 ** = doubly linked only
```

None of these is "bad". A linked list wins if elements must never move and
you never index. A block deque wins if you need both ends, indexing and
address stability. The ring buffer wins when you want **contiguous memory, a
predictable footprint, O(1) at both ends and O(1) indexing**, and you can live
with elements moving when it grows. A *fixed-size* ring buffer adds one more
property nothing else in the table has for free: it never allocates after
creation.

---

## 2. The mental model

### A clock face, not a ruler

Think of the storage as a **circle of slots**, like the numbers on a clock.
Slot capacity-1 is followed by slot 0, the way 12 is followed by 1. The
sequence you store is an *arc* of that circle: it starts at some slot (the
front), runs clockwise, and ends at some slot (the back). Nothing requires
the arc to start at slot 0, and nothing stops it from crossing the "12".

Pushing at the back extends the arc clockwise by one. Popping at the front
shortens it from the other end. Push at the front grows it counter-clockwise.
The arc slides around the circle over time. The elements inside it never
move relative to each other.

### An analogy

Picture a sushi conveyor with 8 spaces in a loop. The chef puts each new
plate on the next free space after the last plate; diners take the oldest
first. Nobody slides the remaining plates forward to "close the gap": the
gap is free space the chef will reach again, coming around the loop.

- To find "the third plate from the oldest", you count from the oldest,
  and counting may carry you past the point where the belt loops back. Can
  you get the answer with arithmetic alone, with no loop?
- When every space has a plate, and when no space has one, the chef's view
  of "where the oldest plate is" and "where the next plate goes" may look
  the same. How would the chef tell those apart? Think about this before
  reading on. It is the most famous question about this structure.

### The state, drawn

Capacity 8. Logical contents, front to back: `A B C D E` (5 elements). One
possible physical state, after some earlier pushes and pops moved the arc:

```text
  physical slot:   0     1     2     3     4     5     6     7
                [  D ][  E ][  . ][  . ][  . ][  A ][  B ][  C ]
                         ^                     ^
                         |                     |
                       back                  front

  logical index:   3     4                       0     1     2

  "." = a slot that is not part of the sequence; its bytes mean nothing.
```

Read it both ways:

- Logical index 0 (the front) is `A`, in physical slot 5.
- Logical index 3 is `D`, in physical slot 0. The sequence crossed the end
  of the block between `C` and `D`.
- The live elements occupy two physical runs: slots 5-7 and slots 0-1. A
  ring that has wrapped is **contiguous on the circle but not in memory**.

After `pop_front` (returns `A`) and `push_back(F)`:

```text
  physical slot:   0     1     2     3     4     5     6     7
                [  D ][  E ][  F ][  . ][  . ][  . ][  B ][  C ]

  logical:  B C D E F      (B is now logical index 0, in slot 6)
```

Notice what did *not* happen: `B`, `C`, `D` and `E` did not move a byte.

### The invariants

These must hold after every public operation. They are *what* must be true.
*How* you keep them true is your job.

1. **Count.** `0 <= count <= capacity`. The structure must be able to
   represent *every* count in that range, including both ends of it.
2. **Contiguous arc.** The live elements occupy exactly `count` slots that
   are consecutive *on the circle*, starting at the front slot. No holes.
3. **Order.** Logical index `i` (for `0 <= i < count`) names the element `i`
   steps clockwise from the front. Pushing or popping at one end never
   changes the relative order of the elements that remain.
4. **Unambiguous state.** From the stored fields alone, "empty" and "full"
   must be distinguishable. So must every other count.
5. **Dead slots are dead.** Slots outside the arc carry no meaning. Reading
   one is never part of a correct answer.
6. **Growth preserves the sequence** (growable only). After the capacity
   changes, logical index `i` names the same element it named before, for
   every `i < count`.

Invariant 4 is the one people get wrong, and the one with the most
well-known answers. Invariant 6 is the one that `realloc` alone does not
give you. Ask yourself, looking at the first drawing, what the contents
would look like logically if you just made the block longer at the end.

---

## 3. Guarantees and trade-offs

### Complexity

```text
operation              time          notes
------------------------------------------------------------------
push_back              O(1)          amortized O(1) if growable
push_front             O(1)          amortized O(1) if growable
pop_front / pop_back   O(1)          no shifting, ever
peek front / back      O(1)
at(i)                  O(1)          a little arithmetic, no loop
count / capacity       O(1)
clear                  O(1)          unless a free_func must run
grow                   O(n)          once in a while; see invariant 6
iterate                O(count)      may be two contiguous runs
```

The cost of `at(i)` depends on choices you make. Some capacities make the
arithmetic cheaper than others. Work out which, and what you give up to get
them.

### Memory

- A fixed ring costs **capacity x sizeof(T)** plus a few words, forever,
  with nothing allocated after creation. For a game loop that is the main
  attraction: no `malloc` per frame and a footprint you can read off the
  type.
- A growable ring's capacity tracks the high-water mark of `count`, like
  `array_t`.
- Whatever you choose for telling full from empty (section 6, question 5)
  has a price: a little memory, a little arithmetic, or a constraint on
  capacity. It is a trade, and it is yours to make (section 7).

### Cache behaviour

- Iteration in logical order walks **at most two** contiguous runs, which
  the prefetcher handles almost as well as a plain array.
- Push and pop touch the slot next to the last one touched, so hot queues
  stay in L1. `at(i)` is one computed address and one load.
- The cost a plain array does not have: once the arc wraps, you cannot hand
  the caller "all elements as one pointer and a length".

### Order and stability

- Order is **always** the order the caller built: FIFO if you push back and
  pop front, LIFO at either end, or a mix. The ring never reorders on its
  own. (Compare the sparse set, where removal moves an unrelated member.)
- **Physical position of an element is stable** while the ring is not
  growing and the element is not removed or overwritten. Its **logical
  index is not**: every `pop_front` shifts every logical index down by one.
- **Growth can move everything**, just like `array_t`.
- In an **overwrite-when-full** ring, `push_back` on a full ring silently
  destroys the front element. Any pointer into that slot now points at the
  new element.

Here is a trace of the observable behaviour of one common design, a fixed
capacity-4 ring that overwrites the oldest element when full (yours may
reject instead; it is one of the decisions you get to make):

```text
push_back 1, 2, 3        contents -> 1 2 3        count 3
push_back 4              contents -> 1 2 3 4      count 4 (full)
push_back 5              contents -> 2 3 4 5      count 4 (1 was dropped)
at(0) -> 2,  at(3) -> 5
pop_front -> 2           contents -> 3 4 5        count 3
push_front 9             contents -> 9 3 4 5      count 4
pop_back -> 5            contents -> 9 3 4
clear                    contents -> (nothing)    count 0
```

Look at line three. The element that left was not the one most recently
touched; it was the oldest. Ask yourself which end a "drop" policy should
drop from when the push was at the *front*.

---

## 4. Usage examples

> **The API below is illustrative only.** The names, the argument order, how
> the element type is passed, the overflow policy and what errors look like
> are all yours to decide. It is loosely styled after RCL's
> `array_new(type, .opt = ...)` idiom. Note that the ticket says `rcl_ring`
> while RCL's existing types are `array_t`, `hashtable_t` and `string_t`
> with no prefix; that naming choice is yours too.

### 4.1 A plain FIFO queue

```c
ring_t *q = ring_new(int, .capacity = 8);

ring_push_back(q, &(int){ 10 });
ring_push_back(q, &(int){ 20 });
ring_push_back(q, &(int){ 30 });

int v;
assert(ring_pop_front(q, &v) && v == 10);   /* false if empty? your call */
assert(ring_count(q) == 2);
assert(*(int *)ring_front(q) == 20);
assert(*(int *)ring_back(q)  == 30);

ring_destroy(&q);   /* RCL style: takes T**, nulls it */
```

### 4.2 Wraparound you never see

```c
ring_t *q = ring_new(int, .capacity = 4);

for (int i = 0; i < 1000; i++) {
  ring_push_back(q, &i);
  if (ring_count(q) == 3) {
    int out;
    ring_pop_front(q, &out);
  }
}
/* contents -> 997 998 999; capacity is still 4; no allocation happened */
```

The front has gone around the circle hundreds of times. The caller cannot
tell, and should not be able to.

### 4.3 Indexed access and logical order

```c
/* Oldest to newest. Index 0 is always the front. */
for (size_t i = 0; i < ring_count(q); i++) {
  int *x = ring_at(q, i);
  printf("%d\n", *x);
}
```

With contents `997 998 999`, this prints three lines in that order, whatever
the physical layout happens to be.

### 4.4 Rolling history (fixed size, overwrite oldest)

```c
/* Frame times over the last 120 frames, for a smoothed FPS readout. */
ring_t *frame_ms = ring_new(float, .capacity = 120, .on_full = RING_OVERWRITE);

void on_frame(float ms) {
  ring_push_back(frame_ms, &ms);   /* drops the oldest once full */

  float sum = 0.f;
  for (size_t i = 0; i < ring_count(frame_ms); i++)
    sum += *(float *)ring_at(frame_ms, i);
  float avg = sum / (float)ring_count(frame_ms);
  /* ... */
}
```

For the first 119 frames the average is over fewer samples; after that,
always over exactly the last 120.

### 4.5 A deque: undo with a cap

```c
/* Newest edit at the back; undo pops the back; overflow drops the front. */
ring_t *undo = ring_new(edit_t, .capacity = 64, .on_full = RING_OVERWRITE);

ring_push_back(undo, &edit);              /* do */

edit_t last;
if (ring_pop_back(undo, &last))           /* undo */
  revert(&last);
```

A stack alone cannot "forget the oldest" cheaply; this needs both ends.

### 4.6 A growable queue

```c
ring_t *events = ring_new(game_event_t, .capacity = 16, .growable = true);

for (int i = 0; i < 100; i++)             /* a burst, nobody consuming */
  ring_push_back(events, &burst[i]);

assert(ring_count(events) == 100);        /* capacity grew, order kept */
```

The interesting version is the same burst arriving when the contents
already wrap (section 9).

### 4.7 A trap to keep in mind

```c
/* "It's just an array, right?" */
game_event_t *all = ring_data(events);     /* raw storage pointer */
for (size_t i = 0; i < ring_count(events); i++)
  handle(&all[i]);                          /* correct order? all of them? */
```

This works in every test where the ring was filled from empty and never
popped, and fails in production once the arc wraps. A second trap of the
same family:

```c
game_event_t *first = ring_at(events, 0);
ring_push_back(events, &another);          /* may grow, or may overwrite */
handle(first);                              /* is this still event 0? */
```

Decide what your documentation promises about pointers from `ring_at`, and
whether you expose raw storage at all. If you do, think about what a safe
"give me the contents as spans" call would have to return.

---

## 5. Relation to Z

All references below are to the working tree of `/Users/paulo/Projects/z`
as I read it (branch `feature/per-40`, with uncommitted changes in
`demo/src/input.c` and others). Line numbers may drift.

### 5.1 How input works today

**There is no event queue.** The main loop drains SDL's event queue every
frame (`demo/src/main.c:69-75`) but only acts on `SDL_EVENT_QUIT`. Every other
event (key down, key up, text, mouse, gamepad) is read and discarded.

**Input is polled state.** `input_state_t` (`demo/src/input.h:7-10`) holds
two arrays, `keys` and `prev_keys`, each `bool[SDL_SCANCODE_COUNT]`.
`input_system` (`demo/src/input.c:16-25`) copies `keys` into `prev_keys`, then
copies SDL's current keyboard snapshot (`SDL_GetKeyboardState`) into `keys`.
In effect this is already a tiny **history of length two**, hand-rolled as
two named arrays.

**Edges come from comparing snapshots.** `movement_system` decides that jump
was *pressed* (not held) when space is down now and was not down in the
previous snapshot (`input.c:46-47`). It then applies the jump if
`!player->jumping` (`input.c:48-51`). Left and right are read as held state
(`input.c:54-58`).

**Two clocks.** `input_system` runs once per *render* frame (`main.c:77`).
`movement_system` and `physics_system` run inside a fixed-timestep loop at
`FIXED_DT = 1/240 s` (`main.c:12`, loop at `main.c:79-83`), driven by an
accumulator fed by real elapsed time and clamped at 0.25 s (`main.c:58-63`).
So one input snapshot is seen by zero, one, or several physics ticks,
depending on how long the frame took.

### 5.2 What that costs

To be fair first: for a platformer prototype with one player and four keys,
polled state is a perfectly good design, and the `jumping` guard makes the
current code behave well. These are the seams, not bugs.

1. **A press is visible to every tick of its frame.** If a frame runs four
   ticks, `jump_pressed` is true in all four. Today the `jumping` flag
   absorbs that. Any future "pressed" action without such a guard (a dash,
   firing, a menu step) would fire once per tick, not once per press.
2. **A frame with zero ticks loses edges.** If a render frame is shorter
   than `FIXED_DT`, the accumulator loop does not run, and on the next frame
   `prev_keys` already equals `keys`. The press edge existed for exactly
   one snapshot, and no tick saw it. Whether this happens in practice
   depends on the frame rate (see 5.5).
3. **A tap shorter than one frame is invisible.** If space goes down and up
   between two calls to `input_system`, both snapshots say "up". The key-down
   and key-up *events* were in SDL's queue (`main.c:70`) and were thrown
   away.
4. **No ordering or timing within a frame.** Snapshots cannot say "left was
   released, then right was pressed, 3 ms apart".

### 5.3 How a ring buffer maps onto it

- **An input event queue between the two clocks.** The event pump
  (`main.c:70-75`) is a natural *producer*, pushing the events it cares
  about at the back in arrival order. The fixed-step loop is a natural
  *consumer*, taking each tick's events from the front: example 4.1's
  shape. Press edges then stop depending on whether a frame ran a tick, and
  short taps survive as a down/up pair. SDL3 events carry a timestamp,
  which is one way to assign events to ticks; I have not checked how Z
  would use it.
- **Fixed or growable?** A frame rarely produces more than a handful of
  events. A small fixed ring with a decided overflow policy is the
  embedded answer; a growable ring is the "never drop input" answer. Which
  would you rather debug: a dropped key-up (a stuck key), or an allocation
  in the input path?
- **Rolling history.** `keys` / `prev_keys` is a history of length two.
  Platformer feel features are short histories too: *jump buffering* ("was
  jump pressed in the last N ticks?") and *coyote time* ("was the player
  grounded in the last N ticks?"), each a small overwrite-oldest ring
  indexed from the back. The FPS logger (`main.c:16-31`) resets a counter
  every second; a ring of frame times (example 4.4) is the rolling version.
- **Replay.** Simulation advances in fixed ticks, so a replay is "the input
  for tick 0, tick 1, ...", recorded per *tick*, not per frame (ticks per
  frame vary, 5.1). At 240 ticks per second, a minute is 14,400 entries.
  The whole `keys` array per tick would be megabytes per minute; a compact
  record of the actions the game uses is a few bytes. "Instant replay of
  the last 10 seconds" is a fixed overwrite-oldest ring of 2,400 records;
  "record the whole session" is a growable one, or one flushed to disk.
  This only reproduces the game if the simulation is a pure function of
  initial state plus per-tick input; floating-point determinism across
  machines is a separate question a ring buffer does not touch.

### 5.4 Where ring semantics could bite in Z

1. **Pointers and indices held across changes** (the traps in 4.7). Growth
   or overwrite can invalidate a pointer; every `pop_front` renumbers
   logical indices, and a history indexed from the back renumbers on every
   push. Pick one convention per use and name it.
2. **Treating storage as one array.** Writing a replay to disk or handing
   a queue to a debug overlay has to respect the wrap.
3. **Overflow policy per use.** History wants overwrite-oldest. An input
   queue arguably never wants to drop a key-up. A replay recorder might
   want "full means stop". One RCL type may have to serve all three.

### 5.5 What I could not verify

- I did not measure the demo's frame rate, so I cannot say whether frames
  shorter than `FIXED_DT` (4.17 ms, above 240 FPS) occur. Rendering waits
  on `SDL_WaitAndAcquireGPUSwapchainTexture` (`engine/src/zengine.c:185`),
  and there is a `TODO: implement vsync wait` at `main.c:91`. I did not
  check which present mode the swapchain uses, so I do not know whether
  the loop is vsync-paced.
- I did not check the value of `SDL_SCANCODE_COUNT`, so the replay-size
  figure above is an order of magnitude, not a number.
- Z has no queue, deque or history structure today. RCL is used for
  `array_t` (`engine/src/tilemap.h:50`, `engine/src/zengine.priv.h:20`) and
  `hashtable_t` (`zengine.priv.h:22`), none of them in a FIFO role. I did
  not find anything that would conflict with adding one.
- I did not check whether SDL events are produced on any thread other than
  the one calling `SDL_PollEvent`. As read, all input handling in the demo
  is single-threaded, so a single-threaded ring is enough for it.

---

## 6. Guiding questions

Work through these in order before writing any code. Each one is meant to
pull a piece of the design out of you.

1. Popping the front must not move the other elements. So, after a pop,
   the front is somewhere other than slot 0. What is the smallest piece of
   state that records where it is?
2. With that state, write down (on paper, in words) which physical slot
   holds logical index `i`. Now take the drawing in section 2 and check
   your answer for `i = 0` and `i = 3`. What happens when the answer would
   be past the end of the block?
3. Is there one expression that covers both "past the end" and "not past
   the end"? What does it cost on your target CPU? Does the answer change
   if capacity is restricted to some family of numbers?
4. You need to know how many elements there are, and where the next push at
   the back goes. Can either be derived from the other plus what you
   already store? Which ones do you choose to store, and why?
5. Draw a ring of capacity 4 that is empty, and one that is full, both
   with the front at slot 2. Using only the fields you chose in question 4,
   do the two drawings look different? If not, list every way you can think
   of to make them different. What does each one cost?
6. Push at the front has to put an element "one before" the current front.
   What is "one before" slot 0? Is that the same question as question 2
   with a negative step?
7. Take the first drawing in section 2 and imagine `realloc` doubling the
   block to 16 slots. Where are `A` through `E` now, physically? Does
   invariant 6 still hold? What is the smallest amount of copying that
   restores it?
8. If you store counters rather than positions, what happens when a counter
   reaches the largest value its type can hold? Is that a bug, or can it be
   made harmless?
9. For an overwrite-when-full ring: when the push is at the back and the
   ring is full, which element is lost, and which pieces of state change?
   Same question for a push at the front.

---

## 7. Design decisions for you to make

None of these are answered here, on purpose.

**Shape**

- Fixed, growable, or one type that can be either (a flag at creation)?
  Deque from day one, or queue first?
- Is capacity what the caller asked for, or rounded up to something
  convenient? If rounded, what does `ring_capacity` report?

**Bookkeeping**

- Which numbers do you store? There are at least three classic answers to
  question 5 in section 6, differing in memory, branches, and constraints
  on capacity. Pick one deliberately and say why in the header.
- `size_t` like `array_t`, or something narrower? If a counter can run past
  capacity, is its wrap defined for the type you picked?

**Overflow and underflow**

- Push on a full fixed ring: reject, overwrite the oldest, or assert? One
  policy per type, per ring at creation, or per function?
- On overwrite, does the evicted element go through `free_func`, or back to
  the caller through an out-parameter?
- Pop, peek or `at(i >= count)` on too few elements: false, NULL, or a
  precondition violation?

**Genericity and ownership**

- Element size at runtime (`void *` plus `item_size`, like `array_t`), or a
  typed view macro like `ARRAY_OF(type)`? Can a typed view work at all, when
  logical index `i` is not `data[i]`?
- Push by pointer and `memcpy`, push by value through a macro (like
  `array_push`), or "emplace" (return a pointer to the new slot for the
  caller to fill)?
- A `free_func` like RCL's other containers? Called on pop (or does pop
  hand ownership to the caller?), on clear, on overwrite, on destroy?

**Access to the storage**

- Do you expose raw storage at all? If so, as "up to two spans" (pointer
  plus length, twice)? Or offer a call that makes the contents contiguous
  in place, at O(n), so the caller can treat it as an array?
- An iteration helper (`ring_foreach`, like `hashtable_foreach`), or just
  `count` plus `at`?

**Growth**

- Growth factor, and whether it ever shrinks.
- There is more than one correct way to restore invariant 6 when a wrapped
  ring grows, with different copy costs. Does the choice change anything
  observable, or only cost?
- Can growth fail? `array_push` (`src/rcl/array.h`) does not check
  `realloc`. Does the ring follow that?

**Concurrency and errors**

- A single-producer, single-consumer ring is the classic lock-free
  structure (audio callbacks, loader threads). Probably not v1, but does
  any choice above make it harder later?
- RCL style so far is asserts, returned pointers, and `bool` for "did it
  happen" (`hashtable_remove`). Does the ring follow that?

---

## 8. Hints (open only if stuck)

Nudges only. Each one points you in a direction and stops there.

> **Hint A, indexing.** A logical index is an offset from the front. The
> circle means offsets "wrap". You already know an arithmetic operator
> whose whole job is wrapping. Then ask what it costs, and when it is free.

> **Hint B, full vs empty.** The ambiguity exists because two different
> counts produce the same picture. Either make one of those counts
> impossible, or store something that tells them apart, or make the
> picture itself bigger than the circle. Each is a known design.

> **Hint C, growth.** `realloc` preserves bytes in physical order. Your
> invariant is about logical order. They agree only in one particular
> situation. Draw both cases.

> **Hint D, push front.** Going one step counter-clockwise from slot 0 is
> the same problem as going one step clockwise from the last slot, mirrored.
> Watch out for unsigned arithmetic going "below zero".

> **Hint E, overwrite.** An overwrite on a full ring is two operations you
> already have, done in a particular order. Check that the order matters
> for `free_func`.

> **Hint F, edge cases.** Check your reasoning for capacity 1, for a ring
> whose arc ends exactly at the last slot, and for a ring that is full with
> its front at slot 0 versus at any other slot.

> **Hint G, counters.** Look up exactly what the C standard says about
> unsigned overflow, and what it says about signed overflow. One of those
> is a tool; the other is a bug.

---

## 9. Test ideas

These are behaviours to check, not test code. They fit the existing RCL
pattern: a `src/ring_test.c` using Unity (`setUp`/`tearDown`, `static void
name(void)` cases, `RUN_TEST` in `main`, as in `src/array_test.c` and
`src/hashtable_test.c`), registered in `meson.build` under
`if get_option('tests')` like the others, and with the header added to the
`install_headers` list.

**Basics**

- A new ring is empty: count 0; front, back, pop and `at(0)` behave as
  documented for empty.
- Push back several, pop front all: push order (FIFO). Pop back all:
  reversed (LIFO). Push front several: `at(0)` is the last one pushed.

**Wraparound** (the ticket's first requirement)

- Fill, pop half, push half again: every `at(i)` is correct. The front is
  no longer at slot 0, and the test should not need to know that.
- A long push/pop loop with a small count, so the front laps the circle
  many times (example 4.2).
- Push front on a ring whose front is at the very start of the block, and
  mixed-end pushes that cross the end in both directions.

**Full and empty**

- Filled exactly to capacity: reports full. Popped back to nothing:
  reports empty, and a push still works.
- Both states with the front at several positions, not just slot 0. This
  catches schemes that only work from a fresh ring.
- Capacity 1: push, full, pop, empty, push again.

**Overflow policy** (whatever you decided in section 7)

- Push on a full fixed ring rejects, overwrites or asserts, as documented.
  With overwrite: the expected value is dropped, count stays at capacity,
  `free_func` runs exactly once for it.
- Pop, peek and out-of-range `at` behave as documented and change nothing.

**Growth** (the ticket's second requirement, if growable)

- Grow from a fresh ring: all elements survive in order.
- Grow while **wrapped**, by pushing at the back and, separately, at the
  front. Every `at(i)` matches. This is the test that catches "just
  realloc".
- After a growth, push and pop until it wraps at the new capacity.

**Ownership**

- `free_func` runs once per remaining element on destroy and clear, never
  for elements popped to the caller, never twice. A counting free function
  like `counting_free` in `hashtable_test.c` makes this easy.

**Invariants under stress**

- Randomized: thousands of mixed operations at both ends (plus clear and
  growth) against a trivially correct reference, such as a plain array
  that shifts with `memmove`. Compare count and every `at(i)` after each
  step. Fixed seed so failures reproduce.
- If counters run freely: a test-only way to start them near their maximum,
  so their own wrap is exercised in milliseconds.

**Z-shaped**

- A fixed overwrite-oldest ring of size N fed the values 0 .. 3N-1: `at(0)`
  is 2N and the back is 3N-1.
- Producer bursts, consumer pops a few per "tick": every value comes out
  exactly once, in order, across many rounds.

---

## 10. Further reading

These are named for further reading only. Read them *after* you have made
your own attempt, since most of them show the core arithmetic directly.

- **Donald Knuth, *The Art of Computer Programming*, Volume 1:
  Fundamental Algorithms**, section 2.2.2, "Sequential Allocation". The
  classic treatment of circular queues and deques.
- **Robert Nystrom, *Game Programming Patterns***, the "Event Queue"
  chapter. Game input and event queues, close to Z's situation.
- **Glenn Fiedler, "Fix Your Timestep!"** (Gaffer On Games). Background
  for Z's accumulator loop and for recording replays per tick.
- **Juho Snellman, "I've been writing ring buffers wrong all these years"**
  (blog post, 2016). Compares the classic full/empty answers. It gives
  them away, so save it for last.
- **The Linux kernel's `kfifo`** (`include/linux/kfifo.h`) and the
  kernel's circular-buffer documentation. A production ring, and the
  single-producer, single-consumer case.
- **Rust's `std::collections::VecDeque`**. A growable ring deque; its
  `as_slices` and `make_contiguous` methods are a lens for the "access to
  storage" decisions in section 7.

---

## If you only remember three things

1. **Logical order is not physical order.** The sequence is an arc on a
   circle of slots. It can start anywhere and cross the end of the block.
   Everything (indexing, both ends, iteration) is a cheap, exact mapping
   from logical to physical, and nothing ever shifts.
2. **Full and empty are the famous trap.** Decide how your stored state
   tells them apart, and how growth keeps logical order when the contents
   wrap. Those two decisions are most of the design; test both with the
   front somewhere other than slot 0.
3. **In Z, a ring buffer fits between two clocks.** Today input is two
   snapshots compared once per frame, and events are discarded
   (`main.c:69-75`, `input.c:16-25`). A queue, a short history, and a
   per-tick replay recorder are three uses with three different overflow
   policies, and pointers or indices held across pushes and pops are the
   new hazard.
