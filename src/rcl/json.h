#pragma once

#include "rcl/array.h"
#include "rcl/hashtable.h"

#ifndef RCL_JSON_ASSERT_GETS
#define RCL_JSON_ASSERT_GETS 1
#endif

#define out *
#define set_out_value(ptr, val) do { if ((ptr)) *(ptr) = (val); } while (0)

typedef struct json_error_s {
  char *message;
  size_t col;
} json_error_t;

json_error_t *json_error_new(char *message, size_t col);
void json_error_free(json_error_t *self);
void json_error_destroy(json_error_t **self);

typedef enum {
  JSON_VALUE_TYPE_NULL,
  JSON_VALUE_TYPE_BOOL,
  JSON_VALUE_TYPE_NUMBER,
  JSON_VALUE_TYPE_STRING,
  JSON_VALUE_TYPE_ARRAY,
  JSON_VALUE_TYPE_OBJECT,
} json_value_type_e;

typedef struct json_value_s {
  json_value_type_e type;
  union {
    bool boolean;
    double number;
    char *string;
    array_t *array;
    hashtable_t *object;
  } value;
} json_value_t;

bool json_parse_safe(const char *src, json_value_t out *result,
                     json_error_t out *error);

json_value_t *json_parse_assert(const char *src);

void json_value_free(json_value_t *self);
void json_value_destroy(json_value_t **ptr);

void json_dump(json_value_t *val, int indent_size);

double json_value_get_double(json_value_t *self);
bool json_value_get_bool(json_value_t *self);
char *json_value_get_string(json_value_t *self);
array_t *json_value_get_array(json_value_t *self);
hashtable_t *json_value_get_object(json_value_t *self);
bool json_value_is_null(json_value_t *self);
