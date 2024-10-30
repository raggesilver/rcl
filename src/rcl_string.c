#include "rcl_string.h"

#include <stdlib.h>
#include <string.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

string_t *string_new_steal(char **str) {
  string_t *self = malloc(sizeof(*self));
  self->length = strlen(*str);
  self->capacity = self->length;
  self->data = *str;
  *str = NULL;
  return self;
}

string_t *string_new(const char *str) {
  char *temp = strdup(str);
  return string_new_steal(&temp);
}

void string_free(string_t *self) {
  free(self->data);
  free(self);
}

void string_destroy(string_t **self) {
  if (self == NULL || *self == NULL)
    return;

  string_free(*self);
  *self = NULL;
}

static inline void string_ensure_capacity(string_t *self, size_t minimum) {
  if (self->capacity < minimum) {
    self->capacity = MAX(self->capacity * 2, minimum);
    self->data = realloc(self->data, self->capacity);
  }
}

void string_append_str(string_t *self, const char *str) {
  size_t len = strlen(str);
  if (self->length + len >= self->capacity) {
    string_ensure_capacity(self, self->length + len + 1);
  }
  strncat(self->data + self->length, str, len);
  self->length += len;
}

void string_append(string_t *self, string_t *other) {
  if (self->length + other->length >= self->capacity) {
    string_ensure_capacity(self, self->length + other->length + 1);
  }
  strncat(self->data + self->length, other->data, other->length);
  self->length += other->length;
}

void string_prepend_str(string_t *self, const char *str) {
  size_t len = strlen(str);
  if (self->length + len >= self->capacity) {
    string_ensure_capacity(self, self->length + len + 1);
  }
  memmove(self->data + len, self->data, self->length);
  memcpy(self->data, str, len);
  self->length += len;
  self->data[self->length] = '\0';
}

void string_prepend(string_t *self, string_t *other) {
  if (self->length + other->length >= self->capacity) {
    string_ensure_capacity(self, self->length + other->length + 1);
  }
  memmove(self->data + other->length, self->data, self->length);
  memcpy(self->data, other->data, other->length);
  self->length += other->length;
  self->data[self->length] = '\0';
}

void string_clear(string_t *self) {
  self->length = 0;
  self->data[0] = '\0';
}

void string_reverse(string_t *self) {
  for (size_t i = 0; i < self->length / 2; i++) {
    char temp = self->data[i];
    self->data[i] = self->data[self->length - i - 1];
    self->data[self->length - i - 1] = temp;
  }
}
