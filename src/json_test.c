#include "unity.h"
#include <rcl/json.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void setUp(void) {}

void tearDown(void) {}

#define VALID_JSON_1                                                           \
  "{\"key\": \"value\", \"arr\": [3,2,1], \"blah\": false, \"foo\": null}"

static void test_parse_string(void) {
  json_value_t *val = json_parse_assert("\"hello world\"");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_STRING, val->type);
  TEST_ASSERT_EQUAL_STRING("hello world", json_value_get_string(val));
  json_value_destroy(&val);
  TEST_ASSERT_NULL(val);
}

static void test_parse_string_escapes(void) {
  json_value_t *val = json_parse_assert("\"hello\\nworld\"");
  TEST_ASSERT_EQUAL_STRING("hello\nworld", json_value_get_string(val));
  json_value_destroy(&val);

  val = json_parse_assert("\"tab\\there\"");
  TEST_ASSERT_EQUAL_STRING("tab\there", json_value_get_string(val));
  json_value_destroy(&val);

  val = json_parse_assert("\"quote\\\"inside\"");
  TEST_ASSERT_EQUAL_STRING("quote\"inside", json_value_get_string(val));
  json_value_destroy(&val);

  val = json_parse_assert("\"back\\\\slash\"");
  TEST_ASSERT_EQUAL_STRING("back\\slash", json_value_get_string(val));
  json_value_destroy(&val);
}

static void test_parse_string_unicode(void) {
  // \u0041 is 'A'
  json_value_t *val = json_parse_assert("\"\\u0041\"");
  TEST_ASSERT_EQUAL_STRING("A", json_value_get_string(val));
  json_value_destroy(&val);

  // \u00E9 is 'é' (2-byte UTF-8)
  val = json_parse_assert("\"caf\\u00E9\"");
  TEST_ASSERT_EQUAL_STRING("café", json_value_get_string(val));
  json_value_destroy(&val);
}

static void test_parse_number_integer(void) {
  json_value_t *val = json_parse_assert("42");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_NUMBER, val->type);
  TEST_ASSERT_EQUAL_FLOAT(42.0, json_value_get_double(val));
  json_value_destroy(&val);
}

static void test_parse_number_negative(void) {
  json_value_t *val = json_parse_assert("-7");
  TEST_ASSERT_EQUAL_FLOAT(-7.0, json_value_get_double(val));
  json_value_destroy(&val);
}

static void test_parse_number_decimal(void) {
  json_value_t *val = json_parse_assert("3.14");
  TEST_ASSERT_EQUAL_FLOAT(3.14, json_value_get_double(val));
  json_value_destroy(&val);
}

static void test_parse_number_exponent(void) {
  json_value_t *val = json_parse_assert("1e10");
  TEST_ASSERT_EQUAL_FLOAT(1e10, json_value_get_double(val));
  json_value_destroy(&val);

  val = json_parse_assert("2.5E-3");
  TEST_ASSERT_EQUAL_FLOAT(2.5e-3, json_value_get_double(val));
  json_value_destroy(&val);
}

static void test_parse_true(void) {
  json_value_t *val = json_parse_assert("true");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_BOOL, val->type);
  TEST_ASSERT_TRUE(json_value_get_bool(val));
  json_value_destroy(&val);
}

static void test_parse_false(void) {
  json_value_t *val = json_parse_assert("false");
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_BOOL, val->type);
  TEST_ASSERT_FALSE(json_value_get_bool(val));
  json_value_destroy(&val);
}

static void test_parse_null(void) {
  json_value_t *val = json_parse_assert("null");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_NULL, val->type);
  TEST_ASSERT_TRUE(json_value_is_null(val));
  json_value_destroy(&val);
}

static void test_parse_empty_array(void) {
  json_value_t *val = json_parse_assert("[]");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_ARRAY, val->type);
  TEST_ASSERT_EQUAL_size_t(0, json_value_get_array(val)->length);
  json_value_destroy(&val);
}

static void test_parse_array(void) {
  json_value_t *val = json_parse_assert("[1, \"two\", true, null]");
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_ARRAY, val->type);

  array_t *arr = json_value_get_array(val);
  TEST_ASSERT_EQUAL_size_t(4, arr->length);

  ARRAY_OF(json_value_t *) *items = (void *)arr;
  TEST_ASSERT_EQUAL_FLOAT(1.0, json_value_get_double(items->data[0]));
  TEST_ASSERT_EQUAL_STRING("two", json_value_get_string(items->data[1]));
  TEST_ASSERT_TRUE(json_value_get_bool(items->data[2]));
  TEST_ASSERT_TRUE(json_value_is_null(items->data[3]));

  json_value_destroy(&val);
}

