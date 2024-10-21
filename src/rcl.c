#include "rcl.h"

/* This function will not be exported and is not
 * directly callable by users of this library.
 */
int internal_function(void) { return 0; }

int rcl_func(void) { return internal_function(); }
