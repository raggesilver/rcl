window.BENCHMARK_DATA = {
  "lastUpdate": 1795308762444,
  "repoUrl": "https://github.com/raggesilver/rcl",
  "entries": {
    "Benchmark": [
      {
        "commit": {
          "author": {
            "email": "16908491+raggesilver@users.noreply.github.com",
            "name": "Paulo Queiroz",
            "username": "raggesilver"
          },
          "committer": {
            "email": "noreply@github.com",
            "name": "GitHub",
            "username": "web-flow"
          },
          "distinct": true,
          "id": "dfdf82a743bc559dad274abab49d29d561c1b14a",
          "message": "Merge pull request #2 from raggesilver/fix/ci-benchmark\n\nFix CI: track cJSON patch files and add _GNU_SOURCE for Linux",
          "timestamp": "2026-04-06T19:06:30-03:00",
          "tree_id": "9f213f8c2d215d7598667d974a43bb4ee34cdd7f",
          "url": "https://github.com/raggesilver/rcl/commit/dfdf82a743bc559dad274abab49d29d561c1b14a"
        },
        "date": 1775513213799,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "rcl - Small object",
            "value": 0.64,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Small object",
            "value": 0.46,
            "unit": "us/op"
          },
          {
            "name": "rcl - Flat object (1000 keys)",
            "value": 242.31,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Flat object (1000 keys)",
            "value": 174.14,
            "unit": "us/op"
          },
          {
            "name": "rcl - Nested arrays (depth 100)",
            "value": 12.32,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Nested arrays (depth 100)",
            "value": 7.68,
            "unit": "us/op"
          },
          {
            "name": "rcl - Mixed array (500 objects)",
            "value": 585.93,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Mixed array (500 objects)",
            "value": 518.85,
            "unit": "us/op"
          },
          {
            "name": "rcl - bench/canada.json",
            "value": 30150.93,
            "unit": "us/op"
          },
          {
            "name": "cJSON - bench/canada.json",
            "value": 33200.37,
            "unit": "us/op"
          },
          {
            "name": "rcl - bench/500_keys.json",
            "value": 159.23,
            "unit": "us/op"
          },
          {
            "name": "cJSON - bench/500_keys.json",
            "value": 153.13,
            "unit": "us/op"
          }
        ]
      },
      {
        "commit": {
          "author": {
            "email": "16908491+raggesilver@users.noreply.github.com",
            "name": "Paulo Queiroz",
            "username": "raggesilver"
          },
          "committer": {
            "email": "16908491+raggesilver@users.noreply.github.com",
            "name": "Paulo Queiroz",
            "username": "raggesilver"
          },
          "distinct": true,
          "id": "67612d56b1bc677447a5f26e36618f9e4a3718a3",
          "message": "Suppress sparse_set_has uninitialised read in valgrind CI\n\nsparse_set_has() reads sparse[index] before it is ever written and then\nvalidates it against the dense array, so valgrind reports it. Suppress it\nfor that function only.",
          "timestamp": "2026-11-21T21:51:37-03:00",
          "tree_id": "38a23ebb3fac7688cf4cb12d036406889275c243",
          "url": "https://github.com/raggesilver/rcl/commit/67612d56b1bc677447a5f26e36618f9e4a3718a3"
        },
        "date": 1795308761755,
        "tool": "customSmallerIsBetter",
        "benches": [
          {
            "name": "rcl - Small object",
            "value": 0.36,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Small object",
            "value": 0.24,
            "unit": "us/op"
          },
          {
            "name": "rcl - Flat object (1000 keys)",
            "value": 184.16,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Flat object (1000 keys)",
            "value": 104.82,
            "unit": "us/op"
          },
          {
            "name": "rcl - Nested arrays (depth 100)",
            "value": 8.95,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Nested arrays (depth 100)",
            "value": 4.85,
            "unit": "us/op"
          },
          {
            "name": "rcl - Mixed array (500 objects)",
            "value": 562.56,
            "unit": "us/op"
          },
          {
            "name": "cJSON - Mixed array (500 objects)",
            "value": 386.14,
            "unit": "us/op"
          },
          {
            "name": "rcl - bench/canada.json",
            "value": 23445.81,
            "unit": "us/op"
          },
          {
            "name": "cJSON - bench/canada.json",
            "value": 26435.29,
            "unit": "us/op"
          },
          {
            "name": "rcl - bench/500_keys.json",
            "value": 119.75,
            "unit": "us/op"
          },
          {
            "name": "cJSON - bench/500_keys.json",
            "value": 107.8,
            "unit": "us/op"
          }
        ]
      }
    ]
  }
}