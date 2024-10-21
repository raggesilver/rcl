
#include "array.h"
#include "unity.h"
#include <stdbool.h>

void setUp(void) {}

void tearDown(void) {}

static void array_basic_usage(void) {
  array_t *arr = array_new(int, );

  TEST_ASSERT_EQUAL(ARRAY_DEFAULT_CAPACITY, arr->capacity);
  TEST_ASSERT_EQUAL(0, arr->length);
  TEST_ASSERT_EQUAL(sizeof(int), arr->item_size);

  array_push(arr, 1);

  ARRAY_OF(int) *arr_ = (void *)arr;

  TEST_ASSERT_EQUAL(arr_->data[0], 1);

  array_destroy(&arr);
}

static void array_grows(void) {
  array_t *arr = array_new(int, );
  const int count = 1000;

  for (int i = 0; i < count; i++) {
    array_push(arr, i);
  }

  ARRAY_OF(int) *arr_ = (void *)arr;

  for (int i = 0; i < count; i++) {
    TEST_ASSERT_EQUAL(arr_->data[i], i);
  }

  array_destroy(&arr);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(array_basic_usage);
  RUN_TEST(array_grows);

  return UNITY_END();
}
