#include "hashtable.h"
#include "unity.h"
#include <stdbool.h>
#include <stdlib.h>

// Force loads of collisions and resizes
// #define HASHTABLE_DEFAULT_CAPACITY 2

void setUp(void) {}

void tearDown(void) {}

static void test_hashtable_with_numbers(void) {
  hashtable_t *table = hashtable_new();

  int num = 42;
  hashtable_set(table, "num", &num);
  int *val = hashtable_get(table, "num");
  TEST_ASSERT_EQUAL_INT(num, *val);

  hashtable_free(table);
}

static void test_hashtable_delete(void) {
  hashtable_t *table = hashtable_new();

  int num = 42;
  hashtable_set(table, "num", &num);
  int *val = hashtable_get(table, "num");
  TEST_ASSERT_EQUAL_INT(num, *val);

  bool deleted = hashtable_delete(table, "num");
  TEST_ASSERT_TRUE(deleted);

  val = hashtable_get(table, "num");
  TEST_ASSERT_NULL(val);

  hashtable_free(table);
}

static void test_hashtable_remove(void) {
  hashtable_t *table = hashtable_new();

  int num = 42;
  hashtable_set(table, "num", &num);
  int *val = hashtable_get(table, "num");
  TEST_ASSERT_EQUAL_INT(num, *val);

  void *removed;
  bool deleted = hashtable_remove(table, "num", &removed);
  TEST_ASSERT_TRUE(deleted);
  TEST_ASSERT_EQUAL_INT(num, *(int *)removed);

  val = hashtable_get(table, "num");
  TEST_ASSERT_NULL(val);

  hashtable_free(table);
}

static void test_hashtable_exists(void) {
  hashtable_t *table = hashtable_new();

  int num = 42;
  hashtable_set(table, "num", &num);

  TEST_ASSERT_TRUE(hashtable_exists(table, "num"));
  TEST_ASSERT_FALSE(hashtable_exists(table, "num1"));

  hashtable_set(table, "num1", &num);
  hashtable_set(table, "num2", &num);
  hashtable_set(table, "num3", &num);
  hashtable_set(table, "num4", &num);
  hashtable_set(table, "num5", &num);

  TEST_ASSERT_TRUE(hashtable_exists(table, "num1"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "num2"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "num3"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "num4"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "num5"));
  TEST_ASSERT_FALSE(hashtable_exists(table, "num6"));

  hashtable_free(table);
}

static void test_hashtable_foreach(void) {
  hashtable_t *table = hashtable_new();

  int num = 42;
  hashtable_set(table, "num1", &num);
  hashtable_set(table, "num2", &num);
  hashtable_set(table, "num3", &num);
  hashtable_set(table, "num4", &num);
  hashtable_set(table, "num5", &num);
  hashtable_set(table, "num6", &num);

  hashtable_foreach(table, {
    int *val = (int *)value;
    TEST_ASSERT_EQUAL_INT(num, *val);
  });

  hashtable_free(table);
}

static void test_hashtable_dump(void) {
  {
    hashtable_t *suspicious_gifts = hashtable_new();

    int num = 42;
    int nnum = -42;
    hashtable_set(suspicious_gifts, "num1", &num);
    hashtable_set(suspicious_gifts, "num2", &nnum);
    hashtable_set(suspicious_gifts, "num3", NULL);

    /* hashtable_empty(suspicious_gifts); */

    hashtable_free(suspicious_gifts);
  }

  {
    hashtable_t *table = hashtable_new();

    bool t = true;
    bool f = false;
    hashtable_set(table, "awesome", &t);
    hashtable_set(table, "awful", &f);

    hashtable_free(table);
  }

  {
    hashtable_t *table = hashtable_new();

    hashtable_set(table, "name", "Paulo");
    hashtable_set(table, "last name", "Queiroz");
    hashtable_set(table, "age", "24");
    hashtable_set(table, "memory leaks", NULL);

    hashtable_free(table);
  }
}

