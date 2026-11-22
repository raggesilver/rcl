
#include "unity.h"
#include <rcl/array.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int free_count;

static void counting_free(void *ptr) {
  free_count++;
  free(ptr);
}

void setUp(void) { free_count = 0; }

void tearDown(void) {}

static void array_basic_usage(void) {
  array_t *arr = array_new(int, );

  TEST_ASSERT_EQUAL(arr->capacity, ARRAY_DEFAULT_CAPACITY);
  TEST_ASSERT_EQUAL(arr->length, 0);
  TEST_ASSERT_EQUAL(arr->item_size, sizeof(int));

  array_push(arr, 1);

  ARRAY_OF(int) *arr_ = (void *)arr;

  TEST_ASSERT_EQUAL(1, arr_->data[0]);

  array_destroy(&arr);
}

static void array_grows(void) {
  array_t *arr = array_new(int, );
  const int count = 1000;

  for (int i = 0; i < count; i++) {
    array_push(arr, i);
  }

  TEST_ASSERT_EQUAL(count, arr->length);

  ARRAY_OF(int) *arr_ = (void *)arr;

  for (int i = 0; i < count; i++) {
    TEST_ASSERT_EQUAL(i, arr_->data[i]);
  }

  array_destroy(&arr);
}

static void array_free_func_is_called(void) {
  array_t *arr = array_new(char *, .free_func = &free);

  array_push(arr, strdup("hello"));
  array_push(arr, strdup("world"));

  ARRAY_OF(char *) *str_arr = (void *)(arr);

  TEST_ASSERT_EQUAL(0, strcmp(str_arr->data[0], "hello"));
  TEST_ASSERT_EQUAL(0, strcmp(str_arr->data[1], "world"));

  array_destroy(&arr);
}

static int int_cmp(const void *a, const void *b) {
  return *(int *)a - *(int *)b;
}

static void array_sorts(void) {
  array_t *arr = array_new(int, );

  array_push(arr, 3);
  array_push(arr, 1);
  array_push(arr, 2);

  ARRAY_OF(int) *int_arr = (void *)(arr);

  TEST_ASSERT_EQUAL(3, int_arr->data[0]);
  TEST_ASSERT_EQUAL(1, int_arr->data[1]);
  TEST_ASSERT_EQUAL(2, int_arr->data[2]);

  array_sort(arr, int_cmp);

  TEST_ASSERT_EQUAL(1, int_arr->data[0]);
  TEST_ASSERT_EQUAL(2, int_arr->data[1]);
  TEST_ASSERT_EQUAL(3, int_arr->data[2]);

  array_destroy(&arr);
}

static void array_array_of(void) {
  array_t *arr = array_new(int, );

  ARRAY_OF(int) *int_arr = (void *)(arr);

  TEST_ASSERT_EQUAL(0, int_arr->length);

  array_push(arr, 1);

  TEST_ASSERT_EQUAL(1, int_arr->length);
  TEST_ASSERT_EQUAL(1, arr->length);

  int_arr->length++;

  TEST_ASSERT_EQUAL(2, int_arr->length);
  TEST_ASSERT_EQUAL(2, arr->length);

  TEST_ASSERT_EQUAL(arr->capacity, int_arr->capacity);
  TEST_ASSERT_EQUAL(arr->item_size, int_arr->item_size);
  TEST_ASSERT_EQUAL(arr->data, int_arr->data);

  array_destroy(&arr);
}

static void array_removes(void) {
  array_t *arr = array_new(int, );

  array_push(arr, 1);
  array_push(arr, 2);
  array_push(arr, 3);

  TEST_ASSERT_EQUAL(3, arr->length);

  int removed;
  array_remove(arr, 1, &removed);

  TEST_ASSERT_EQUAL(2, removed);

  array_destroy(&arr);
}

