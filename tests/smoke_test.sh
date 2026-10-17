#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

input_file="$test_dir/input.txt"
actual_file="$test_dir/actual.txt"
expected_file="$test_dir/expected.txt"
redirect_file="$test_dir/redirection.txt"

{
    printf '%s\n' 'echo "hello world"'
    printf '%s\n' "echo 'operators ; && || | stay quoted'"
    printf '%s\n' 'echo escaped\ space'
    printf 'echo compact>%s\n' "$redirect_file"
    printf 'echo append>>%s\n' "$redirect_file"
    printf 'cat<%s\n' "$redirect_file"
    printf '%s\n' 'printf abc|wc -c'
    printf '%s\n' 'false && echo should-not-run'
    printf '%s\n' 'false || echo recovered'
    printf '%s\n' 'true && echo continued'
    printf '%s\n' 'echo alpha | tr a-z A-Z'
    printf '%s\n' 'search main.cpp'
    printf '%s\n' 'exit'
} > "$input_file"

cat > "$expected_file" <<'EOF'
hello world
operators ; && || | stay quoted
escaped space
compact
append
3
recovered
continued
ALPHA
True
EOF

HOME="$test_dir" "$project_dir/myshell" < "$input_file" > "$actual_file"
diff -u "$expected_file" "$actual_file"
printf '%s\n' 'All smoke tests passed.'