static void test_hashtable_deletion_preserves_probe_chain(void) {
  hashtable_t *table = hashtable_new_with_capacity(8);

  // Insert items that will likely collide
  hashtable_set(table, "key_a", "value_a");
  hashtable_set(table, "key_b", "value_b");
  hashtable_set(table, "key_c", "value_c");
  hashtable_set(table, "key_d", "value_d");

  // Delete some items to create tombstones
  hashtable_delete(table, "key_b");

  // Items after the deleted one should still be findable
  TEST_ASSERT_TRUE(hashtable_exists(table, "key_c"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "key_d"));
  TEST_ASSERT_EQUAL_STRING("value_c", hashtable_get(table, "key_c"));
  TEST_ASSERT_EQUAL_STRING("value_d", hashtable_get(table, "key_d"));

  hashtable_free(table);
}

static void test_hashtable_tombstone_reuse(void) {
  hashtable_t *table = hashtable_new();

  hashtable_set(table, "reuse", "first");
  TEST_ASSERT_EQUAL(1, table->length);

  hashtable_delete(table, "reuse");
  TEST_ASSERT_EQUAL(0, table->length);
  TEST_ASSERT_FALSE(hashtable_exists(table, "reuse"));

  hashtable_set(table, "reuse", "second");
  TEST_ASSERT_EQUAL(1, table->length);
  TEST_ASSERT_TRUE(hashtable_exists(table, "reuse"));
  TEST_ASSERT_EQUAL_STRING("second", hashtable_get(table, "reuse"));

  hashtable_free(table);
}

static void test_hashtable_update_existing_key(void) {
  hashtable_t *table = hashtable_new();

  hashtable_set(table, "key", "value1");
  TEST_ASSERT_EQUAL(1, table->length);

  hashtable_set(table, "key", "value2");
  TEST_ASSERT_EQUAL(1, table->length);
  TEST_ASSERT_EQUAL_STRING("value2", hashtable_get(table, "key"));

  hashtable_free(table);
}

static void test_hashtable_resize_with_tombstones(void) {
  hashtable_t *table = hashtable_new_with_capacity(4);

  hashtable_set(table, "a", "1");
  hashtable_set(table, "b", "2");
  hashtable_set(table, "c", "3");

  hashtable_delete(table, "b");
  TEST_ASSERT_EQUAL(2, table->length);

  // Force resize by adding more items
  hashtable_set(table, "d", "4");
  hashtable_set(table, "e", "5");

  // After resize, all items should still be accessible
  TEST_ASSERT_TRUE(hashtable_exists(table, "a"));
  TEST_ASSERT_FALSE(hashtable_exists(table, "b"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "c"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "d"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "e"));
  TEST_ASSERT_EQUAL(4, table->length);

  hashtable_free(table);
}

static void test_hashtable_delete_nonexistent(void) {
  hashtable_t *table = hashtable_new();

  bool deleted = hashtable_delete(table, "nonexistent");
  TEST_ASSERT_FALSE(deleted);
  TEST_ASSERT_EQUAL(0, table->length);

  hashtable_free(table);
}

static void test_hashtable_remove_after_delete(void) {
  hashtable_t *table = hashtable_new();

  hashtable_set(table, "key1", "value1");
  hashtable_set(table, "key2", "value2");

  hashtable_delete(table, "key1");

  void *removed;
  bool success = hashtable_remove(table, "key2", &removed);
  TEST_ASSERT_TRUE(success);
  TEST_ASSERT_EQUAL_STRING("value2", (char *)removed);
  TEST_ASSERT_FALSE(hashtable_exists(table, "key2"));

  hashtable_free(table);
}

static int free_count = 0;

static void counting_free(void *ptr) {
  free_count++;
  free(ptr);
}

