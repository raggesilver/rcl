#pragma once

#include <stddef.h>
#include <stdlib.h>

typedef struct s_array {
  void *data;
  size_t capacity;
  size_t length;
  size_t item_size;
} array_t;

typedef struct s_array_init {
  size_t capacity;
  size_t item_size;
} array_init_t;

array_t *array_new_full(array_init_t init);

#define ARRAY_DEFAULT_CAPACITY 20

#define array_new(type, ...)                                                   \
  array_new_full((array_init_t){.capacity = ARRAY_DEFAULT_CAPACITY,            \
                                .item_size = sizeof(type),                     \
                                __VA_ARGS__})

#define ARRAY_OF(type)                                                         \
  struct {                                                                     \
    type *data;                                                                \
    size_t capacity;                                                           \
    size_t length;                                                             \
    size_t item_size;                                                          \
  }

#define array_push(arr, item)                                                  \
  do {                                                                         \
    ARRAY_OF(__typeof__((item))) *__arr = (void *)(arr);                       \
    if (__arr->length == __arr->capacity) {                                    \
      __arr->data =                                                            \
          realloc(__arr->data, __arr->capacity * 2 * __arr->item_size);        \
      __arr->capacity *= 2;                                                    \
    }                                                                          \
    __arr->data[__arr->length++] = item;                                       \
  } while (0)

void array_destroy(array_t **arr);

void array_free(array_t *arr);