static void test_parse_nested_array(void) {
  json_value_t *val = json_parse_assert("[[1, 2], [3, 4]]");
  array_t *outer = json_value_get_array(val);
  TEST_ASSERT_EQUAL_size_t(2, outer->length);

  ARRAY_OF(json_value_t *) *outer_items = (void *)outer;

  array_t *inner0 = json_value_get_array(outer_items->data[0]);
  TEST_ASSERT_EQUAL_size_t(2, inner0->length);
  ARRAY_OF(json_value_t *) *inner0_items = (void *)inner0;
  TEST_ASSERT_EQUAL_FLOAT(1.0, json_value_get_double(inner0_items->data[0]));
  TEST_ASSERT_EQUAL_FLOAT(2.0, json_value_get_double(inner0_items->data[1]));

  array_t *inner1 = json_value_get_array(outer_items->data[1]);
  TEST_ASSERT_EQUAL_size_t(2, inner1->length);
  ARRAY_OF(json_value_t *) *inner1_items = (void *)inner1;
  TEST_ASSERT_EQUAL_FLOAT(3.0, json_value_get_double(inner1_items->data[0]));
  TEST_ASSERT_EQUAL_FLOAT(4.0, json_value_get_double(inner1_items->data[1]));

  json_value_destroy(&val);
}

static void test_parse_empty_object(void) {
  json_value_t *val = json_parse_assert("{}");
  TEST_ASSERT_NOT_NULL(val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_OBJECT, val->type);
  TEST_ASSERT_EQUAL_size_t(0, json_value_get_object(val)->length);
  json_value_destroy(&val);
}

static void test_parse_object(void) {
  json_value_t *val = json_parse_assert(VALID_JSON_1);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_OBJECT, val->type);

  hashtable_t *obj = json_value_get_object(val);
  TEST_ASSERT_EQUAL_size_t(4, obj->length);

  json_value_t *key_val = hashtable_get(obj, "key");
  TEST_ASSERT_NOT_NULL(key_val);
  TEST_ASSERT_EQUAL_STRING("value", json_value_get_string(key_val));

  json_value_t *arr_val = hashtable_get(obj, "arr");
  TEST_ASSERT_NOT_NULL(arr_val);
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_ARRAY, arr_val->type);
  TEST_ASSERT_EQUAL_size_t(3, json_value_get_array(arr_val)->length);

  json_value_t *blah_val = hashtable_get(obj, "blah");
  TEST_ASSERT_NOT_NULL(blah_val);
  TEST_ASSERT_FALSE(json_value_get_bool(blah_val));

  json_value_t *foo_val = hashtable_get(obj, "foo");
  TEST_ASSERT_NOT_NULL(foo_val);
  TEST_ASSERT_TRUE(json_value_is_null(foo_val));

  json_value_destroy(&val);
}

static void test_parse_nested_object(void) {
  json_value_t *val =
      json_parse_assert("{\"outer\": {\"inner\": 42}}");

  hashtable_t *obj = json_value_get_object(val);
  json_value_t *outer = hashtable_get(obj, "outer");
  TEST_ASSERT_EQUAL_INT(JSON_VALUE_TYPE_OBJECT, outer->type);

  hashtable_t *inner_obj = json_value_get_object(outer);
  json_value_t *inner = hashtable_get(inner_obj, "inner");
  TEST_ASSERT_EQUAL_FLOAT(42.0, json_value_get_double(inner));

  json_value_destroy(&val);
}

static void test_parse_trailing_comma(void) {
  json_value_t *result = NULL;
  json_error_t *error = NULL;

  bool ok = json_parse_safe("{\"a\": 1,}", &result, &error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(result);
  json_error_destroy(&error);

  ok = json_parse_safe("[1, 2, 3,]", &result, &error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(result);
  json_error_destroy(&error);
}

static void test_parse_safe_invalid(void) {
  json_value_t *result = NULL;
  json_error_t *error = NULL;

  bool ok = json_parse_safe("{{", &result, &error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(result);
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_NOT_NULL(error->message);
  json_error_destroy(&error);

  ok = json_parse_safe("\"unterminated", &result, &error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(result);
  TEST_ASSERT_NOT_NULL(error);
  json_error_destroy(&error);

  ok = json_parse_safe("", &result, &error);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_NULL(result);
  TEST_ASSERT_NOT_NULL(error);
  json_error_destroy(&error);
}

int main(void) {
  UNITY_BEGIN();

  RUN_TEST(test_parse_string);
  RUN_TEST(test_parse_string_escapes);
  RUN_TEST(test_parse_string_unicode);
  RUN_TEST(test_parse_number_integer);
  RUN_TEST(test_parse_number_negative);
  RUN_TEST(test_parse_number_decimal);
  RUN_TEST(test_parse_number_exponent);
  RUN_TEST(test_parse_true);
  RUN_TEST(test_parse_false);
  RUN_TEST(test_parse_null);
  RUN_TEST(test_parse_empty_array);
  RUN_TEST(test_parse_array);
  RUN_TEST(test_parse_nested_array);
  RUN_TEST(test_parse_empty_object);
  RUN_TEST(test_parse_object);
  RUN_TEST(test_parse_nested_object);
  RUN_TEST(test_parse_trailing_comma);
  RUN_TEST(test_parse_safe_invalid);

  return UNITY_END();
}
