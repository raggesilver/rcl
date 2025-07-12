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

  /**
   * A function that frees an element of the array. If set, it will be called on
   * each element of the array when the array is destroyed or an element is
   * destroyed. Mind you, setting a function for non-pointer types will result
   * in wonderful segfaults.
   */
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
 * Create a new array by transforming each element from the source array into
 * a new value in the new array.
 *
 * Be VERY CAREFUL, as this function does not set a free_func on the new array.
 * If you do not do that yourself, you may create a memory leak.
 *
 * @param arr the source array
 * @param from_type type of elements on source array
 * @param to_type type of elements on the new array
 * @param func a function that takes from_type and returns to_type
 * @returns the new array
 */
#define array_map(arr, from_type, to_type, func)                               \
  ({                                                                           \
    ARRAY_OF(from_type) *__arr = (void *)(arr);                                \
    array_t *__res =                                                           \
        array_new_full((array_init_t){.capacity = __arr->length,               \
                                      .item_size = sizeof(to_type),            \
                                      .free_func = NULL});                     \
    ARRAY_OF(to_type) *__res_arr = (void *)__res;                              \
    for (size_t i = 0; i < __arr->length; i++) {                               \
      __res_arr->data[i] = func(__arr->data[i]);                               \
    }                                                                          \
    __res->length = __arr->length;                                             \
    __res;                                                                     \
  })

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

/**
 * Delete an element from an array.
 *
 * @param arr the array to delete from.
 * @param index the index of the element to delete.
 */
void array_delete(array_t *arr, size_t index);

/**
 * Remove an element from an array. The element will not be freed, instead it
 * will be copied to the location pointed to by `item_out`.
 *
 * @param arr the array to remove from.
 * @param index the index of the element to remove.
 * @param item_out a pointer to where the removed item should be stored.
 */
__attribute__((nonnull(3))) void array_remove(array_t *arr, size_t index,
                                              void *item_out);
