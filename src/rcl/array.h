#pragma once

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

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

/**
 * A function that filters elements of an array. This function should return
 * true if the element should be kept, or false if it should be removed.
 *
 * @param a a pointer to the element to filter.
 * @return true if the element should be kept, false otherwise.
 */
typedef bool(array_filter_func)(const void *a);

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
    size_t __cap =                                                              \
        __arr->length > 0 ? __arr->length : ARRAY_DEFAULT_CAPACITY;            \
    array_t *__res =                                                           \
        array_new_full((array_init_t){.capacity = __cap,                       \
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
 * Filter elements in-place, removing those not passing the filter.
 *
 * This function calls `array_delete(self, index_to_remove)` internally, meaning
 * removed element will be free'd if you set a free function for the array.
 *
 * @param self the array to filter.
 * @param fn the filter function.
 * @return the filtered array.
 */
array_t *array_filter(array_t *self, array_filter_func *fn);

/**
 * Create a new array by copying elements that pass a filter function.
 *
 * The copy function receives each element by value and should return a copied
 * value. For pointer types (e.g. `char *`), this would be a function like
 * `strdup`. For value types, this can be an identity function.
 *
 * The new array inherits the source array's free_func.
 *
 * @param arr the source array.
 * @param type the type of elements in the array.
 * @param filter_fn a filter function (receives pointer to element).
 * @param copy_fn a copy function (receives element by value, returns copy).
 * @returns a new filtered array.
 */
#define array_filter_copy(arr, type, filter_fn, copy_fn)                       \
  ({                                                                           \
    ARRAY_OF(type) *__arr = (void *)(arr);                                     \
    array_t *__res = array_new_full(                                           \
        (array_init_t){.capacity = __arr->length ?: ARRAY_DEFAULT_CAPACITY,    \
                       .item_size = sizeof(type),                              \
                       .free_func = ((array_t *)__arr)->free_func});           \
    ARRAY_OF(type) *__res_arr = (void *)__res;                                 \
    for (size_t i = 0; i < __arr->length; i++) {                               \
      if (filter_fn(&__arr->data[i])) {                                        \
        __res_arr->data[__res->length++] = copy_fn(__arr->data[i]);            \
      }                                                                        \
    }                                                                          \
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
