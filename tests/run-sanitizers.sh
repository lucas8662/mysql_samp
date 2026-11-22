#!/usr/bin/env bash
# Builds the plugin and its tests with a sanitizer, then runs ctest.
#
#   tests/run-sanitizers.sh address   # ASan + UBSan, 32-bit like production (GCC)
#   tests/run-sanitizers.sh thread    # ThreadSanitizer, 64-bit test build (Clang)
#
# Set MYSQL_TEST_HOST (and the other MYSQL_TEST_* variables) to include the
# live database tests; otherwise they are reported as skipped.
set -euo pipefail

kind=${1:?usage: $0 address|thread}
root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build/sanitize-$kind
extra=()

case $kind in
  address)
    # alignment: MariaDB Connector/C reads protocol fields unaligned, which
    # is harmless on x86 and not our code.
    flags="-m32 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize=alignment -fno-sanitize-recover=undefined"
    extra+=(-DCMAKE_SHARED_LINKER_FLAGS="$flags")
    ;;
  thread)
    # TSan has no 32-bit runtime, so this build targets the host. GCC 11's
    # TSan misses races (and pthread_cond_clockwait), so use Clang.
    flags="-g -O1 -fsanitize=thread"
    export CC=${CC:-clang} CXX=${CXX:-clang++}
    export TSAN_OPTIONS=${TSAN_OPTIONS:-second_deadlock_stack=1}
    extra+=(-DPLUGIN_NATIVE_ARCH=ON)
    ;;
  *)
    echo "unknown sanitizer: $kind (use address or thread)" >&2
    exit 2
    ;;
esac

cmake -S "$root" -B "$build" -Wno-dev \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DPLUGIN_OUTPUT_DIR="$build/bin" \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_EXE_LINKER_FLAGS="$flags" \
  "${extra[@]}"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
