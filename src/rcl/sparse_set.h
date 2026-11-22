#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * A function used to free values stored in a sparse set. It is called when a
 * value is replaced, removed, cleared, or when the set is freed.
 *
 * @param data the value to free. This is the same pointer that was added.
 */
typedef void(sparse_set_free_func_t)(void *data);

typedef struct sparse_set_item_s {
  size_t index;
  void *data;
} sparse_set_item_t;

/**
 * A sparse set mapping integer ids in `[0, max_items)` to `void *` values.
 *
 * Add, remove, lookup and clear are O(1) (clear is O(n) if a `free_func` is
 * set). Members are stored contiguously in `dense`, so iterating over
 * `dense[0..length)` is fast. Removing an item moves the last item into its
 * slot, so iteration order is NOT stable across removals.
 */
typedef struct sparse_set_s {
  /** The (fixed) exclusive upper bound for ids, and the number of members. */
  size_t max_items, length;

  /** Maps an id to its slot in `dense`. Never initialized, only validated. */
  size_t *sparse;
  /** The members, packed in `[0, length)`. */
  sparse_set_item_t *dense;

  /** Called on values that are replaced, removed or cleared. May be NULL. */
  sparse_set_free_func_t *free_func;
} sparse_set_t;

typedef struct {
  size_t max_items;
  sparse_set_free_func_t *free_func;
} sparse_set_init_t;

/**
 * Create a new, empty sparse set.
 *
 * @param init the initialization parameters. `max_items` must be greater than
 * zero.
 * @return a pointer to the new sparse set.
 */
sparse_set_t *sparse_set_new_full(sparse_set_init_t init);

/**
 * Create a new, empty sparse set with defaults (`max_items = 1024`, no
 * `free_func`). Any field of `sparse_set_init_t` can be overridden, e.g.
 * `sparse_set_new(.max_items = 64, .free_func = &free)`.
 */
#define sparse_set_new(...)                                                    \
  sparse_set_new_full(                                                         \
      (sparse_set_init_t){.max_items = 1024, .free_func = NULL, __VA_ARGS__})

/**
 * Check whether an id is in the set.
 *
 * @param self the sparse set.
 * @param index the id to look for.
 * @return true if the id is in the set. Ids >= `max_items` return false.
 */
bool sparse_set_has(sparse_set_t *self, size_t index);

/**
 * Add a value under an id. If the id already exists its value is replaced, and
 * the old value is freed with `free_func` (unless it is the same pointer).
 *
 * @param self the sparse set.
 * @param index the id. Must be less than `max_items`.
 * @param value the value to store.
 */
void sparse_set_add(sparse_set_t *self, size_t index, void *value);

/**
 * Remove an id from the set. The value is freed with `free_func` if set. The
 * last item takes the removed item's slot in `dense`.
 *
 * @param self the sparse set.
 * @param index the id to remove.
 * @return true if the id was in the set, false otherwise.
 */
bool sparse_set_remove(sparse_set_t *self, size_t index);

/**
 * Get the value stored under an id.
 *
 * @param self the sparse set.
 * @param index the id to look up.
 * @return the value, or NULL if the id is not in the set.
 */
void *sparse_set_get(sparse_set_t *self, size_t index);

/**
 * Remove every item from the set, calling `free_func` on each value if set.
 *
 * @param self the sparse set.
 */
void sparse_set_clear(sparse_set_t *self);

/**
 * Free a sparse set, calling `free_func` on each remaining value if set.
 *
 * @param self the sparse set to free.
 */
void sparse_set_free(sparse_set_t *self);

/**
 * Free a sparse set and set its pointer to NULL. Does nothing if the pointer
 * is already NULL.
 *
 * @param self a pointer to the sparse set to destroy.
 */
void sparse_set_destroy(sparse_set_t **self);
