#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "rcl/common.h"

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
   * destroyed.
   *
   * It can only be set with `array_new_full` or `array_set_free_func`, which
   * assert that `item_size` is `sizeof(void *)`: elements are passed to the
   * function by value, so they must be pointers. Do not write to it directly.
   */
  array_free_func *const free_func;
} array_t;

typedef struct s_array_init {
  size_t capacity;
  size_t item_size;
  array_free_func *free_func;
} array_init_t;

/**
 * Create a new, empty array.
 *
 * If `init.free_func` is set, `init.item_size` must be `sizeof(void *)`. This
 * is checked with `assert`, so it is not enforced when compiled with `NDEBUG`.
 *
 * @param init the initialization parameters for the array.
 * @return a pointer to the new array.
 */
array_t *array_new_full(array_init_t init);

/**
 * Set (or clear) the function used to free the elements of an array.
 *
 * Nothing is freed now: this only changes what happens to the elements that are
 * in the array, and the ones added later, when they are deleted or the array is
 * freed. `item_size` must be `sizeof(void *)` unless `func` is NULL. This is
 * checked with `assert`, so it is not enforced when compiled with `NDEBUG`.
 *
 * @param arr the array.
 * @param func the function that frees an element, or NULL for none.
 */
void array_set_free_func(array_t *arr, array_free_func *func);

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
    array_free_func *const free_func;                                          \
  }

/**
 * Get a pointer to the element at `index`, without knowing its type. This is
 * `(char *)arr->data + index * arr->item_size`.
 *
 * No bounds checking is done: `index` must be less than `arr->length` (or equal
 * to it, if you are about to write the slot yourself and have the capacity).
 * The pointer is invalidated by anything that may reallocate the array, like
 * `array_push`.
 *
 * @param arr the array.
 * @param index the index of the element.
 * @return a pointer to the element. For an array of `char *` this is a
 * `char **`, the address of the slot, not the stored pointer.
 */
static inline void *array_at(const array_t *arr, size_t index) {
  return (char *)arr->data + index * arr->item_size;
}

/**
 * Loop over every element of an array, like a `for` loop. The body follows the
 * macro, and `break` and `continue` work as usual:
 *
 *     array_foreach(arr, entry_t, it, i) {
 *       printf("%zu: %d\n", i, it->layer);
 *     }
 *
 * `it` and `i` are declared by the macro and are scoped to the loop.
 *
 * `arr` is evaluated more than once, so it must not have side effects. The
 * length is read once when the loop starts: do not push to or delete from the
 * array inside the loop, as that can reallocate the data (leaving `it`
 * dangling) or change the length.
 *
 * @param arr the array to loop over.
 * @param type the type of the elements. It must match the array's `item_size`.
 * @param it the name of the variable holding a `type *` to the current element.
 * For an array of `char *`, `type` is `char *` and `it` is a `char **`: the
 * address of the slot, not the stored pointer.
 * @param i the name of the `size_t` variable holding the current index.
 */
#define array_foreach(arr, type, it, i)                                        \
  for (size_t i = 0, __n = (arr)->length, __once = 1; __once; __once = 0)      \
    for (type *it = (type *)(arr)->data; i < __n; i++, it++)

/**
 * Like `array_foreach`, but `it` is a copy of the element instead of a pointer
 * to it. This is handier for arrays of pointers, where it gives you the stored
 * pointer (`char *`) rather than the address of its slot (`char **`):
 *
 *     array_foreach_value(names, char *, name, i) {
 *       printf("%zu: %s\n", i, name);
 *     }
 *
 * Because `it` is a copy, assigning to it does not change the array: use
 * `array_foreach` to modify elements in place. Large structs are copied on
 * every iteration.
 *
 * The same rules as `array_foreach` apply: `arr` is evaluated more than once,
 * and the array must not be pushed to or deleted from inside the loop.
 *
 * @param arr the array to loop over.
 * @param type the type of the elements. It must match the array's `item_size`.
 * @param it the name of the variable holding a copy of the current element.
 * @param i the name of the `size_t` variable holding the current index.
 */
#define array_foreach_value(arr, type, it, i)                                  \
  for (size_t i = 0, __n = (arr)->length, __once = 1; __once; __once = 0)      \
    for (type it; i < __n && ((it = ((type *)(arr)->data)[i]), 1); i++)

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
 * If you do not do that yourself with `array_set_free_func`, you may create a
 * memory leak.
 *
 * @param arr the source array
 * @param from_type type of elements on source array
 * @param to_type type of elements on the new array
 * @param func a function that takes from_type and returns to_type
 * @returns the new array
 */
#define array_map(arr, from_type, to_type, func)                               \
  ({                                                                           \
    array_t *__arr = (arr);                                                    \
    size_t __cap = __arr->length > 0 ? __arr->length : ARRAY_DEFAULT_CAPACITY; \
    array_t *__res = array_new_full((array_init_t){                            \
        .capacity = __cap, .item_size = sizeof(to_type), .free_func = NULL});  \
    for (size_t i = 0; i < __arr->length; i++) {                               \
      ((to_type *)__res->data)[i] = func(((from_type *)__arr->data)[i]);       \
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
    array_t *__arr = (arr);                                                    \
    array_t *__res = array_new_full(                                           \
        (array_init_t){.capacity = __arr->length ?: ARRAY_DEFAULT_CAPACITY,    \
                       .item_size = sizeof(type),                              \
                       .free_func = __arr->free_func});                        \
    size_t __count = 0;                                                        \
    for (size_t i = 0; i < __arr->length; i++) {                               \
      if (filter_fn(&((type *)__arr->data)[i])) {                              \
        ((type *)__res->data)[__count++] = copy_fn(((type *)__arr->data)[i]);  \
      }                                                                        \
    }                                                                          \
    __res->length = __count;                                                   \
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

/**
 * Find an element in a sorted array using binary search.
 *
 * The array must already be sorted according to `cmp`, otherwise the result is
 * unspecified. If several elements compare equal to `key`, any one of them may
 * be found.
 *
 * @param arr the array to search. Must be sorted by `cmp`.
 * @param key a pointer to the value to look for. Like the array elements, it is
 * passed to `cmp` as a pointer (for an array of `char *`, `key` is a `char
 * **`).
 * @param cmp the comparison function. `key` is passed as the first argument and
 * an element of the array as the second.
 * @param index_out where to store the index of the match. May be NULL if the
 * index is not needed. Set to 0 when nothing is found.
 * @return true if a matching element was found, false otherwise.
 */
bool array_bsearch(const array_t *arr, const void *key, array_compare_func *cmp,
                   size_t out index_out);

/**
 * Delete an element by swapping the last element into its place. O(1), but the
 * order of the array is not preserved. The deleted element is freed with
 * `free_func` if one is set, like `array_delete`.
 *
 * @param arr the array to delete from.
 * @param index the index of the element to delete.
 * @return true if the element was deleted, false if `index` is out of bounds
 * (the array is left unchanged).
 */
bool array_swap_delete(array_t *arr, size_t index);

/**
 * Remove an element by swapping the last element into its place. O(1), but the
 * order of the array is not preserved. The element is not freed, instead it is
 * copied to the location pointed to by `item_out`, like `array_remove`.
 *
 * `item_out` is always written: it is zeroed if `index` is out of bounds.
 *
 * @param arr the array to remove from.
 * @param index the index of the element to remove.
 * @param item_out a pointer to where the removed item should be stored.
 * @return true if the element was removed, false if `index` is out of bounds
 * (the array is left unchanged).
 */
__attribute__((nonnull(3))) bool array_swap_remove(array_t *arr, size_t index,
                                                   void *item_out);
