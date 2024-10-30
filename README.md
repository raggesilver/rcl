<div align="center">

<h1>RCL — Raggesilver's C Library</h1>

<img src="https://github.com/raggesilver/rcl/actions/workflows/ci.yml/badge.svg" alt="CI Status" />

</div>

This is a collection of useful functions and data structures I use in my C
projects. I do not have the time to write full documentation, although all
functions are documented in their header files. I don't recommend using this
library unless you know what you're doing.

## Data structures

- `hashtable_t` — Decently fast open-addressing, double-hashing implementation.
- `array_t` — Generic, dynamic array. Includes helper functions for accessing
  `arr->data` with any type.
- `string_t` — String. Includes the basic stuff you'd expect from a string
  type in a modern language.
- `list_t` — Linked list. (coming soon)
- `string_builder_t` — A string builder. Useful for building one string out of
  many, without reallocating a string on each concatenation. (coming soon)

## Functions

- `read_line` — Read a line from a file descriptor. Keeps an internal buffer
  for each file descriptor for consecutive calls. (coming soon)
- `read_word` — Read the next word (space-separated) from a file descriptor.
  Keeps an internal buffer for each file descriptor for consecutive calls.
  (coming soon)
