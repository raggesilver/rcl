#pragma once

#include <stddef.h>

typedef struct s_string {
  size_t length;
  size_t capacity;
  char *data;
} string_t;

string_t *string_new(const char *str);

/**
 * Create a new string_t object from a given string. This function will use the
 * given string as the internal buffer without copying it, then set it to NULL.
 */
string_t *string_new_steal(char **str);

void string_free(string_t *self);

void string_destroy(string_t **self);

void string_append_str(string_t *self, const char *str);

void string_append(string_t *self, string_t *other);

void string_prepend_str(string_t *self, const char *str);

void string_prepend(string_t *self, string_t *other);

/**
 * Clear the string, but keep the allocated memory.
 */
void string_clear(string_t *self);

/**
 * Reverse the string in place.
 */
void string_reverse(string_t *self);
