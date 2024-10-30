#include "rcl_string.h"
#include "unity.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

static void test_simple_string(void) {
  string_t *str = string_new("hello");
  TEST_ASSERT_EQUAL_STRING("hello", str->data);
  string_free(str);
}

static void test_string_steal(void) {
  char *str = strdup("hello");
  string_t *s = string_new_steal(&str);
  TEST_ASSERT_EQUAL_STRING("hello", s->data);
  TEST_ASSERT_NULL(str);
  string_free(s);
}

static void test_string_append(void) {
  {
    string_t *s = string_new("hello");
    string_t *s2 = string_new(" world");
    string_append(s, s2);
    TEST_ASSERT_EQUAL_STRING("hello world", s->data);
    string_free(s);
    string_free(s2);
  }

  {
    string_t *s = string_new("hello");
    string_append_str(s, " world");

    TEST_ASSERT_EQUAL_STRING("hello world", s->data);
    string_free(s);
  }
}

static void test_string_prepend(void) {
  {
    string_t *s = string_new("world");
    string_prepend_str(s, "hello ");
    TEST_ASSERT_EQUAL_STRING("hello world", s->data);
    string_free(s);
  }

  {
    string_t *s = string_new("world");
    string_t *s2 = string_new("hello ");
    string_prepend(s, s2);
    TEST_ASSERT_EQUAL_STRING("hello world", s->data);
    string_free(s);
    string_free(s2);
  }

  {
    string_t *s = string_new("you");

    string_prepend_str(s, "with ");
    string_prepend_str(s, "be ");
    string_prepend_str(s, "force ");
    string_prepend_str(s, "the ");
    string_prepend_str(s, "May ");

    TEST_ASSERT_EQUAL_STRING("May the force be with you", s->data);
    string_free(s);
  }
}

static void test_string_a_little_of_both(void) {
  string_t *s = string_new("with");

  string_prepend_str(s, "be ");
  string_prepend_str(s, "force ");
  string_prepend_str(s, "the ");
  string_prepend_str(s, "May ");

  string_append_str(s, " you");

  TEST_ASSERT_EQUAL_STRING("May the force be with you", s->data);
  TEST_ASSERT_EQUAL_CHAR(0, s->data[s->length]);
  string_free(s);
}

static void stress_test(void) {
  string_t *s = string_new("");

  for (int i = 0; i < 1000000; i++) {
    string_append_str(s, "42");
  }

  TEST_ASSERT_EQUAL_UINT(2000000, s->length);

  string_destroy(&s);
  TEST_ASSERT_NULL(s);
}

static void test_string_clear(void) {
  string_t *s = string_new("");

  for (int i = 0; i < 100; i++) {
    string_append_str(s, "42");
  }

  TEST_ASSERT_EQUAL_UINT(200, s->length);
  size_t capacity = s->capacity;
  string_clear(s);
  TEST_ASSERT_EQUAL_UINT(0, s->length);
  TEST_ASSERT_EQUAL_UINT(capacity, s->capacity);
  TEST_ASSERT_EQUAL_STRING("", s->data);

  string_free(s);
}

static void test_string_reverse(void) {
  string_t *s = string_new("hello");
  string_reverse(s);
  TEST_ASSERT_EQUAL_STRING("olleh", s->data);
  string_free(s);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_simple_string);
  RUN_TEST(test_string_steal);
  RUN_TEST(test_string_append);
  RUN_TEST(test_string_prepend);
  RUN_TEST(test_string_a_little_of_both);
  RUN_TEST(stress_test);
  RUN_TEST(test_string_clear);
  RUN_TEST(test_string_reverse);

  return UNITY_END();
}
