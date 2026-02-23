#include "rcl/json.h"
#include "rcl/array.h"
#include "rcl/hashtable.h"
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define check_out_of_bounds_token(arr, index_ptr, err, label)                  \
  if (*index_ptr >= arr->length) {                                             \
    err = json_error_new(strdup("Unexpected end of input"), 0);                \
    goto label;                                                                \
  }

typedef enum {
  JSON_TOKEN_LBRACE,
  JSON_TOKEN_RBRACE,
  JSON_TOKEN_LBRACK,
  JSON_TOKEN_RBRACK,
  JSON_TOKEN_COLON,
  JSON_TOKEN_COMMA,
  JSON_TOKEN_STRING,
  JSON_TOKEN_NUMBER,
  JSON_TOKEN_TRUE,
  JSON_TOKEN_FALSE,
  JSON_TOKEN_NULL,
  JSON_TOKEN_INVALID = -1,
} json_token_type_e;

typedef struct json_token_s {
  json_token_type_e type;
  const char *start;
  size_t length;
} json_token_t;

json_value_t *json_parse_token(const array_t *_tokens, size_t *cursor,
                               json_error_t out *error);
json_value_t *json_parse_string(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error);
json_value_t *json_parse_number(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error);
json_value_t *json_parse_bool(const array_t *_tokens, size_t *cursor,
                              json_error_t out *error);
json_value_t *json_parse_array(const array_t *_tokens, size_t *cursor,
                               json_error_t out *error);
json_value_t *json_parse_object(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error);
json_value_t *json_parse_null(const array_t *_tokens, size_t *cursor,
                              json_error_t out *error);

json_token_type_e _get_token_type(const char *ptr) {
  switch (*ptr) {
  case '{':
    return JSON_TOKEN_LBRACE;
  case '}':
    return JSON_TOKEN_RBRACE;
  case '[':
    return JSON_TOKEN_LBRACK;
  case ']':
    return JSON_TOKEN_RBRACK;
  case ':':
    return JSON_TOKEN_COLON;
  case ',':
    return JSON_TOKEN_COMMA;
  case '"':
    return JSON_TOKEN_STRING;
  default:
    if (strncmp(ptr, "true", 4) == 0)
      return JSON_TOKEN_TRUE;
    if (strncmp(ptr, "false", 5) == 0)
      return JSON_TOKEN_FALSE;
    if (strncmp(ptr, "null", 4) == 0)
      return JSON_TOKEN_NULL;
    if (isdigit(*ptr) || *ptr == '-')
      return JSON_TOKEN_NUMBER;
    break;
  }
  return JSON_TOKEN_INVALID;
}

json_error_t *json_error_new(char *message, size_t col) {
  json_error_t *self = malloc(sizeof(*self));

  *self = (json_error_t){
      .message = message,
      .col = col,
  };

  return self;
}

void json_error_free(json_error_t *self) {
  if (!self)
    return;
  if (self->message)
    free(self->message);
  free(self);
}

void json_error_destroy(json_error_t **self) {
  if (self) {
    json_error_free(*self);
    *self = NULL;
  }
}

const char *_get_string_end(const char *start) {
  const char *ptr = start;
  while (*ptr) {
    if (*ptr == '\\') {
      // Validate escaped character
      ptr++;
      if (*ptr == '\0')
        return NULL; // Unterminated escape
      if (strchr("\"\\/bfnrtu", *ptr) == NULL)
        return NULL; // Invalid escape
      if (*ptr == 'u') {
        // Validate Unicode escape (4 hex digits)
        for (int i = 1; i <= 4; i++) {
          if (!isxdigit(ptr[i])) {
            return NULL; // Invalid Unicode escape
          }
        }
        ptr += 5; // skip 'u' + 4 hex digits
      } else {
        ptr++; // skip the escaped character
      }
    } else if (*ptr == '"') {
      return ptr;
    } else {
      ptr++;
    }
  }
  return NULL; // Unterminated string
}

