#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void(sparse_set_free_func_t)(void *data);

typedef struct sparse_set_item_s {
  size_t index;
  void *data;
} sparse_set_item_t;

typedef struct sparse_set_s {
  size_t max_items, length;

  size_t *sparse;
  sparse_set_item_t *dense;

  sparse_set_free_func_t *free_func;
} sparse_set_t;

typedef struct {
  size_t max_items;
  sparse_set_free_func_t *free_func;
} sparse_set_init_t;

sparse_set_t *sparse_set_new_full(sparse_set_init_t init);

#define sparse_set_new(...)                                                    \
  sparse_set_new_full(                                                         \
      (sparse_set_init_t){.max_items = 1024, .free_func = NULL, __VA_ARGS__})

bool sparse_set_has(sparse_set_t *self, size_t index);
void sparse_set_add(sparse_set_t *self, size_t index, void *value);
bool sparse_set_remove(sparse_set_t *self, size_t index);
void *sparse_set_get(sparse_set_t *self, size_t index);
void sparse_set_clear(sparse_set_t *self);
void sparse_set_free(sparse_set_t *self);
void sparse_set_destroy(sparse_set_t **self);
