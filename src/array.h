#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_DEFAULT_CAPACITY 32

/**
 * A function that frees an element of an array. If set, it will be called on
 * each element of the array when the array is destroyed or an element is
 * destroyed.
 *
 * @param ptr the element to free. This is the same pointer that was stored in
 * the array. If you stored a `char*` in the array, this will be a `char*`.
 */
typedef void(array_free_func)(void *ptr);

/**
 * A function that compares two elements of an array. This function should
 * return a negative number if `a` is less than `b`, a positive number if `a` is
 * greater than `b`, and 0 if `a` is equal to `b`.
 *
 * @param a a pointer to the first element to compare.
 * @param b a pointer to the second element to compare.
 * @return a negative number if `a` is less than `b`, a positive number if `a`
 * is greater than `b`, and 0 if `a` is equal to `b`.
 */
typedef int(array_compare_func)(const void *a, const void *b);

typedef struct s_array {
  void *data;
  size_t capacity;
  size_t length;
  size_t item_size;

  array_free_func *free_func;
} array_t;

typedef struct s_array_init {
  size_t capacity;
  size_t item_size;
  array_free_func *free_func;
} array_init_t;

/**
 * Create a new, empty array.
 *
 * @param init the initialization parameters for the array.
 * @return a pointer to the new array.
 */
array_t *array_new_full(array_init_t init);

#define array_new(type, ...)                                                   \
  array_new_full((array_init_t){.capacity = ARRAY_DEFAULT_CAPACITY,            \
                                .item_size = sizeof(type),                     \
                                .free_func = NULL,                             \
                                __VA_ARGS__})

#define ARRAY_OF(type)                                                         \
  struct {                                                                     \
    type *data;                                                                \
    size_t capacity;                                                           \
    size_t length;                                                             \
    size_t item_size;                                                          \
                                                                               \
    array_free_func *free_func;                                                \
  }

/**
 * Add an item to the end of the array.
 *
 * @param arr the array to add the item to.
 * @param item the item to add.
 */
#define array_push(arr, item)                                                  \
  do {                                                                         \
    __auto_type __item = (item);                                               \
    array_t *__arr = (arr);                                                    \
    if (__arr->length == __arr->capacity) {                                    \
      __arr->data =                                                            \
          realloc(__arr->data, __arr->capacity * 2 * __arr->item_size);        \
      __arr->capacity *= 2;                                                    \
    }                                                                          \
    ((__typeof__(__item) *)__arr->data)[__arr->length++] = __item;             \
  } while (0)

/**
 * Free an array and set its pointer to NULL.
 *
 * @param arr a pointer to the array to destroy.
 */
void array_destroy(array_t **arr);

/**
 * Free an array.
 *
 * @param arr the array to free.
 */
void array_free(array_t *arr);

/**
 * Sort an array using a comparison function.
 *
 * @param arr the array to sort.
 * @param cmp the comparison function to use.
 */
void array_sort(array_t *arr, array_compare_func *cmp);
