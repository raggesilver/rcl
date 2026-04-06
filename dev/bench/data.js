window.BENCHMARK_DATA = {
  "lastUpdate": 1775513214198,
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
      }
    ]
  }
}