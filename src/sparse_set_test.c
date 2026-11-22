#include "unity.h"
#include <rcl/sparse_set.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static int free_count = 0;

void setUp(void) { free_count = 0; }

void tearDown(void) {}

static void counting_free(void *data) {
  free_count++;
  free(data);
}

static int *new_int(int v) {
  int *p = malloc(sizeof(*p));
  *p = v;
  return p;
}

static void sparse_set_basic_usage(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);

  TEST_ASSERT_EQUAL(16, s->max_items);
  TEST_ASSERT_EQUAL(0, s->length);
  TEST_ASSERT_FALSE(sparse_set_has(s, 3));
  TEST_ASSERT_NULL(sparse_set_get(s, 3));

  int a = 10;
  sparse_set_add(s, 3, &a);

  TEST_ASSERT_EQUAL(1, s->length);
  TEST_ASSERT_TRUE(sparse_set_has(s, 3));
  TEST_ASSERT_EQUAL_PTR(&a, sparse_set_get(s, 3));
  TEST_ASSERT_FALSE(sparse_set_has(s, 4));

  sparse_set_destroy(&s);
  TEST_ASSERT_NULL(s);
}

static void sparse_set_default_capacity(void) {
  sparse_set_t *s = sparse_set_new();

  TEST_ASSERT_EQUAL(1024, s->max_items);
  TEST_ASSERT_NULL(s->free_func);

  sparse_set_add(s, 1023, NULL);
  TEST_ASSERT_TRUE(sparse_set_has(s, 1023));

  sparse_set_destroy(&s);
}

static void sparse_set_has_out_of_range(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 8);

  TEST_ASSERT_FALSE(sparse_set_has(s, 8));
  TEST_ASSERT_FALSE(sparse_set_has(s, SIZE_MAX));
  TEST_ASSERT_NULL(sparse_set_get(s, 8));
  TEST_ASSERT_FALSE(sparse_set_remove(s, 8));

  sparse_set_destroy(&s);
}

static void sparse_set_adds_multiple(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2, c = 3;

  sparse_set_add(s, 5, &a);
  sparse_set_add(s, 2, &b);
  sparse_set_add(s, 9, &c);

  TEST_ASSERT_EQUAL(3, s->length);
  TEST_ASSERT_EQUAL_PTR(&a, sparse_set_get(s, 5));
  TEST_ASSERT_EQUAL_PTR(&b, sparse_set_get(s, 2));
  TEST_ASSERT_EQUAL_PTR(&c, sparse_set_get(s, 9));
  TEST_ASSERT_FALSE(sparse_set_has(s, 3));

  // Dense storage is in insertion order
  TEST_ASSERT_EQUAL(5, s->dense[0].index);
  TEST_ASSERT_EQUAL(2, s->dense[1].index);
  TEST_ASSERT_EQUAL(9, s->dense[2].index);

  sparse_set_destroy(&s);
}

static void sparse_set_add_replaces(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2;

  sparse_set_add(s, 4, &a);
  sparse_set_add(s, 4, &b);

  TEST_ASSERT_EQUAL(1, s->length);
  TEST_ASSERT_EQUAL_PTR(&b, sparse_set_get(s, 4));

  sparse_set_destroy(&s);
}

static void sparse_set_removes_middle(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2, c = 3;

  sparse_set_add(s, 5, &a);
  sparse_set_add(s, 2, &b);
  sparse_set_add(s, 9, &c);

  TEST_ASSERT_TRUE(sparse_set_remove(s, 5));

  TEST_ASSERT_EQUAL(2, s->length);
  TEST_ASSERT_FALSE(sparse_set_has(s, 5));
  TEST_ASSERT_NULL(sparse_set_get(s, 5));
  // The moved item must still be reachable
  TEST_ASSERT_EQUAL_PTR(&c, sparse_set_get(s, 9));
  TEST_ASSERT_EQUAL_PTR(&b, sparse_set_get(s, 2));
  TEST_ASSERT_EQUAL(9, s->dense[0].index);

  sparse_set_destroy(&s);
}

