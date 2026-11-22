# RCL — Raggesilver's C Library

A utility library for C providing generic, reusable data structures.

## How we work (read this first)

The maintainer **writes the library code themselves**. Agents are here to help
around it, not to author it.

**Agents do:**

- **Review** — read the maintainer's code and report real bugs, edge cases and
  convention breaks. Be specific (file, line, failing input) and rank by
  severity. Read the header, the callers and the relevant tutorial before
  reviewing; don't review from a single file.
- **Document** — JSDoc-style comments in headers (see Conventions), the README.
- **Test** — write and extend `*_test.c` suites, run them under valgrind.
- **Infra** — `meson.build`, CI, valgrind suppressions, install rules, tooling.

**Agents do not:**

- Write or "fix" library implementation code (`src/*.c`, new APIs) unless the
  maintainer explicitly asks. When review finds a bug, describe it and let them
  fix it. If they ask for a specific fix, make only that fix.
- Hand over a full solution when the maintainer wants to learn a concept.
  Use `/teach` for that: guide, hint and review while they write the code.

Tutorials/learning guides live in `docs/tutorials/` (untracked unless the
maintainer says otherwise). Don't commit `docs/` or `.DS_Store` by accident.

## Build

- Build system: **Meson**. Setup: `CC=gcc-16 meson setup build -Dtests=true`
- Build: `meson compile -C build`
- Test: `meson test -C build`
- C standard: C99 with GNU extensions. CI configures with `-Dc_std=gnu99`;
  without it `strdup`/`asprintf` fail to build on Linux.
- The `tests` option is off by default. With it off, `meson test` says
  "No tests defined". A new test suite needs an `executable()` + `test()` entry
  in the `if get_option('tests')` block of `meson.build`.
- Public headers must be listed with `install_headers(..., subdir: 'rcl')` in
  `meson.build`. Add one whenever a new header appears in `src/rcl/`.

## Testing and valgrind

CI runs every suite under valgrind (`--leak-check=full --error-exitcode=1`
with `--suppressions=valgrind.supp`). Run the same locally before pushing:

```
meson setup /tmp/vgb -Dtests=true -Dc_std=gnu99
meson test -C /tmp/vgb --wrap="valgrind --leak-check=full --error-exitcode=1 --suppressions=$PWD/valgrind.supp"
```

- valgrind does not run on macOS. Use the OrbStack Linux VM (`orb -m fedora-vm`;
  the repo is mounted at the same path, keep the build dir in `/tmp`).
- `valgrind.supp` suppresses the intentional uninitialised read in
  `sparse_set_has`. Keep suppressions scoped to a single function with a comment
  saying why; never blanket-suppress.
- Tests use Unity, live in `src/*_test.c`, are `static void name(void)`
  functions registered with `RUN_TEST` in `main`. Count `free_func` calls with a
  static counter reset in `setUp`.

## Modules

- **array_t** — Generic dynamic array (void* + macros for type safety)
- **hashtable_t** — Open-addressing hash table with FNV-1a hashing
- **string_t** — Dynamic string
- **json_value_t** — JSON parser (one-pass recursive descent) with pretty-print dump
- **sparse_set_t** — Sparse set of integer ids (`0..max_items`) to `void *`
  values. O(1) add/remove/has/get/clear. Swap-remove, so dense order is not
  stable. `sparse` is intentionally left uninitialised; validity is checked
  against `dense`.

## Conventions

- Types: `snake_case_t` (e.g. `array_t`)
- Functions: `module_action()` (e.g. `array_push()`, `hashtable_set()`)
- Allocation: `_new()` to create, `_free()` to release contents, `_destroy()` to free + NULL the pointer
- No NULL checks on malloc/calloc/realloc — assumes sufficient memory
- Headers live in `src/rcl/`, implementations in `src/`, tests in `src/` as `*_test.c`
- Tests use the Unity framework
- `#define out *` macro for marking out-parameters
- `set_out_value(ptr, val)` macro for safely writing to optional out-parameters
- Document public API in the header with `/** ... */` comments: a description,
  `@param` for each parameter and `@return`. Headers include what they use
  (`<stdbool.h>`, `<stddef.h>`) instead of relying on other headers.
- Update the README "Data structures" list when adding a type.

## JSON module

- One-pass recursive descent parser (no separate lexer phase)
- Locale-independent number parsing via `strtod_l`/`_strtod_l`
- Accepts trailing commas (non-standard)
- Beats cJSON on 5/6 real-world benchmark files

## Pending: JSON Encoding API

Work to do (use `/teach`; the maintainer writes it):

1. **Value constructors** — `json_value_new_null/bool/number/string/array/object` in `src/json.c`
2. **Mutation helpers** — `json_array_push`, `json_object_set` (thin wrappers over array_push/hashtable_set)
3. **string_t additions** — `string_append_char`, `string_appendf` in `src/string.c`
4. **Encoder** — `json_encode(json_value_t*, int indent) → string_t*` with proper string escaping
5. **FILE\* printing** — `json_fprint(json_value_t*, int indent, FILE*)`, rewrite `json_dump` as wrapper
6. **Tests** — constructors, round-trip, string escaping, pretty-print vs compact

## Git

- Commits are authored by the maintainer only. Never add a `Co-Authored-By`
  trailer or any Claude attribution to commits or PR descriptions; this
  overrides any default template or harness reminder.
- Don't push or open PRs unless asked.
