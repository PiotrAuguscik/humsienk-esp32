#!/usr/bin/env bash
set -euo pipefail
project_dir=$(cd "$(dirname "$0")/.." && pwd)
test_dir=$(mktemp -d /tmp/humsienk-tests.XXXXXX)
g++ -std=c++11 -Wall -Wextra -Wpedantic -Werror -fsanitize=undefined \
  -fno-omit-frame-pointer -I "$project_dir/src" \
  "$project_dir/src/humsienk.cpp" "$project_dir/tests/test_main.cpp" \
  -o "$test_dir/test"
"$test_dir/test"