// Decodes a JSON string token into a clean C string. The input `start` points
// to the opening quote and `length` includes both quotes. Escape sequences are
// resolved into their actual characters. The lexer has already validated the
// input, so we trust it here.
char *decode_json_string(const char *start, size_t length,
                         size_t out decoded_length) {
  // Skip opening and closing quotes
  const char *src = start + 1;
  const char *end = start + length - 1;

  // Allocate worst case (same length minus quotes), will be smaller if escapes
  char *result = malloc(length - 1);
  size_t pos = 0;

  while (src < end) {
    if (*src == '\\') {
      src++;
      switch (*src) {
      case '"':
        result[pos++] = '"';
        break;
      case '\\':
        result[pos++] = '\\';
        break;
      case '/':
        result[pos++] = '/';
        break;
      case 'b':
        result[pos++] = '\b';
        break;
      case 'f':
        result[pos++] = '\f';
        break;
      case 'n':
        result[pos++] = '\n';
        break;
      case 'r':
        result[pos++] = '\r';
        break;
      case 't':
        result[pos++] = '\t';
        break;
      case 'u': {
        // Parse 4 hex digits into a code point
        unsigned int cp = 0;
        for (int i = 0; i < 4; i++) {
          src++;
          cp <<= 4;
          if (*src >= '0' && *src <= '9')
            cp |= *src - '0';
          else if (*src >= 'a' && *src <= 'f')
            cp |= *src - 'a' + 10;
          else if (*src >= 'A' && *src <= 'F')
            cp |= *src - 'A' + 10;
        }
        // Encode as UTF-8
        if (cp <= 0x7F) {
          result[pos++] = (char)cp;
        } else if (cp <= 0x7FF) {
          result[pos++] = (char)(0xC0 | (cp >> 6));
          result[pos++] = (char)(0x80 | (cp & 0x3F));
        } else {
          result[pos++] = (char)(0xE0 | (cp >> 12));
          result[pos++] = (char)(0x80 | ((cp >> 6) & 0x3F));
          result[pos++] = (char)(0x80 | (cp & 0x3F));
        }
        break;
      }
      }
      src++;
    } else {
      result[pos++] = *src++;
    }
  }

  result[pos] = '\0';
  set_out_value(decoded_length, pos);
  return result;
}

const char *_get_number_end(const char *start) {
  const char *ptr = start;
  if (*ptr == '-')
    ptr++;
  while (isdigit(*ptr))
    ptr++;
  if (*ptr == '.') {
    ptr++;
    while (isdigit(*ptr))
      ptr++;
  }
  if (*ptr == 'e' || *ptr == 'E') {
    ptr++;
    if (*ptr == '+' || *ptr == '-')
      ptr++;
    while (isdigit(*ptr))
      ptr++;
  }

  if (ptr == start) // No digits found
    return NULL;
  if (*start == '-' && ptr == start + 1) // Only a '-' found
    return NULL;
  return ptr;
}

bool _json_lex(const char *src, array_t out *tokens, json_error_t out *error) {
  if (tokens)
    *tokens = NULL;
  if (error)
    *error = NULL;

  const char *ptr = src;
  array_t *arr = array_new(json_token_t);
  json_error_t *_error = NULL;

  while (*ptr) {
    while (*ptr && isspace(*ptr))
      ptr++;
    if (*ptr == '\0')
      break;

    json_token_type_e token_type = _get_token_type(ptr);

    if (token_type == JSON_TOKEN_INVALID) {
      _error = json_error_new(strdup("Invalid token"), ptr - src);
      goto return_error;
    }

    json_token_t token = {.type = token_type, .start = ptr, .length = 1};
    switch (token_type) {
    case JSON_TOKEN_STRING: {
      const char *end = _get_string_end(ptr + 1);
      if (!end) {
        _error = json_error_new(strdup("Unterminated string"), ptr - src);
        goto return_error;
      }
      token.length = end - ptr + 1;
      break;
    };
    case JSON_TOKEN_TRUE:
      token.length = 4;
      break;
    case JSON_TOKEN_FALSE:
      token.length = 5;
      break;
    case JSON_TOKEN_NULL:
      token.length = 4;
      break;
    case JSON_TOKEN_NUMBER: {
      const char *end = _get_number_end(ptr);
      if (!end) {
        _error = json_error_new(strdup("Invalid number"), ptr - src);
        goto return_error;
      }
      token.length = end - ptr;
      break;
    };
    default:
      break;
    }

    array_push(arr, token);
    ptr += token.length;
  }

  if (tokens) {
    *tokens = arr;
  } else {
    array_destroy(&arr);
  }

  return true;

return_error:
  array_destroy(&arr);
  if (error) {
    *error = _error;
  } else {
    json_error_destroy(&_error);
  }
  return false;
}

void debug_lexer_tokens(array_t *_tokens) {
  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;

  for (size_t i = 0; i < tokens->length; i++) {
    json_token_t *token = &tokens->data[i];
    printf("Token: type=%d, value='%.*s'\n", token->type, (int)token->length,
           token->start);
  }
}

