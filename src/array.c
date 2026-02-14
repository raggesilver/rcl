#include "array.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      for (size_t i = 0; i < arr->length; i++) {
        // We don't know the type of the item, so we need to calculate the
        // address of the item using pointer arithmetic. We use the item_size
        // to calculate the offset of the item from the start of the data array.
        arr->free_func(_arr->data[i]);
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

void array_delete(array_t *arr, size_t index) {
  if (index >= arr->length)
    return;

  if (arr->free_func != NULL) {
    if (arr->item_size != sizeof(void *)) {
      fprintf(
          stderr,
          "Warning: free function set, but item size is not sizeof(void *)\n");
    } else {
      ARRAY_OF(void *) *_arr = (void *)arr;
      // We don't know the type of the item, so we need to calculate the address
      // of the item using pointer arithmetic. We use the item_size to calculate
      // the offset of the item from the start of the data array.
      arr->free_func(_arr->data[index]);
    }
  }

  if (index < arr->length - 1) {
    // Move all the items after the deleted item one position to the left.
    memmove((char *)arr->data + index * arr->item_size,
            (char *)arr->data + (index + 1) * arr->item_size,
            (arr->length - index - 1) * arr->item_size);
  }

  arr->length--;
}

void array_remove(array_t *arr, size_t index, void *item_out) {
  // We must always set a value for out items. Even if validation fails.
  memset(item_out, 0, arr->item_size);

  if (index >= arr->length)
    return;

  memcpy(item_out, (char *)arr->data + index * arr->item_size, arr->item_size);

  if (index < arr->length - 1) {
    // Move all the items after the deleted item one position to the left.
    memmove((char *)arr->data + index * arr->item_size,
            (char *)arr->data + (index + 1) * arr->item_size,
            (arr->length - index - 1) * arr->item_size);
  }

  arr->length--;
}
