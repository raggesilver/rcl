#include "rcl/json.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char *read_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "Could not open file: %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(len + 1);
  fread(buf, 1, len, f);
  buf[len] = '\0';
  fclose(f);
  return buf;
}

static double time_diff_us(struct timespec start, struct timespec end) {
  return (end.tv_sec - start.tv_sec) * 1e6 +
         (end.tv_nsec - start.tv_nsec) / 1e3;
}

typedef struct {
  const char *name;
  const char *unit;
  double value;
} bench_entry_t;

// Dynamic array of results
static bench_entry_t *g_results = NULL;
static int g_results_count = 0;
static int g_results_cap = 0;

static void add_result(const char *name, const char *unit, double value) {
  if (g_results_count >= g_results_cap) {
    g_results_cap = g_results_cap ? g_results_cap * 2 : 32;
    g_results = realloc(g_results, g_results_cap * sizeof(bench_entry_t));
  }
  g_results[g_results_count++] =
      (bench_entry_t){.name = name, .unit = unit, .value = value};
}

static void print_json_string(const char *s) {
  putchar('"');
  for (; *s; s++) {
    switch (*s) {
    case '"':
      printf("\\\"");
      break;
    case '\\':
      printf("\\\\");
      break;
    case '\n':
      printf("\\n");
      break;
    default:
      putchar(*s);
    }
  }
  putchar('"');
}

static void print_results(void) {
  printf("[\n");
  for (int i = 0; i < g_results_count; i++) {
    printf("  {\"name\": ");
    print_json_string(g_results[i].name);
    printf(", \"unit\": ");
    print_json_string(g_results[i].unit);
    printf(", \"value\": %.2f}", g_results[i].value);
    if (i + 1 < g_results_count)
      putchar(',');
    putchar('\n');
  }
  printf("]\n");
}

static void run_bench(const char *label, const char *src, int iterations) {
  struct timespec start, end;

  // rcl
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < iterations; i++) {
    json_value_t *val = NULL;
    json_parse_safe(src, &val, NULL);
    json_value_free(val);
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  double rcl_us = time_diff_us(start, end) / iterations;

  // cJSON
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < iterations; i++) {
    cJSON *val = cJSON_Parse(src);
    cJSON_Delete(val);
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  double cjson_us = time_diff_us(start, end) / iterations;

  // Build names like "rcl - Small object"
  char rcl_name[256], cjson_name[256];
  snprintf(rcl_name, sizeof(rcl_name), "rcl - %s", label);
  snprintf(cjson_name, sizeof(cjson_name), "cJSON - %s", label);

  // strdup so the pointers stay valid
  add_result(strdup(rcl_name), "us/op", rcl_us);
  add_result(strdup(cjson_name), "us/op", cjson_us);
}

// Generate a JSON string with many key-value pairs
static char *generate_flat_object(int num_keys) {
  size_t cap = 64 + num_keys * 40;
  char *buf = malloc(cap);
  size_t pos = 0;
  pos += snprintf(buf + pos, cap - pos, "{");
  for (int i = 0; i < num_keys; i++) {
    if (i > 0)
      pos += snprintf(buf + pos, cap - pos, ",");
    pos += snprintf(buf + pos, cap - pos, "\"key_%d\":%d", i, i);
  }
  pos += snprintf(buf + pos, cap - pos, "}");
  return buf;
}

// Generate a deeply nested JSON array
static char *generate_nested_array(int depth) {
  size_t cap = depth * 4 + 16;
  char *buf = malloc(cap);
  size_t pos = 0;
  for (int i = 0; i < depth; i++)
    pos += snprintf(buf + pos, cap - pos, "[");
  pos += snprintf(buf + pos, cap - pos, "1");
  for (int i = 0; i < depth; i++)
    pos += snprintf(buf + pos, cap - pos, "]");
  return buf;
}

// Generate a JSON array of mixed-type objects
static char *generate_mixed_array(int count) {
  size_t cap = 64 + count * 120;
  char *buf = malloc(cap);
  size_t pos = 0;
  pos += snprintf(buf + pos, cap - pos, "[");
  for (int i = 0; i < count; i++) {
    if (i > 0)
      pos += snprintf(buf + pos, cap - pos, ",");
    pos += snprintf(buf + pos, cap - pos,
                    "{\"id\":%d,\"name\":\"item_%d\","
                    "\"active\":%s,\"value\":%.2f,\"tags\":[\"a\",\"b\"]}",
                    i, i, i % 2 ? "true" : "false", i * 1.5);
  }
  pos += snprintf(buf + pos, cap - pos, "]");
  return buf;
}

// Run a single parser on a file (for memory profiling with /usr/bin/time -l)
static void run_single(const char *parser, const char *path, int iterations) {
  char *src = read_file(path);
  if (strcmp(parser, "rcl") == 0) {
    for (int i = 0; i < iterations; i++) {
      json_value_t *val = NULL;
      json_parse_safe(src, &val, NULL);
      json_value_free(val);
    }
  } else if (strcmp(parser, "cjson") == 0) {
    for (int i = 0; i < iterations; i++) {
      cJSON *val = cJSON_Parse(src);
      cJSON_Delete(val);
    }
  } else {
    fprintf(stderr, "Unknown parser: %s\n", parser);
    exit(1);
  }
  free(src);
}

int main(int argc, char **argv) {
  // --parser <name> <file> [iterations] — run a single parser (for memory
  // profiling)
  if (argc >= 4 && strcmp(argv[1], "--parser") == 0) {
    int iterations = argc > 4 ? atoi(argv[4]) : 1;
    run_single(argv[2], argv[3], iterations);
    return 0;
  }

  // If a file path is provided, benchmark that file
  if (argc > 1) {
    char *src = read_file(argv[1]);
    int iterations = argc > 2 ? atoi(argv[2]) : 20;
    run_bench(argv[1], src, iterations);
    free(src);
    print_results();
    free(g_results);
    return 0;
  }

  // Otherwise, run synthetic benchmarks
  char *small = "{\"name\":\"test\",\"value\":42,\"active\":true}";
  run_bench("Small object", small, 100000);

  char *flat = generate_flat_object(1000);
  run_bench("Flat object (1000 keys)", flat, 1000);

  char *nested = generate_nested_array(100);
  run_bench("Nested arrays (depth 100)", nested, 50000);

  char *mixed = generate_mixed_array(500);
  run_bench("Mixed array (500 objects)", mixed, 1000);

  free(flat);
  free(nested);
  free(mixed);

  print_results();
  free(g_results);

  return 0;
}
