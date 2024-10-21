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
    for (size_t i = 0; i < arr->length; i++) {
      void *item = ((char *)arr->data) + i * arr->item_size;
      dprintf(2, "freeing item %zu, %20s, offset: %zu, %p, %p\n", i,
              ((char **)arr->data)[i], i * arr->item_size, item,
              ((void **)arr->data)[i]);
      // We don't know the type of the item, so we need to calculate the
      // address of the item using pointer arithmetic. We use the item_size
      // to calculate the offset of the item from the start of the data array.
      arr->free_func(item);
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
