#include "hashtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const size_t _table_sizes[] = {
    769,       1543,      3079,      6151,      12289,    24593,
    49157,     98317,     196613,    393241,    786433,   1572869,
    3145739,   6291469,   12582917,  25165843,  50331653, 100663319,
    201326611, 402653189, 805306457, 1610612741};
const size_t _table_sizes_count =
    sizeof(_table_sizes) / sizeof(_table_sizes[0]);

#ifndef HASHTABLE_DEFAULT_CAPACITY
#define HASHTABLE_DEFAULT_CAPACITY 769
#endif

// FNV-1a hash function

#define FNV_PRIME_32 16777619
#define FNV_OFFSET_32 2166136261

__attribute__((always_inline)) static inline size_t fnv1a(const char *key) {
  const unsigned char *d = (const unsigned char *)key;
  size_t hash = FNV_OFFSET_32;

  for (size_t i = 0; key[i]; i++) {
    hash ^= (size_t)d[i];
    hash *= FNV_PRIME_32;
  }

  return hash;
}

static size_t get_next_table_size(const size_t current_size) {
  for (size_t i = 0; i < _table_sizes_count; i++) {
    if (_table_sizes[i] > current_size) {
      return _table_sizes[i];
    }
  }
  return current_size * 2;
}

static size_t hashtable_hash(hashtable_t *self, const char *key) {
  size_t index = self->hash_func(key) % self->capacity;
  size_t i = 0;

  while (self->items[index].key != NULL &&
         strcmp(self->items[index].key, key) != 0) {
    i++;
    index = (index + i * i) % self->capacity;
  }

  return index;
}

/**
 * Grow the hashtable by doubling its capacity and rehashing all the items
 */
static void hashtable_grow(hashtable_t *self) {
  size_t new_capacity = get_next_table_size(self->capacity);

  hashtable_t *tmp_table = hashtable_new_with_capacity(new_capacity);

  for (size_t i = 0; i < self->capacity; i++) {
    if (self->items[i].key == NULL) {
      continue;
    }
    size_t new_index = hashtable_hash(tmp_table, self->items[i].key);
    tmp_table->items[new_index] = self->items[i];
  }

  free(self->items);
  self->items = tmp_table->items;
  self->capacity = new_capacity;

  // We set the items to NULL so that the free function doesn't free the
  // keys and values.
  tmp_table->items = NULL;
  hashtable_free(tmp_table);
}

hashtable_t *hashtable_new(void) {
  return hashtable_new_with_capacity(HASHTABLE_DEFAULT_CAPACITY);
}

hashtable_t *hashtable_new_with_capacity(size_t initial_capacity) {
  hashtable_t *self = malloc(sizeof(*self));

  *self = (hashtable_t){
      .items = calloc(initial_capacity, sizeof(*self->items)),
      .capacity = initial_capacity,
      .free_func = NULL,
      .hash_func = &fnv1a,
  };

  return self;
}

__attribute__((always_inline)) inline void
hashtable_set_free_func(hashtable_t *self, hashtable_free_func_t func)

{
  self->free_func = func;
}

// Whenever we implement hashtable_set_hash_func we need to re-hash the entire
// table

bool hashtable_exists(hashtable_t *self, const char *key) {
  size_t index = hashtable_hash(self, key);
  return self->items[index].key != NULL;
}

void *hashtable_get(hashtable_t *self, const char *key) {
  size_t index = hashtable_hash(self, key);
  return self->items[index].value;
}

void hashtable_set(hashtable_t *self, const char *key, void *value) {
  size_t index = hashtable_hash(self, key);
  item_t *item = &self->items[index];

  // Set a new item
  if (item->key == NULL) {
    // Grow the table if it's more than 70% full
    if ((float)self->length / self->capacity >= 0.7f) {
      // Grow the table
      hashtable_grow(self);
      hashtable_set(self, key, value);
      return;
    }
    self->length++;

    item->value = value;
    item->key = strdup(key);
  }
  // Replace an existing item
  else {
    if (item->value && self->free_func) {
      self->free_func(item->value);
    }
    item->value = value;
  }
}

bool hashtable_remove(hashtable_t *self, const char *key, void **value) {
  size_t index = hashtable_hash(self, key);
  item_t *item = &self->items[index];

  if (item->key == NULL) {
    if (value) {
      *value = NULL;
    }
    return false;
  }

  if (value) {
    *value = item->value;
  }
  free(item->key);
  item->key = NULL;
  item->value = NULL;
  self->length--;
  return true;
}

void hashtable_free(hashtable_t *self) {
  if (!self)
    return;

  if (self->items) {
    for (size_t i = 0; i < self->capacity; i++) {
      if (self->items[i].key != NULL) {
        if (self->free_func) {
          self->free_func(self->items[i].value);
        }
        free(self->items[i].key);
      }
    }
    free(self->items);
  }

  free(self);
}

bool hashtable_delete(hashtable_t *self, const char *key) {
  size_t index = self->hash_func(key) % self->capacity;
  item_t *item = &self->items[index];

  if (item->key == NULL) {
    return false;
  }

  if (item->value && self->free_func) {
    self->free_func(item->value);
  }
  free(item->key);
  item->key = NULL;
  item->value = NULL;
  self->length--;
  return true;
}