json_value_t *json_parse_token(const array_t *_tokens, size_t *cursor,
                               json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  if (*cursor >= tokens->length) {
    _error = json_error_new(strdup("Unexpected end of input"), 0);
    goto return_error;
  }

  json_token_t *token = &tokens->data[*cursor];
  switch (token->type) {
  case JSON_TOKEN_STRING:
    return json_parse_string(_tokens, cursor, error);
  case JSON_TOKEN_NUMBER:
    return json_parse_number(_tokens, cursor, error);
  case JSON_TOKEN_TRUE:
  case JSON_TOKEN_FALSE:
    return json_parse_bool(_tokens, cursor, error);
  case JSON_TOKEN_NULL:
    return json_parse_null(_tokens, cursor, error);
  case JSON_TOKEN_LBRACK:
    return json_parse_array(_tokens, cursor, error);
  case JSON_TOKEN_LBRACE:
    return json_parse_object(_tokens, cursor, error);
  default:
    _error = json_error_new(strdup("Unexpected token"), 0);
    goto return_error;
  }

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_object(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error) {
  set_out_value(error, NULL);
  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  hashtable_t *object = hashtable_new();
  json_error_t *_error = NULL;

  object->free_func = (hashtable_free_func_t)json_value_free;

  check_out_of_bounds_token(_tokens, cursor, _error, return_error);

  if (tokens->data[*cursor].type != JSON_TOKEN_LBRACE) {
    _error = json_error_new(strdup("Expected '{' at start of object"), 0);
    goto return_error;
  }
  (*cursor)++;

  while (true) {
    check_out_of_bounds_token(_tokens, cursor, _error, return_error);
    json_token_t *token = &tokens->data[*cursor];
    if (token->type == JSON_TOKEN_RBRACE) {
      (*cursor)++;
      break;
    }

    if (token->type != JSON_TOKEN_STRING) {
      _error = json_error_new(strdup("Expected string key in object"), 0);
      goto return_error;
    }
    (*cursor)++;

    check_out_of_bounds_token(_tokens, cursor, _error, return_error);
    if (tokens->data[*cursor].type != JSON_TOKEN_COLON) {
      _error = json_error_new(strdup("Expected ':' after key in object"), 0);
      goto return_error;
    }
    (*cursor)++;

    // This call already advances the cursor, so we don't need to do it here.
    json_value_t *value = json_parse_token(_tokens, cursor, error);
    if (!value)
      goto return_error;

    char *key = decode_json_string(token->start, token->length, NULL);
    hashtable_set(object, key, value);
    free(key);

    // We allow trailing commas in objects, so we check for a comma after
    // parsing a value.
    if (*cursor < tokens->length &&
        (&tokens->data[*cursor])->type == JSON_TOKEN_COMMA) {
      (*cursor)++;
    }
  }

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_OBJECT,
      .value.object = object,
  };
  return self;

return_error:
  hashtable_destroy(&object);
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_bool(const array_t *_tokens, size_t *cursor,
                              json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  check_out_of_bounds_token(tokens, cursor, _error, return_error);

  json_token_t *token = &tokens->data[*cursor];
  (*cursor)++;

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_BOOL,
      .value.boolean = token->type == JSON_TOKEN_TRUE,
  };
  return self;

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_string(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  check_out_of_bounds_token(tokens, cursor, _error, return_error);

  json_token_t *token = &tokens->data[*cursor];
  char *str = decode_json_string(token->start, token->length, NULL);
  (*cursor)++;

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_STRING,
      .value.string = str,
  };
  return self;

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_number(const array_t *_tokens, size_t *cursor,
                                json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  check_out_of_bounds_token(tokens, cursor, _error, return_error);

  json_token_t *token = &tokens->data[*cursor];
  (*cursor)++;

  // strtod handles all valid JSON number formats (integer, decimal, exponent)
  char *buf = strndup(token->start, token->length);
  double value = strtod(buf, NULL);
  free(buf);

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_NUMBER,
      .value.number = value,
  };
  return self;

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_null(const array_t *_tokens, size_t *cursor,
                              json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  check_out_of_bounds_token(tokens, cursor, _error, return_error);

  (*cursor)++;

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_NULL,
  };
  return self;

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

json_value_t *json_parse_array(const array_t *_tokens, size_t *cursor,
                               json_error_t out *error) {
  set_out_value(error, NULL);
  ARRAY_OF(json_token_t) *tokens = (void *)_tokens;
  array_t *array = array_new(json_value_t *);
  json_error_t *_error = NULL;

  array->free_func = (array_free_func *)json_value_free;

  check_out_of_bounds_token(tokens, cursor, _error, return_error);

  if (tokens->data[*cursor].type != JSON_TOKEN_LBRACK) {
    _error = json_error_new(strdup("Expected '[' at start of array"), 0);
    goto return_error;
  }
  (*cursor)++;

  while (true) {
    check_out_of_bounds_token(tokens, cursor, _error, return_error);
    if (tokens->data[*cursor].type == JSON_TOKEN_RBRACK) {
      (*cursor)++;
      break;
    }

    json_value_t *value = json_parse_token(_tokens, cursor, error);
    if (!value)
      goto return_error;

    array_push(array, value);

    if (*cursor < tokens->length &&
        tokens->data[*cursor].type == JSON_TOKEN_COMMA) {
      (*cursor)++;
    }
  }

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_ARRAY,
      .value.array = array,
  };
  return self;

return_error:
  array_destroy(&array);
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return NULL;
}