static void array_removes2(void) {
  array_t *arr = array_new(char *, .free_func = &free);

  array_push(arr, strdup("hello"));
  array_push(arr, strdup("world"));

  TEST_ASSERT_EQUAL(2, arr->length);

  char *removed;
  array_remove(arr, 0, &removed);

  TEST_ASSERT_EQUAL(0, strcmp(removed, "hello"));

  free(removed);
  array_destroy(&arr);
}

static void array_deletes(void) {
  array_t *arr = array_new(char *, .free_func = &free);

  array_push(arr, strdup("hello"));
  array_push(arr, strdup("world"));

  TEST_ASSERT_EQUAL(2, arr->length);

  array_delete(arr, 0);

  TEST_ASSERT_EQUAL(1, arr->length);
  TEST_ASSERT_EQUAL(0, strcmp(((char **)arr->data)[0], "world"));

  array_destroy(&arr);
}

static int map_double(int el) { return el * 2; }

static char *map_int_to_str(int el) {
  char *res;
  asprintf(&res, "%d", el);
  return res;
}

static void array_test_map(void) {
  array_t *arr = array_new(int, );

  array_push(arr, 1);
  array_push(arr, 2);
  array_push(arr, 3);

  array_t *_res = array_map(arr, int, int, map_double);

  ARRAY_OF(int) *res = (void *)_res;

  TEST_ASSERT_EQUAL(res->data[0], 2);
  TEST_ASSERT_EQUAL(res->data[1], 4);
  TEST_ASSERT_EQUAL(res->data[2], 6);

  TEST_ASSERT_EQUAL(res->length, arr->length);

  array_free(arr);
  array_free(_res);
}

static void array_test_map2(void) {
  array_t *arr = array_new(int, );

  array_push(arr, 204);
  array_push(arr, 303);
  array_push(arr, 402);

  array_t *_res = array_map(arr, int, char *, map_int_to_str);
  array_set_free_func(_res, free);

  ARRAY_OF(char *) *res = (void *)_res;

  TEST_ASSERT_EQUAL_STRING(res->data[0], "204");
  TEST_ASSERT_EQUAL_STRING(res->data[1], "303");
  TEST_ASSERT_EQUAL_STRING(res->data[2], "402");

  TEST_ASSERT_EQUAL(res->length, arr->length);

  array_free(arr);
  array_free(_res);
}

bool _string_matches_prefix(const void *ptr) {
  const char *str = *((const char **)ptr);
  return strncmp(str, "prefix ", 7) == 0;
}

static void array_test_filter(void) {
  array_t *arr = array_new(char *);

  array_push(arr, "prefix foo");
  array_push(arr, "no match");
  array_push(arr, "prefix bar");

  array_filter(arr, &_string_matches_prefix);

  ARRAY_OF(char *) *_arr = (void *)arr;
  TEST_ASSERT_EQUAL_size_t(2, _arr->length);
  TEST_ASSERT_EQUAL_STRING("prefix foo", _arr->data[0]);
  TEST_ASSERT_EQUAL_STRING("prefix bar", _arr->data[1]);

  array_destroy(&arr);
}

bool _filter_always_true(const void *_) {
  (void)_;
  return true;
}

int copy_int(int el) { return el; }

static void array_test_filter_copy(void) {
  array_t *arr = array_new(int);

  array_push(arr, 20);
  array_push(arr, 200);
  array_push(arr, 2000);
  array_push(arr, 20000);

  array_t *copy = array_filter_copy(arr, int, _filter_always_true, copy_int);

  ARRAY_OF(int) *_copy = (void *)copy;
  TEST_ASSERT_EQUAL_size_t(arr->length, _copy->length);
  for (size_t i = 0; i < arr->length; i++) {
    TEST_ASSERT_EQUAL(((int *)arr->data)[i], _copy->data[i]);
  }
  TEST_ASSERT_EQUAL_PTR(arr->free_func, _copy->free_func);

  array_destroy(&arr);
  array_destroy(&copy);
}

void *copy_str_ptr(void *ptr) { return ptr; }

