#include "rcl/json.h"
#include "rcl/array.h"
#include "rcl/hashtable.h"
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEFAULT_JSON_ARRAY_CAPACITY
#define DEFAULT_JSON_ARRAY_CAPACITY 4
#endif

#ifndef DEFAULT_JSON_OBJECT_CAPACITY
#define DEFAULT_JSON_OBJECT_CAPACITY 13
#endif

// Locale-independent strtod. JSON mandates '.' as the decimal separator, but
// strtod respects the current locale which may use ',' instead. We use the
// platform-specific locale-aware variant with an explicit "C" locale to avoid
// this.
#ifdef _WIN32
#include <locale.h>
static double json_strtod(const char *str, char **endptr) {
  static _locale_t c_locale = NULL;
  if (!c_locale)
    c_locale = _create_locale(LC_NUMERIC, "C");
  return _strtod_l(str, endptr, c_locale);
}
#else
#include <locale.h>
#if defined(__APPLE__)
#include <xlocale.h>
#endif
static double json_strtod(const char *str, char **endptr) {
  static locale_t c_locale = (locale_t)0;
  if (!c_locale)
    c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  return strtod_l(str, endptr, c_locale);
}
#endif

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
  JSON_TOKEN_END = -2,
} json_token_type_e;

json_value_t *json_parse_token(const char *src, const char **ptr,
                               json_error_t out *error);
json_value_t *json_parse_string(const char *src, const char **ptr,
                                json_error_t out *error);
json_value_t *json_parse_number(const char *src, const char **ptr,
                                json_error_t out *error);
json_value_t *json_parse_array(const char *src, const char **ptr,
                               json_error_t out *error);
