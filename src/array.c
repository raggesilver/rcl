#include "array.h"
#include <stdio.h>
#include <stdlib.h>

array_t *array_new_full(array_init_t init) {
  array_t *array = malloc(sizeof(array_t));

  *array = (array_t){
      .data = malloc(init.capacity * init.item_size),
      .capacity = init.capacity,
      .length = 0,
      .item_size = init.item_size,
      .free_func = init.free_func,
  };

  return array;
}

void array_free(array_t *arr) {
  if (!arr)
    return;

  if (arr->free_func != NULL) {
    if (arr->item_size != sizeof(void *)) {
      fprintf(
          stderr,
          "Warning: free function set, but item size is not sizeof(void *)\n");
    } else {
      ARRAY_OF(void *) *_arr = (void *)arr;
      for (size_t i = 0; i < _arr->length; i++) {
        // We don't know the type of the item, so we need to calculate the
        // address of the item using pointer arithmetic. We use the item_size
        // to calculate the offset of the item from the start of the data array.
        _arr->free_func(_arr->data[i]);
      }
    }
  }

  free(arr->data);
  free(arr);
}

__attribute__((always_inline)) inline void array_destroy(array_t **arr) {
  if (!arr || !*arr)
    return;

  array_free(*arr);
  *arr = NULL;
}

void array_sort(array_t *arr, array_compare_func *cmp) {
  qsort(arr->data, arr->length, arr->item_size, cmp);
}

__attribute__((always_inline)) static inline int
reverse_compare(void *cmp, const void *a, const void *b) {
  return -((array_compare_func *)cmp)(a, b);
}

void array_rsort(array_t *arr, array_compare_func *cmp) {
  qsort_r(arr->data, arr->length, arr->item_size, (void *)cmp, reverse_compare);
}
