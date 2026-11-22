#include "rcl/sparse_set.h"
#include <assert.h>
#include <stdlib.h>

sparse_set_t *sparse_set_new_full(sparse_set_init_t init) {
  assert(init.max_items > 0);
  sparse_set_t *self = malloc(sizeof(*self));

  self->sparse = malloc(init.max_items * sizeof(size_t));
  self->dense = malloc(init.max_items * sizeof(sparse_set_item_t));
  self->length = 0;
  self->max_items = init.max_items;
  self->free_func = init.free_func;

  return self;
}

bool sparse_set_has(sparse_set_t *self, size_t index) {
  if (index >= self->max_items) {
    return false;
  }

  size_t dense_index = self->sparse[index];

  if (dense_index >= self->length) {
    return false;
  }

  sparse_set_item_t *item = &self->dense[dense_index];
  return item->index == index;
}

void sparse_set_add(sparse_set_t *self, size_t index, void *value) {
  assert(index < self->max_items);

  if (sparse_set_has(self, index)) {
    size_t dense_index = self->sparse[index];
    void *current = self->dense[dense_index].data;
    // Free existing item
    if (self->free_func && current != value) {
      self->free_func(current);
    }
    self->dense[dense_index].data = value;
  } else {
    size_t dense_index = self->length++;
    self->dense[dense_index] =
        (sparse_set_item_t){.index = index, .data = value};
    self->sparse[index] = dense_index;
  }
}

bool sparse_set_remove(sparse_set_t *self, size_t index) {
  if (!sparse_set_has(self, index)) {
    return false;
  }

  size_t dense_index = self->sparse[index], last = self->length - 1;
  // Free item
  if (self->free_func) {
    self->free_func(self->dense[dense_index].data);
  }

  if (dense_index < last) {
    self->dense[dense_index] = self->dense[last];
    self->sparse[self->dense[dense_index].index] = dense_index;
  }

  self->dense[last] = (sparse_set_item_t){};
  self->length--;
  return true;
}

void *sparse_set_get(sparse_set_t *self, size_t index) {
  if (!sparse_set_has(self, index)) {
    return NULL;
  }

  size_t dense_index = self->sparse[index];
  return self->dense[dense_index].data;
}

void sparse_set_clear(sparse_set_t *self) {
  if (self->free_func) {
    for (size_t i = 0; i < self->length; i++) {
      self->free_func(self->dense[i].data);
    }
  }

  self->length = 0;
}

void sparse_set_free(sparse_set_t *self) {
  sparse_set_clear(self);
  free(self->sparse);
  free(self->dense);
  free(self);
}

void sparse_set_destroy(sparse_set_t **self) {
  if (*self) {
    sparse_set_free(*self);
    *self = NULL;
  }
}
