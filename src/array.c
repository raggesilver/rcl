#include "array.h"
#include <stdlib.h>

array_t *array_new_full(array_init_t init) {
  array_t *array = malloc(sizeof(array_t));

  *array = (array_t){
      .data = malloc(init.capacity * init.item_size),
      .capacity = init.capacity,
      .length = 0,
      .item_size = init.item_size,
  };

  return array;
}

void array_free(array_t *arr) {
  if (!arr) return;
  // TODO: check if arr contains a free function for items
  free(arr->data);
  free(arr);
}

__attribute__((always_inline)) inline void array_destroy(array_t **arr) {
  if (!arr || !*arr) return;

  array_free(*arr);
  *arr = NULL;
}
