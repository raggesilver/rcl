#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifndef HASHTABLE_TOMBSTONE_MARKER
#define HASHTABLE_TOMBSTONE_MARKER ((void *)-1)
#endif

/**
 * A function used to free values in the hashtable. This function will be called
 * on every value in the hashtable when the hashtable is freed.
 *
 * @param ptr the value to free
 */
typedef void (*hashtable_free_func_t)(void *ptr);

/**
 * A hash function used to hash `hashtable_t` keys.
 *
 * @param key the key to be hashed
 * @returns a hased index for the given key. note this hash will be modulo'd
 * before being used to index the hashtable.
 */
typedef size_t (*hashtable_hash_func_t)(const char *key);

typedef struct s_item {
  char *key;
  void *value;
} item_t;

typedef struct s_hashtable {
  item_t *items;
  size_t capacity;
  size_t length;

  hashtable_free_func_t free_func;
  hashtable_hash_func_t hash_func;
} hashtable_t;

/**
 * Create a new hashtable with the default capacity.
 *
 * @returns a new hashtable with the default capacity
 */
hashtable_t *hashtable_new(void);

/**
 * Create a new hashtable with a specific initial capacity.
 *
 * @param initial_capacity the initial capacity of the hashtable
 * @returns a new hashtable with the given initial capacity
 */
hashtable_t *hashtable_new_with_capacity(size_t initial_capacity);

/**
 * Free the hashtable and all of its values. If a free function has been set
 * with `hashtable_set_free_func`, it will be called on every value in the
 * hashtable.
 *
 * @param self the hashtable to free
 */
void hashtable_free(hashtable_t *self);

/**
 * Set the free function for the hashtable. This function will be called on
 * every value in the hashtable when the hashtable is freed.
 *
 * @param self the hashtable to set the free function for
 * @param func the function to call on every value in the hashtable
 */
void hashtable_set_free_func(hashtable_t *self, hashtable_free_func_t func);

/**
 * Check if a key exists in the hashtable.
 *
 * @param self the hashtable to check
 * @param key the key to check for
 * @returns true if the key exists in the hashtable, false otherwise
 */
bool hashtable_exists(hashtable_t *self, const char *key);

/**
 * Get a value from the hashtable.
 *
 * @param self the hashtable to get the value from
 * @param key the key to get the value for
 * @returns the value associated with the key, or NULL if the key does not
 * exist
 */
void *hashtable_get(hashtable_t *self, const char *key);

/**
 * Set a value in the hashtable.
 *
 * WARNING: if you call set a value for a key that already exists in the
 * hashtable, the old value will be freed if a free function has been set for
 * this table. If you want to avoid this behavior, you can use
 * `hashtable_exists` to check if the key already exists before setting the
 * value.
 *
 * @param self the hashtable to set the value in
 * @param key the key to set the value for
 * @param value the value to set
 */
void hashtable_set(hashtable_t *self, const char *key, void *value);

/**
 * Remove a value from the hashtable. Note that this function will not free the
 * value that was removed. If you want to free the value, you must do so
 * manually.
 *
 * @param self the hashtable to remove the value from
 * @param key the key to remove the value for
 * @param value a pointer to store the value that was removed
 * @returns true if the key existed and was removed, false otherwise
 */
bool hashtable_remove(hashtable_t *self, const char *key, void **value);

/**
 * Delete a key from the hashtable. This function will remove the key and free
 * the value associated with the key if a free function was set for this table.
 *
 * @param self the hashtable to delete the key from
 * @param key the key to delete
 * @returns true if the key existed and was deleted, false otherwise
 */
bool hashtable_delete(hashtable_t *self, const char *key);

/**
 * Iterate over all the items in the hashtable.
 *
 * @param table the hashtable to iterate over
 * @param fn the function to call on each item in the hashtable
 *
 * @note the function `fn` should be a block of code that uses the variables
 * `key` and `value` to access the current item in the hashtable
 */
#define hashtable_foreach(table, fn)                                           \
  for (size_t i = 0; i < table->capacity; i++) {                               \
    if (table->items[i].key == NULL ||                                         \
        table->items[i].key == HASHTABLE_TOMBSTONE_MARKER) {                   \
      continue;                                                                \
    }                                                                          \
    {                                                                          \
      __attribute__((unused)) const char *key = table->items[i].key;           \
      __attribute__((unused)) void *value = table->items[i].value;             \
      fn                                                                       \
    }                                                                          \
  }