static void array_test_filter_copy2(void) {
  array_t *arr = array_new(char *, .free_func = &free);

  array_push(arr, strdup("keep me"));
  array_push(arr, strdup("don't discard me"));
  array_push(arr, strdup("also keep me"));

  array_t *copy = array_filter_copy(arr, char *, _filter_always_true, strdup);

  ARRAY_OF(char *) *_arr = (void *)arr;
  ARRAY_OF(char *) *_copy = (void *)copy;

  TEST_ASSERT_EQUAL_size_t(arr->length, _copy->length);
  TEST_ASSERT_EQUAL_PTR(arr->free_func, _copy->free_func);

  for (size_t i = 0; i < arr->length; i++) {
    TEST_ASSERT_EQUAL_STRING(_arr->data[i], _copy->data[i]);
  }

  array_destroy(&arr);
  array_destroy(&copy);
}

/* ---- helpers for the sort / bsearch / swap-remove / array_at tests ---- */

typedef struct {
  int layer;
  int id;
} entry_t;

// Overflow-safe, unlike `*a - *b`.
static int safe_int_cmp(const void *a, const void *b) {
  int x = *(const int *)a, y = *(const int *)b;
  return (x > y) - (x < y);
}

static int entry_cmp(const void *a, const void *b) {
  return safe_int_cmp(&((const entry_t *)a)->layer,
                      &((const entry_t *)b)->layer);
}

