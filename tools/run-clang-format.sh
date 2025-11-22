#!/usr/bin/env bash
set -euo pipefail

# Run clang-format with the repository rules, skipping files listed in .clang-format-ignore.

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

ignore_file=".clang-format-ignore"

# Collect C/C++ sources and headers in include/ and main/
mapfile -t files < <(find include main -type f \( -name "*.c" -o -name "*.h" \))

# Filter out ignored paths if the ignore file exists
if [[ -f "$ignore_file" ]]; then
  mapfile -t ignored <"$ignore_file"
  if [[ ${#ignored[@]} -gt 0 ]]; then
    filtered=()
    for f in "${files[@]}"; do
      skip=false
      for pattern in "${ignored[@]}"; do
        [[ -z "$pattern" ]] && continue
        if [[ "$f" == $pattern ]]; then
          skip=true
          break
        fi
      done
      $skip || filtered+=("$f")
    done
    files=("${filtered[@]}")
  fi
fi

if [[ ${#files[@]} -eq 0 ]]; then
  echo "No source files found for clang-format."
  exit 0
fi

# --dry-run + --Werror so CI fails on formatting regressions without modifying files.
clang-format --style=file --dry-run --Werror "${files[@]}"