static void test_hashtable_delete_calls_free_func(void) {
  hashtable_t *table = hashtable_new();
  hashtable_set_free_func(table, counting_free);

  free_count = 0;

  int *val1 = malloc(sizeof(int));
  int *val2 = malloc(sizeof(int));
  *val1 = 42;
  *val2 = 99;

  hashtable_set(table, "key1", val1);
  hashtable_set(table, "key2", val2);

  hashtable_delete(table, "key1");
  TEST_ASSERT_EQUAL(1, free_count);

  hashtable_free(table);
  TEST_ASSERT_EQUAL(2, free_count);
}

static void test_hashtable_multiple_delete_reinsert(void) {
  hashtable_t *table = hashtable_new_with_capacity(8);

  hashtable_set(table, "x", "1");
  hashtable_set(table, "y", "2");
  hashtable_set(table, "z", "3");

  hashtable_delete(table, "x");
  hashtable_delete(table, "y");

  hashtable_set(table, "x", "4");
  hashtable_set(table, "y", "5");

  TEST_ASSERT_EQUAL(3, table->length);
  TEST_ASSERT_EQUAL_STRING("4", hashtable_get(table, "x"));
  TEST_ASSERT_EQUAL_STRING("5", hashtable_get(table, "y"));
  TEST_ASSERT_EQUAL_STRING("3", hashtable_get(table, "z"));

  hashtable_free(table);
}

static void test_hashtable_tombstone_saturation(void) {
  // With capacity 4, we can have at most 2 active items before grow triggers.
  // Fill slots with tombstones so no NULLs remain, then query a missing key.
  // Before the for-loop fix, this would infinite loop.
  hashtable_t *table = hashtable_new_with_capacity(4);

  hashtable_set(table, "a", "1");
  hashtable_set(table, "b", "2");

  hashtable_delete(table, "a");
  hashtable_delete(table, "b");
  // 2 tombstones, 2 NULLs, length=0

  hashtable_set(table, "c", "3");
  hashtable_set(table, "d", "4");
  // Worst case: c and d land in the NULL slots, leaving 2 tombstones + 2 active

  TEST_ASSERT_FALSE(hashtable_exists(table, "nonexistent"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "c"));
  TEST_ASSERT_TRUE(hashtable_exists(table, "d"));

  hashtable_free(table);
}

static void test_hashtable_foreach_skips_tombstones(void) {
  hashtable_t *table = hashtable_new();

  hashtable_set(table, "a", "1");
  hashtable_set(table, "b", "2");
  hashtable_set(table, "c", "3");

  hashtable_delete(table, "b");

  int count = 0;
  hashtable_foreach(table, {
    // key should never be a tombstone or NULL
    TEST_ASSERT_NOT_NULL(key);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(
        HASHTABLE_TOMBSTONE_MARKER, key,
        "foreach should skip tombstones");
    count++;
  });
  TEST_ASSERT_EQUAL(2, count);

  hashtable_free(table);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_hashtable_with_numbers);
  RUN_TEST(test_hashtable_delete);
  RUN_TEST(test_hashtable_remove);
  RUN_TEST(test_hashtable_foreach);
  RUN_TEST(test_hashtable_exists);
  RUN_TEST(test_hashtable_dump);

  // Tombstone and probe chain tests
  RUN_TEST(test_hashtable_deletion_preserves_probe_chain);
  RUN_TEST(test_hashtable_tombstone_reuse);
  RUN_TEST(test_hashtable_update_existing_key);
  RUN_TEST(test_hashtable_resize_with_tombstones);
  RUN_TEST(test_hashtable_delete_nonexistent);
  RUN_TEST(test_hashtable_remove_after_delete);
  RUN_TEST(test_hashtable_delete_calls_free_func);
  RUN_TEST(test_hashtable_multiple_delete_reinsert);
  RUN_TEST(test_hashtable_tombstone_saturation);
  RUN_TEST(test_hashtable_foreach_skips_tombstones);

  return UNITY_END();
}
