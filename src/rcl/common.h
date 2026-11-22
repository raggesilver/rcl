#pragma once

/**
 * Marks a parameter as an out-parameter (`type out name` expands to
 * `type *name`). Purely documentation for the reader.
 *
 * Out-parameters always get a value written to them, even on failure, so they
 * are never left uninitialised. Callers may pass NULL for the ones documented
 * as optional.
 */
#define out *

/**
 * Safely write to an optional out-parameter: does nothing if `ptr` is NULL.
 *
 * @param ptr the out-parameter (may be NULL).
 * @param val the value to store.
 */
#define set_out_value(ptr, val)                                                \
  do {                                                                         \
    if ((ptr))                                                                 \
      *(ptr) = (val);                                                          \
  } while (0)