static int str_cmp(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

static array_t *int_array_of(const int *values, size_t n) {
  array_t *arr = array_new(int, );
  for (size_t i = 0; i < n; i++) {
    array_push(arr, values[i]);
  }
  return arr;
}

/* ---- array_at ---- */

static void array_at_addresses_slots(void) {
  const int values[] = {10, 20, 30};
  array_t *arr = int_array_of(values, 3);

  for (size_t i = 0; i < 3; i++) {
    TEST_ASSERT_EQUAL_PTR((int *)arr->data + i, array_at(arr, i));
    TEST_ASSERT_EQUAL_INT(values[i], *(int *)array_at(arr, i));
  }

  *(int *)array_at(arr, 1) = 99;
  TEST_ASSERT_EQUAL_INT(99, ((int *)arr->data)[1]);

  array_destroy(&arr);
}

static void array_at_uses_item_size(void) {
  array_t *arr = array_new(entry_t, );
  array_push(arr, ((entry_t){.layer = 1, .id = 100}));
  array_push(arr, ((entry_t){.layer = 2, .id = 200}));

  entry_t *second = array_at(arr, 1);
  TEST_ASSERT_EQUAL_PTR((entry_t *)arr->data + 1, second);
  TEST_ASSERT_EQUAL_INT(2, second->layer);
  TEST_ASSERT_EQUAL_INT(200, second->id);

  array_destroy(&arr);
}

/* ---- array_sort ---- */

static void array_sort_edge_cases(void) {
  array_t *empty = array_new(int, );
  array_sort(empty, safe_int_cmp);
  TEST_ASSERT_EQUAL_size_t(0, empty->length);
  array_destroy(&empty);

  const int one[] = {7};
  array_t *single = int_array_of(one, 1);
  array_sort(single, safe_int_cmp);
  TEST_ASSERT_EQUAL_INT(7, ((int *)single->data)[0]);
  array_destroy(&single);

  const int dupes[] = {5, 1, 5, INT_MIN, 3, 1, INT_MAX, 5};
  const int expected[] = {INT_MIN, 1, 1, 3, 5, 5, 5, INT_MAX};
  array_t *arr = int_array_of(dupes, 8);
  array_sort(arr, safe_int_cmp);
  for (size_t i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_INT(expected[i], ((int *)arr->data)[i]);
  }
  array_destroy(&arr);
}

static void array_sort_structs_by_layer(void) {
  array_t *arr = array_new(entry_t, );
  const int layers[] = {3, 1, 2, 0};
  for (int i = 0; i < 4; i++) {
    array_push(arr, ((entry_t){.layer = layers[i], .id = i}));
  }

  array_sort(arr, entry_cmp);

  for (int i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL_INT(i, ((entry_t *)arr->data)[i].layer);
  }
  // The id travels with its layer.
  TEST_ASSERT_EQUAL_INT(3, ((entry_t *)arr->data)[0].id);
  TEST_ASSERT_EQUAL_INT(0, ((entry_t *)arr->data)[3].id);

  array_destroy(&arr);
}

/* ---- array_bsearch ---- */

static void array_bsearch_empty(void) {
  array_t *arr = array_new(int, );
  int key = 1;
  size_t index = 99;

  TEST_ASSERT_FALSE(array_bsearch(arr, &key, safe_int_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(0, index);

  array_destroy(&arr);
}

static void array_bsearch_single_element(void) {
  const int values[] = {5};
  array_t *arr = int_array_of(values, 1);
  size_t index = 99;

  int hit = 5;
  TEST_ASSERT_TRUE(array_bsearch(arr, &hit, safe_int_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(0, index);

  int below = 4, above = 6;
  index = 99;
  TEST_ASSERT_FALSE(array_bsearch(arr, &below, safe_int_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(0, index);
  index = 99;
  TEST_ASSERT_FALSE(array_bsearch(arr, &above, safe_int_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(0, index);

  array_destroy(&arr);
}

// For every length 0..20: every present value is found at its own index, and
// every gap (including below the first and above the last) is a miss.
static void array_bsearch_every_position_and_gap(void) {
  for (size_t n = 0; n <= 20; n++) {
    array_t *arr = array_new(int, );
    for (size_t i = 0; i < n; i++) {
      array_push(arr, (int)(i * 2 + 2)); // 2, 4, 6, ...
    }

    for (size_t i = 0; i < n; i++) {
      int key = (int)(i * 2 + 2);
      size_t index = 99;
      TEST_ASSERT_TRUE(array_bsearch(arr, &key, safe_int_cmp, &index));
      TEST_ASSERT_EQUAL_size_t(i, index);
    }

    for (int key = 1; key <= (int)(n * 2 + 1); key += 2) { // odd = absent
      size_t index = 99;
      TEST_ASSERT_FALSE(array_bsearch(arr, &key, safe_int_cmp, &index));
      TEST_ASSERT_EQUAL_size_t(0, index);
    }

    array_destroy(&arr);
  }
}

static void array_bsearch_null_index_out(void) {
  const int values[] = {1, 2, 3};
  array_t *arr = int_array_of(values, 3);

  int hit = 2, miss = 9;
  TEST_ASSERT_TRUE(array_bsearch(arr, &hit, safe_int_cmp, NULL));
  TEST_ASSERT_FALSE(array_bsearch(arr, &miss, safe_int_cmp, NULL));

  array_destroy(&arr);
}

static void array_bsearch_duplicates_returns_a_match(void) {
  const int values[] = {1, 2, 2, 2, 3};
  array_t *arr = int_array_of(values, 5);

  int key = 2;
  size_t index = 99;
  TEST_ASSERT_TRUE(array_bsearch(arr, &key, safe_int_cmp, &index));
  TEST_ASSERT_TRUE(index >= 1 && index <= 3);

  array_destroy(&arr);
}

static void array_bsearch_structs_and_strings(void) {
  array_t *entries = array_new(entry_t, );
  for (int i = 0; i < 5; i++) {
    array_push(entries, ((entry_t){.layer = i * 10, .id = i}));
  }
  entry_t key = {.layer = 30};
  size_t index = 99;
  TEST_ASSERT_TRUE(array_bsearch(entries, &key, entry_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(3, index);
  array_destroy(&entries);

  // For `char *` elements both the key and the elements are `char **`.
  array_t *strs = array_new(char *);
  array_push(strs, "apple");
  array_push(strs, "banana");
  array_push(strs, "cherry");
  char *needle = "banana";
  TEST_ASSERT_TRUE(array_bsearch(strs, &needle, str_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(1, index);
  needle = "blueberry";
  TEST_ASSERT_FALSE(array_bsearch(strs, &needle, str_cmp, &index));
  TEST_ASSERT_EQUAL_size_t(0, index);
  array_destroy(&strs);
}

/* ---- array_swap_delete ---- */

static void array_swap_delete_middle_moves_last_in(void) {
  const int values[] = {1, 2, 3, 4};
  array_t *arr = int_array_of(values, 4);

  TEST_ASSERT_TRUE(array_swap_delete(arr, 1));

  TEST_ASSERT_EQUAL_size_t(3, arr->length);
  TEST_ASSERT_EQUAL_INT(1, ((int *)arr->data)[0]);
  TEST_ASSERT_EQUAL_INT(4, ((int *)arr->data)[1]);
  TEST_ASSERT_EQUAL_INT(3, ((int *)arr->data)[2]);

  array_destroy(&arr);
}

static void array_swap_delete_first_last_and_only(void) {
  const int values[] = {1, 2, 3};
  array_t *arr = int_array_of(values, 3);

  TEST_ASSERT_TRUE(array_swap_delete(arr, 0)); // {3, 2}
  TEST_ASSERT_EQUAL_INT(3, ((int *)arr->data)[0]);
  TEST_ASSERT_EQUAL_INT(2, ((int *)arr->data)[1]);

  TEST_ASSERT_TRUE(array_swap_delete(arr, 1)); // last: just dropped
  TEST_ASSERT_EQUAL_size_t(1, arr->length);
  TEST_ASSERT_EQUAL_INT(3, ((int *)arr->data)[0]);

  TEST_ASSERT_TRUE(array_swap_delete(arr, 0)); // only element
  TEST_ASSERT_EQUAL_size_t(0, arr->length);

  array_destroy(&arr);
}

static void array_swap_delete_out_of_bounds(void) {
  array_t *empty = array_new(int, );
  TEST_ASSERT_FALSE(array_swap_delete(empty, 0));
  TEST_ASSERT_EQUAL_size_t(0, empty->length);
  array_destroy(&empty);

  const int values[] = {1, 2};
  array_t *arr = int_array_of(values, 2);
  TEST_ASSERT_FALSE(array_swap_delete(arr, 2));
  TEST_ASSERT_FALSE(array_swap_delete(arr, SIZE_MAX));
  TEST_ASSERT_EQUAL_size_t(2, arr->length);
  TEST_ASSERT_EQUAL_INT(1, ((int *)arr->data)[0]);
  TEST_ASSERT_EQUAL_INT(2, ((int *)arr->data)[1]);
  array_destroy(&arr);
}

static void array_swap_delete_calls_free_func_on_the_element(void) {
  array_t *arr = array_new(char *, .free_func = &counting_free);
  array_push(arr, strdup("a"));
  array_push(arr, strdup("b"));
  array_push(arr, strdup("c"));

  TEST_ASSERT_TRUE(array_swap_delete(arr, 0));

  TEST_ASSERT_EQUAL_INT(1, free_count);
  TEST_ASSERT_EQUAL_size_t(2, arr->length);
  TEST_ASSERT_EQUAL_STRING("c", ((char **)arr->data)[0]);
  TEST_ASSERT_EQUAL_STRING("b", ((char **)arr->data)[1]);

  // A failed delete must not free anything.
  TEST_ASSERT_FALSE(array_swap_delete(arr, 5));
  TEST_ASSERT_EQUAL_INT(1, free_count);

  array_destroy(&arr);
  TEST_ASSERT_EQUAL_INT(3, free_count);
}

static void array_swap_delete_last_with_free_func(void) {
  array_t *arr = array_new(char *, .free_func = &counting_free);
  array_push(arr, strdup("a"));
  array_push(arr, strdup("b"));

  TEST_ASSERT_TRUE(array_swap_delete(arr, 1));
  TEST_ASSERT_EQUAL_INT(1, free_count);
  TEST_ASSERT_EQUAL_STRING("a", ((char **)arr->data)[0]);

  array_destroy(&arr);
  TEST_ASSERT_EQUAL_INT(2, free_count);
}

/* ---- array_swap_remove ---- */

static void array_swap_remove_middle_moves_last_in(void) {
  const int values[] = {1, 2, 3, 4};
  array_t *arr = int_array_of(values, 4);

  int removed = -1;
  TEST_ASSERT_TRUE(array_swap_remove(arr, 1, &removed));

  TEST_ASSERT_EQUAL_INT(2, removed);
  TEST_ASSERT_EQUAL_size_t(3, arr->length);
  TEST_ASSERT_EQUAL_INT(1, ((int *)arr->data)[0]);
  TEST_ASSERT_EQUAL_INT(4, ((int *)arr->data)[1]);
  TEST_ASSERT_EQUAL_INT(3, ((int *)arr->data)[2]);

  array_destroy(&arr);
}

static void array_swap_remove_first_last_and_only(void) {
  const int values[] = {1, 2, 3};
  array_t *arr = int_array_of(values, 3);
  int removed = -1;

  TEST_ASSERT_TRUE(array_swap_remove(arr, 0, &removed)); // {3, 2}
  TEST_ASSERT_EQUAL_INT(1, removed);
  TEST_ASSERT_EQUAL_INT(3, ((int *)arr->data)[0]);

  TEST_ASSERT_TRUE(array_swap_remove(arr, 1, &removed)); // last
  TEST_ASSERT_EQUAL_INT(2, removed);
  TEST_ASSERT_EQUAL_size_t(1, arr->length);

  TEST_ASSERT_TRUE(array_swap_remove(arr, 0, &removed)); // only element
  TEST_ASSERT_EQUAL_INT(3, removed);
  TEST_ASSERT_EQUAL_size_t(0, arr->length);

  array_destroy(&arr);
}

static void array_swap_remove_out_of_bounds_zeroes_out(void) {
  const int values[] = {1, 2};
  array_t *arr = int_array_of(values, 2);

  int removed = 1234;
  TEST_ASSERT_FALSE(array_swap_remove(arr, 2, &removed));
  TEST_ASSERT_EQUAL_INT(0, removed);
  TEST_ASSERT_EQUAL_size_t(2, arr->length);

  array_t *empty = array_new(int, );
  removed = 1234;
  TEST_ASSERT_FALSE(array_swap_remove(empty, 0, &removed));
  TEST_ASSERT_EQUAL_INT(0, removed);

  array_destroy(&empty);
  array_destroy(&arr);
}

static void array_swap_remove_does_not_free(void) {
  array_t *arr = array_new(char *, .free_func = &counting_free);
  array_push(arr, strdup("a"));
  array_push(arr, strdup("b"));

  char *removed = NULL;
  TEST_ASSERT_TRUE(array_swap_remove(arr, 0, &removed));
  TEST_ASSERT_EQUAL_INT(0, free_count);
  TEST_ASSERT_EQUAL_STRING("a", removed);
  TEST_ASSERT_EQUAL_STRING("b", ((char **)arr->data)[0]);

  free(removed); // ownership moved to the caller
  array_destroy(&arr);
  TEST_ASSERT_EQUAL_INT(1, free_count);
}

static void array_swap_remove_structs(void) {
  array_t *arr = array_new(entry_t, );
  for (int i = 0; i < 3; i++) {
    array_push(arr, ((entry_t){.layer = i, .id = i * 100}));
  }

  entry_t removed = {.layer = -1, .id = -1};
  TEST_ASSERT_TRUE(array_swap_remove(arr, 0, &removed));
  TEST_ASSERT_EQUAL_INT(0, removed.layer);
  TEST_ASSERT_EQUAL_INT(0, removed.id);
  TEST_ASSERT_EQUAL_INT(2, ((entry_t *)arr->data)[0].layer);
  TEST_ASSERT_EQUAL_INT(200, ((entry_t *)arr->data)[0].id);

  array_destroy(&arr);
}

/* ---- array_set_free_func ---- */

static void array_new_full_sets_free_func(void) {
  array_t *arr = array_new(char *, .free_func = &counting_free);
  TEST_ASSERT_EQUAL_PTR(counting_free, arr->free_func);

  array_t *plain = array_new(char *);
  TEST_ASSERT_NULL(plain->free_func);

  array_destroy(&plain);
  array_destroy(&arr);
}

static void array_set_free_func_applies_to_existing_elements(void) {
  array_t *arr = array_new(char *);
  array_push(arr, strdup("a"));
  array_push(arr, strdup("b"));

  array_set_free_func(arr, counting_free);
  TEST_ASSERT_EQUAL_PTR(counting_free, arr->free_func);
  TEST_ASSERT_EQUAL_INT(0, free_count); // setting frees nothing

  array_destroy(&arr);
  TEST_ASSERT_EQUAL_INT(2, free_count);
}

static void array_set_free_func_replaces_and_clears(void) {
  array_t *arr = array_new(char *, .free_func = &counting_free);
  array_push(arr, strdup("a"));

  array_set_free_func(arr, NULL);
  TEST_ASSERT_NULL(arr->free_func);

  // Cleared: deleting no longer frees, so the caller owns the string.
  char *removed = NULL;
  array_remove(arr, 0, &removed);
  TEST_ASSERT_EQUAL_INT(0, free_count);
  free(removed);

  array_push(arr, strdup("b"));
  array_set_free_func(arr, counting_free);
  array_delete(arr, 0);
  TEST_ASSERT_EQUAL_INT(1, free_count);

  array_destroy(&arr);
}

static void array_set_free_func_null_is_allowed_for_any_item_size(void) {
  array_t *arr = array_new(entry_t, );
  array_set_free_func(arr, NULL);
  TEST_ASSERT_NULL(arr->free_func);
  array_destroy(&arr);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(array_basic_usage);
  RUN_TEST(array_grows);
  RUN_TEST(array_free_func_is_called);
  RUN_TEST(array_sorts);
  RUN_TEST(array_array_of);
  RUN_TEST(array_removes);
  RUN_TEST(array_removes2);
  RUN_TEST(array_deletes);
  RUN_TEST(array_test_map);
  RUN_TEST(array_test_map2);
  RUN_TEST(array_test_filter);
  RUN_TEST(array_test_filter_copy);
  RUN_TEST(array_test_filter_copy2);

  RUN_TEST(array_at_addresses_slots);
  RUN_TEST(array_at_uses_item_size);
  RUN_TEST(array_sort_edge_cases);
  RUN_TEST(array_sort_structs_by_layer);
  RUN_TEST(array_bsearch_empty);
  RUN_TEST(array_bsearch_single_element);
  RUN_TEST(array_bsearch_every_position_and_gap);
  RUN_TEST(array_bsearch_null_index_out);
  RUN_TEST(array_bsearch_duplicates_returns_a_match);
  RUN_TEST(array_bsearch_structs_and_strings);
  RUN_TEST(array_swap_delete_middle_moves_last_in);
  RUN_TEST(array_swap_delete_first_last_and_only);
  RUN_TEST(array_swap_delete_out_of_bounds);
  RUN_TEST(array_swap_delete_calls_free_func_on_the_element);
  RUN_TEST(array_swap_delete_last_with_free_func);
  RUN_TEST(array_swap_remove_middle_moves_last_in);
  RUN_TEST(array_swap_remove_first_last_and_only);
  RUN_TEST(array_swap_remove_out_of_bounds_zeroes_out);
  RUN_TEST(array_swap_remove_does_not_free);
  RUN_TEST(array_swap_remove_structs);
  RUN_TEST(array_new_full_sets_free_func);
  RUN_TEST(array_set_free_func_applies_to_existing_elements);
  RUN_TEST(array_set_free_func_replaces_and_clears);
  RUN_TEST(array_set_free_func_null_is_allowed_for_any_item_size);

  return UNITY_END();
}