void json_value_free(json_value_t *self) {
  if (!self)
    return;
  switch (self->type) {
  case JSON_VALUE_TYPE_STRING:
    free(self->value.string);
    break;
  case JSON_VALUE_TYPE_ARRAY:
    array_destroy(&self->value.array);
    break;
  case JSON_VALUE_TYPE_OBJECT:
    hashtable_destroy(&self->value.object);
    break;
  default:
    break;
  }
  free(self);
}

void json_value_destroy(json_value_t **ptr) {
  if (ptr) {
    json_value_free(*ptr);
    *ptr = NULL;
  }
}

bool json_parse_safe(const char *src, json_value_t out *result,
                     json_error_t out *error) {
  set_out_value(result, NULL);
  set_out_value(error, NULL);

  array_t *tokens = NULL;
  json_error_t *lex_error = NULL;

  if (!_json_lex(src, &tokens, &lex_error)) {
    set_out_value(error, lex_error);
    if (!error)
      json_error_destroy(&lex_error);
    return false;
  }

  size_t cursor = 0;
  json_error_t *parse_error = NULL;
  json_value_t *value = json_parse_token(tokens, &cursor, &parse_error);
  array_destroy(&tokens);

  if (!value) {
    set_out_value(error, parse_error);
    if (!error)
      json_error_destroy(&parse_error);
    return false;
  }

  set_out_value(result, value);
  if (!result)
    json_value_destroy(&value);
  return true;
}

void _json_dump(json_value_t *val, int indent_size, int indent_level) {
  if (!val) {
    printf("null");
    return;
  }

  switch (val->type) {
  case JSON_VALUE_TYPE_NULL:
    printf("null");
    break;
  case JSON_VALUE_TYPE_BOOL:
    printf("%s", val->value.boolean ? "true" : "false");
    break;
  case JSON_VALUE_TYPE_NUMBER:
    printf("%g", val->value.number);
    break;
  case JSON_VALUE_TYPE_STRING:
    // TODO: re-encode escape sequences
    printf("\"%s\"", val->value.string);
    break;
  case JSON_VALUE_TYPE_ARRAY: {
    ARRAY_OF(json_value_t *) *arr = (void *)val->value.array;
    if (arr->length == 0) {
      printf("[]");
      break;
    }
    printf("[\n");
    for (size_t i = 0; i < arr->length; i++) {
      printf("%*s", indent_size * (indent_level + 1), "");
      _json_dump(arr->data[i], indent_size, indent_level + 1);
      if (i + 1 < arr->length)
        printf(",");
      printf("\n");
    }
    printf("%*s]", indent_size * indent_level, "");
    break;
  }
  case JSON_VALUE_TYPE_OBJECT: {
    hashtable_t *obj = val->value.object;
    if (obj->length == 0) {
      printf("{}");
      break;
    }
    printf("{\n");
    size_t printed = 0;
    hashtable_foreach(obj, {
      printf("%*s\"%s\": ", indent_size * (indent_level + 1), "", key);
      _json_dump((json_value_t *)value, indent_size, indent_level + 1);
      printed++;
      if (printed < obj->length)
        printf(",");
      printf("\n");
    });
    printf("%*s}", indent_size * indent_level, "");
    break;
  }
  }
}

void json_dump(json_value_t *val, int indent_size) {
  _json_dump(val, indent_size, 0);
  printf("\n");
}

json_value_t *json_parse_assert(const char *src) {
  json_value_t *result = NULL;
  json_error_t *error = NULL;

  if (!json_parse_safe(src, &result, &error)) {
    fprintf(stderr, "JSON parsing error at column %zu: %s\n", error->col,
            error->message);
    json_error_destroy(&error);
    exit(EXIT_FAILURE);
  }

  return result;
}

double json_value_get_double(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
  assert(self->type == JSON_VALUE_TYPE_NUMBER);
#endif
  return self->value.number;
}

bool json_value_get_bool(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
  assert(self->type == JSON_VALUE_TYPE_BOOL);
#endif
  return self->value.boolean;
}

char *json_value_get_string(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
  assert(self->type == JSON_VALUE_TYPE_STRING);
#endif
  return self->value.string;
}

array_t *json_value_get_array(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
  assert(self->type == JSON_VALUE_TYPE_ARRAY);
#endif
  return self->value.array;
}

hashtable_t *json_value_get_object(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
  assert(self->type == JSON_VALUE_TYPE_OBJECT);
#endif
  return self->value.object;
}

bool json_value_is_null(json_value_t *self) {
#if RCL_JSON_ASSERT_GETS
  assert(self);
#endif
  return self->type == JSON_VALUE_TYPE_NULL;
}