static void sparse_set_removes_last(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2;

  sparse_set_add(s, 5, &a);
  sparse_set_add(s, 2, &b);

  TEST_ASSERT_TRUE(sparse_set_remove(s, 2));

  TEST_ASSERT_EQUAL(1, s->length);
  TEST_ASSERT_FALSE(sparse_set_has(s, 2));
  TEST_ASSERT_EQUAL_PTR(&a, sparse_set_get(s, 5));

  sparse_set_destroy(&s);
}

static void sparse_set_removes_only_item(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 4);
  int a = 1;

  sparse_set_add(s, 0, &a);
  TEST_ASSERT_TRUE(sparse_set_remove(s, 0));

  TEST_ASSERT_EQUAL(0, s->length);
  TEST_ASSERT_FALSE(sparse_set_has(s, 0));

  sparse_set_destroy(&s);
}

static void sparse_set_remove_missing(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1;

  TEST_ASSERT_FALSE(sparse_set_remove(s, 3));

  sparse_set_add(s, 5, &a);
  TEST_ASSERT_FALSE(sparse_set_remove(s, 3));
  TEST_ASSERT_TRUE(sparse_set_remove(s, 5));
  // Removing twice must fail the second time
  TEST_ASSERT_FALSE(sparse_set_remove(s, 5));

  sparse_set_destroy(&s);
}

static void sparse_set_readds_after_remove(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2, c = 3;

  sparse_set_add(s, 5, &a);
  sparse_set_add(s, 2, &b);
  sparse_set_remove(s, 5);
  sparse_set_add(s, 5, &c);

  TEST_ASSERT_EQUAL(2, s->length);
  TEST_ASSERT_EQUAL_PTR(&c, sparse_set_get(s, 5));
  TEST_ASSERT_EQUAL_PTR(&b, sparse_set_get(s, 2));

  sparse_set_destroy(&s);
}

static void sparse_set_fills_to_capacity(void) {
  const size_t n = 64;
  sparse_set_t *s = sparse_set_new(.max_items = n);
  int values[64];

  for (size_t i = 0; i < n; i++) {
    values[i] = (int)i;
    sparse_set_add(s, i, &values[i]);
  }

  TEST_ASSERT_EQUAL(n, s->length);

  for (size_t i = 0; i < n; i++) {
    TEST_ASSERT_EQUAL_PTR(&values[i], sparse_set_get(s, i));
  }

  sparse_set_destroy(&s);
}

static void sparse_set_removes_many(void) {
  const size_t n = 64;
  sparse_set_t *s = sparse_set_new(.max_items = n);
  int values[64];

  for (size_t i = 0; i < n; i++) {
    values[i] = (int)i;
    sparse_set_add(s, i, &values[i]);
  }

  // Remove every even id; odd ids must all survive the swaps
  for (size_t i = 0; i < n; i += 2) {
    TEST_ASSERT_TRUE(sparse_set_remove(s, i));
  }

  TEST_ASSERT_EQUAL(n / 2, s->length);

  for (size_t i = 0; i < n; i++) {
    if (i % 2 == 0) {
      TEST_ASSERT_FALSE(sparse_set_has(s, i));
    } else {
      TEST_ASSERT_EQUAL_PTR(&values[i], sparse_set_get(s, i));
    }
  }

  sparse_set_destroy(&s);
}

static void sparse_set_clears(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 16);
  int a = 1, b = 2;

  sparse_set_add(s, 5, &a);
  sparse_set_add(s, 2, &b);
  sparse_set_clear(s);

  TEST_ASSERT_EQUAL(0, s->length);
  TEST_ASSERT_FALSE(sparse_set_has(s, 5));
  TEST_ASSERT_FALSE(sparse_set_has(s, 2));

  // Still usable after clearing
  sparse_set_add(s, 2, &a);
  TEST_ASSERT_EQUAL_PTR(&a, sparse_set_get(s, 2));
  TEST_ASSERT_EQUAL(1, s->length);

  sparse_set_destroy(&s);
}

