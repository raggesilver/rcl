#include <rcl/array.h>
#include <rcl/common.h>
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

array_t *array_filter(array_t *self, array_filter_func *fn) {
  for (size_t i = self->length; i > 0;) {
    --i;
    void *item = array_at(self, i);
    if (!fn(item)) {
      array_delete(self, i);
    }
  }
  return self;
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
    memmove(array_at(arr, index), array_at(arr, index + 1),
            (arr->length - index - 1) * arr->item_size);
  }

  arr->length--;
}

void array_remove(array_t *arr, size_t index, void *item_out) {
  // We must always set a value for out items. Even if validation fails.
  memset(item_out, 0, arr->item_size);

  if (index >= arr->length)
    return;

  memcpy(item_out, array_at(arr, index), arr->item_size);

  if (index < arr->length - 1) {
    // Move all the items after the deleted item one position to the left.
    memmove(array_at(arr, index), array_at(arr, index + 1),
            (arr->length - index - 1) * arr->item_size);
  }

  arr->length--;
}

bool array_bsearch(const array_t *arr, const void *key, array_compare_func *cmp,
                   size_t out index_out) {
  // Half-open window [low, high): the indices that may still hold the key.
  size_t low = 0, high = arr->length;

  while (low < high) {
    size_t mid = low + (high - low) / 2;
    const void *mid_item = array_at(arr, mid);
    int cmp_result = cmp(key, mid_item);
    if (cmp_result == 0) {
      set_out_value(index_out, mid);
      return true;
    } else if (cmp_result < 0) {
      high = mid;
    } else {
      low = mid + 1;
    }
  }

  set_out_value(index_out, 0);
  return false;
}

bool array_swap_delete(array_t *arr, size_t index) {
  if (index >= arr->length) {
    return false;
  }

  void *item = array_at(arr, index);

  if (arr->free_func != NULL) {
    if (arr->item_size != sizeof(void *)) {
      fprintf(
          stderr,
          "Warning: free function set, but item size is not sizeof(void *)\n");
    } else {
      // free_func takes the stored pointer, not the address of its slot.
      arr->free_func(((void **)arr->data)[index]);
    }
  }

  if (index < arr->length - 1) {
    memcpy(item, array_at(arr, arr->length - 1), arr->item_size);
  }

  arr->length--;
  return true;
}

__attribute__((nonnull(3))) bool array_swap_remove(array_t *arr, size_t index,
                                                   void *item_out) {
  if (index >= arr->length) {
    memset(item_out, 0, arr->item_size);
    return false;
  }

  memcpy(item_out, array_at(arr, index), arr->item_size);

  if (index < arr->length - 1) {
    memcpy(array_at(arr, index), array_at(arr, arr->length - 1),
           arr->item_size);
  }

  arr->length--;
  return true;
}