json_value_t *json_parse_object(const char *src, const char **ptr,
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
  char *ptr;

  if (!(ptr = memchr(src, '\\', end - src))) {
    // Fast path: no escapes, just copy the substring
    char *result = strndup(src, end - src);
    set_out_value(decoded_length, end - src);
    return result;
  }

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

json_token_type_e _json_lex_get_next_token(const char *src, const char **_ptr,
                                           json_error_t out *error) {
  // This function is a lex-parse hybrid. We look at what the next token is and
  // return its type. We also advance the pointer to the next thing to parse. If
  // we find anything invalid along the way, we return an error.
  //
  // For keywords such as null, true, and false we validate the entire thing and
  // move the pointer past it. For values, such as numbers we leave the pointer
  // at the first character and let the parser deal with it. For strings, we
  // leave the pointer at the opening quote.
  //
  // This function skips whitespace only, but returns commas and colons (both of
  // which move the pointer past themselves).
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  const char *ptr = *_ptr;
  while (*ptr && isspace(*ptr))
    ptr++;
  if (!*ptr) {
    *_ptr = ptr;
    return JSON_TOKEN_END;
  }

  json_token_type_e token_type = _get_token_type(ptr);
  switch (token_type) {
  case JSON_TOKEN_INVALID:
  case JSON_TOKEN_END:
    _error = json_error_new(strdup("Invalid token"), ptr - src);
    goto return_error;
  case JSON_TOKEN_COLON:
  case JSON_TOKEN_COMMA:
  case JSON_TOKEN_LBRACE:
  case JSON_TOKEN_RBRACE:
  case JSON_TOKEN_LBRACK:
  case JSON_TOKEN_RBRACK:
    *_ptr = ptr + 1;
    return token_type;
  case JSON_TOKEN_STRING:
  case JSON_TOKEN_NUMBER:
    *_ptr = ptr;
    return token_type;
  case JSON_TOKEN_FALSE:
    *_ptr = ptr + 5;
    return token_type;
  case JSON_TOKEN_TRUE:
  case JSON_TOKEN_NULL:
    *_ptr = ptr + 4;
    return token_type;
  }

return_error:
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return JSON_TOKEN_INVALID;
}

json_value_t *json_parse_token(const char *src, const char **ptr,
                               json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  __auto_type token_type = _json_lex_get_next_token(src, ptr, &_error);
  if (_error)
    goto return_error;

  if (token_type == JSON_TOKEN_END) {
    // We do not treat end of input as an error here, the caller can decide if
    // it's unexpected or not. We just return NULL to indicate no more tokens.
    return NULL;
  }

  switch (token_type) {
  case JSON_TOKEN_STRING:
    return json_parse_string(src, ptr, error);
  case JSON_TOKEN_NUMBER:
    return json_parse_number(src, ptr, error);
  case JSON_TOKEN_TRUE:
  case JSON_TOKEN_FALSE: {
    json_value_t *value = malloc(sizeof(*value));
    *value = (json_value_t){
        .type = JSON_VALUE_TYPE_BOOL,
        .value.boolean = token_type == JSON_TOKEN_TRUE,
    };
    return value;
  }
  case JSON_TOKEN_NULL: {
    json_value_t *value = malloc(sizeof(*value));
    *value = (json_value_t){
        .type = JSON_VALUE_TYPE_NULL,
        .value = {0},
    };
    return value;
  }
  case JSON_TOKEN_LBRACK:
    return json_parse_array(src, ptr, error);
  case JSON_TOKEN_LBRACE:
    return json_parse_object(src, ptr, error);
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

json_value_t *json_parse_object(const char *src, const char **ptr,
                                json_error_t out *error) {
  set_out_value(error, NULL);
  hashtable_t *object =
      hashtable_new_with_capacity(DEFAULT_JSON_OBJECT_CAPACITY);
  json_error_t *_error = NULL;

  object->free_func = (hashtable_free_func_t)json_value_free;

  while (true) {
    __auto_type token_type = _json_lex_get_next_token(src, ptr, &_error);
    if (_error)
      goto return_error;

    if (token_type == JSON_TOKEN_RBRACE) {
      break;
    }

    if (token_type == JSON_TOKEN_COMMA) {
      if (object->length == 0) {
        _error = json_error_new(strdup("Leading comma in object"), 0);
        goto return_error;
      }
      continue;
    }

    if (token_type != JSON_TOKEN_STRING) {
      _error = json_error_new(strdup("Expected string key in object"), 0);
      goto return_error;
    }
    const char *end = _get_string_end(*ptr + 1);
    if (!end) {
      _error = json_error_new(strdup("Unterminated string in object key"), 0);
      goto return_error;
    }
    size_t key_length = end - *ptr + 1; // This includes both quotes.
    char *key = decode_json_string(*ptr, key_length, NULL);
    *ptr += key_length;

    token_type = _json_lex_get_next_token(src, ptr, &_error);
    if (token_type != JSON_TOKEN_COLON) {
      _error = json_error_new(strdup("Expected ':' after key in object"), 0);
      free(key);
      goto return_error;
    }

    json_value_t *value = json_parse_token(src, ptr, &_error);
    if (!value) {
      // json_parse_token no longer throws an error on end-of-input, so we need
      // to check for that case here and return a more specific error.
      if (!_error)
        _error = json_error_new(strdup("Unexpected end of input in object"), 0);
      free(key);
      goto return_error;
    }

    hashtable_set_steal(object, key, value);
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

json_value_t *json_parse_string(const char *src, const char **ptr,
                                json_error_t out *error) {
  set_out_value(error, NULL);
  json_error_t *_error = NULL;

  const char *end = _get_string_end(*ptr + 1);
  if (!end) {
    _error = json_error_new(strdup("Unterminated string"), *ptr - src);
    goto return_error;
  }
  size_t length = end - *ptr + 1; // This includes both quotes.
  char *str = decode_json_string(*ptr, length, NULL);
  *ptr += length;

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

json_value_t *json_parse_number(__attribute__((unused)) const char *src,
                                const char **ptr, json_error_t out *error) {
  set_out_value(error, NULL);
  // json_error_t *_error = NULL;

  double value = json_strtod(*ptr, (char **)ptr);

  json_value_t *self = malloc(sizeof(*self));
  *self = (json_value_t){
      .type = JSON_VALUE_TYPE_NUMBER,
      .value.number = value,
  };
  return self;

  // return_error:
  //   set_out_value(error, _error);
  //   if (!error)
  //     json_error_destroy(&_error);
  //   return NULL;
}

json_value_t *json_parse_array(const char *src, const char **ptr,
                               json_error_t out *error) {
  set_out_value(error, NULL);
  array_t *array =
      array_new(json_value_t *, .capacity = DEFAULT_JSON_ARRAY_CAPACITY);
  json_error_t *_error = NULL;

  array->free_func = (array_free_func *)json_value_free;

  while (true) {
    const char *peek = *ptr;
    __auto_type token_type = _json_lex_get_next_token(src, &peek, &_error);
    if (_error)
      goto return_error;

    if (token_type == JSON_TOKEN_RBRACK) {
      *ptr = peek;
      break;
    }

    if (token_type == JSON_TOKEN_COMMA) {
      *ptr = peek;
      if (array->length == 0) {
        _error = json_error_new(strdup("Leading comma in array"), 0);
        goto return_error;
      }
      continue;
    }

    if (token_type == JSON_TOKEN_END) {
      _error = json_error_new(strdup("Unexpected end of input in array"), 0);
      goto return_error;
    }

    // Don't commit the peek — let json_parse_token re-scan and handle the
    // value token from the original position.
    json_value_t *value = json_parse_token(src, ptr, &_error);
    if (!value) {
      if (!_error)
        _error = json_error_new(strdup("Unexpected end of input in array"), 0);
      goto return_error;
    }

    array_push(array, value);
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

  json_error_t *_error = NULL;

  const char **ptr = &src;
  json_value_t *root = json_parse_token(src, ptr, &_error);

  if (_error)
    goto return_error;

  if (!root) {
    _error = json_error_new(strdup("Empty input"), 0);
    goto return_error;
  }

  __auto_type token_type = _json_lex_get_next_token(src, ptr, &_error);
  if (_error || token_type != JSON_TOKEN_END) {
    if (!_error)
      _error =
          json_error_new(strdup("Trailing characters after JSON value"), 0);
    goto return_error;
  }

  if (result)
    *result = root;
  else
    json_value_destroy(&root);
  return true;

return_error:
  if (root)
    json_value_destroy(&root);
  set_out_value(error, _error);
  if (!error)
    json_error_destroy(&_error);
  return false;
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