static void sparse_set_destroy_null_is_safe(void) {
  sparse_set_t *s = NULL;

  sparse_set_destroy(&s);
  TEST_ASSERT_NULL(s);
}

static void sparse_set_free_func_on_replace(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 8, .free_func = counting_free);

  sparse_set_add(s, 1, new_int(1));
  TEST_ASSERT_EQUAL(0, free_count);

  sparse_set_add(s, 1, new_int(2));
  TEST_ASSERT_EQUAL(1, free_count);
  TEST_ASSERT_EQUAL(2, *(int *)sparse_set_get(s, 1));

  sparse_set_destroy(&s);
  TEST_ASSERT_EQUAL(2, free_count);
}

static void sparse_set_free_func_skips_same_pointer(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 8, .free_func = counting_free);
  int *p = new_int(7);

  sparse_set_add(s, 1, p);
  sparse_set_add(s, 1, p);

  TEST_ASSERT_EQUAL(0, free_count);
  TEST_ASSERT_EQUAL_PTR(p, sparse_set_get(s, 1));

  sparse_set_destroy(&s);
  TEST_ASSERT_EQUAL(1, free_count);
}

static void sparse_set_free_func_on_remove(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 8, .free_func = counting_free);

  sparse_set_add(s, 1, new_int(1));
  sparse_set_add(s, 2, new_int(2));
  sparse_set_add(s, 3, new_int(3));

  sparse_set_remove(s, 1);
  TEST_ASSERT_EQUAL(1, free_count);

  // Removing a missing id must not free anything
  sparse_set_remove(s, 1);
  TEST_ASSERT_EQUAL(1, free_count);

  // Survivors are intact after the swap
  TEST_ASSERT_EQUAL(2, *(int *)sparse_set_get(s, 2));
  TEST_ASSERT_EQUAL(3, *(int *)sparse_set_get(s, 3));

  sparse_set_destroy(&s);
  TEST_ASSERT_EQUAL(3, free_count);
}

static void sparse_set_free_func_on_clear(void) {
  sparse_set_t *s = sparse_set_new(.max_items = 8, .free_func = counting_free);

  sparse_set_add(s, 1, new_int(1));
  sparse_set_add(s, 2, new_int(2));
  sparse_set_add(s, 3, new_int(3));

  sparse_set_clear(s);
  TEST_ASSERT_EQUAL(3, free_count);

  // A second clear must not double free
  sparse_set_clear(s);
  TEST_ASSERT_EQUAL(3, free_count);

  sparse_set_destroy(&s);
  TEST_ASSERT_EQUAL(3, free_count);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(sparse_set_basic_usage);
  RUN_TEST(sparse_set_default_capacity);
  RUN_TEST(sparse_set_has_out_of_range);
  RUN_TEST(sparse_set_adds_multiple);
  RUN_TEST(sparse_set_add_replaces);
  RUN_TEST(sparse_set_removes_middle);
  RUN_TEST(sparse_set_removes_last);
  RUN_TEST(sparse_set_removes_only_item);
  RUN_TEST(sparse_set_remove_missing);
  RUN_TEST(sparse_set_readds_after_remove);
  RUN_TEST(sparse_set_fills_to_capacity);
  RUN_TEST(sparse_set_removes_many);
  RUN_TEST(sparse_set_clears);
  RUN_TEST(sparse_set_destroy_null_is_safe);
  RUN_TEST(sparse_set_free_func_on_replace);
  RUN_TEST(sparse_set_free_func_skips_same_pointer);
  RUN_TEST(sparse_set_free_func_on_remove);
  RUN_TEST(sparse_set_free_func_on_clear);

  return UNITY_END();
}
