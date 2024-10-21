
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

  array_rsort(arr, int_cmp);

  TEST_ASSERT_EQUAL(3, int_arr->data[0]);
  TEST_ASSERT_EQUAL(2, int_arr->data[1]);
  TEST_ASSERT_EQUAL(1, int_arr->data[2]);

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

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(array_basic_usage);
  RUN_TEST(array_grows);
  RUN_TEST(array_free_func_is_called);
  RUN_TEST(array_sorts);
  RUN_TEST(array_array_of);

  return UNITY_END();
}
