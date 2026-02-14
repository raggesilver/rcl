
#include "array.h"
#include "unity.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void setUp(void) {}

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
  _res->free_func = free;

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

bool _filter_always_true(const void *_) { return true; }

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

  ARRAY_OF(char *) *_copy = (void *)copy;
  TEST_ASSERT_EQUAL_size_t(arr->length, _copy->length);
  TEST_ASSERT_EQUAL_PTR(arr->free_func, _copy->free_func);

  for (size_t i = 0; i < arr->length; i++) {
    TEST_ASSERT_EQUAL_STRING(((char **)arr->data)[i], _copy->data[i]);
  }

  array_destroy(&arr);
  array_destroy(&copy);
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

  return UNITY_END();
}
