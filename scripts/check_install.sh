#!/usr/bin/env bash
#
# Report which ESHM pieces are installed on this machine and what each one
# enables. Answers "did the install work?" and "why can't I compile/import?".
#
#   scripts/check_install.sh                 # check the system
#   scripts/check_install.sh --root DIR      # check a staged/extracted tree
#   scripts/check_install.sh --prefix /usr/local   # source install location
#
set -uo pipefail

ROOT=""
PREFIX="/usr"

while [ $# -gt 0 ]; do
    case "$1" in
        --root)   ROOT="${2%/}"; shift 2 ;;
        --prefix) PREFIX="${2%/}"; shift 2 ;;
        -h|--help) sed -n '2,/^set -uo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//; $d'; exit 0 ;;
        *) echo "unknown option '$1'" >&2; exit 1 ;;
    esac
done

if [ -t 1 ]; then GREEN=$'\033[32m'; RED=$'\033[31m'; DIM=$'\033[2m'; BOLD=$'\033[1m'; RESET=$'\033[0m'
else GREEN=""; RED=""; DIM=""; BOLD=""; RESET=""; fi

MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || echo x86_64-linux-gnu)"
LIBDIRS=("$ROOT$PREFIX/lib/$MULTIARCH" "$ROOT$PREFIX/lib" "$ROOT/usr/local/lib")
missing=0

found_in() {   # $1 = filename; echoes the first directory that has it
    local name="$1" dir
    for dir in "${LIBDIRS[@]}"; do
        [ -e "$dir/$name" ] && { echo "$dir/$name"; return 0; }
    done
    return 1
}

report() {     # $1 = ok/no, $2 = label, $3 = detail
    if [ "$1" = ok ]; then
        printf '  %s✓%s %-28s %s%s%s\n' "$GREEN" "$RESET" "$2" "$DIM" "$3" "$RESET"
    else
        printf '  %s✗%s %-28s %s\n' "$RED" "$RESET" "$2" "$3"
        missing=$((missing + 1))
    fi
}

echo "${BOLD}Packages${RESET}"
if command -v dpkg-query >/dev/null && [ -z "$ROOT" ]; then
    for pkg in libeshm1 libeshm-dev python3-eshm; do
        # Check Status, not just Version: `apt remove` without purge leaves the
        # package in state "deinstall ok config-files" (rc), which still
        # reports a version even though every file is gone.
        entry="$(dpkg-query -W -f='${Status}|${Version}' "$pkg" 2>/dev/null)"
        status="${entry%%|*}"
        ver="${entry##*|}"
        case "$status" in
            "install ok installed") report ok "$pkg" "version $ver" ;;
            "deinstall ok config-files")
                report no "$pkg" "removed but not purged (dpkg 'rc') - sudo apt purge $pkg" ;;
            *) report no "$pkg" "not installed" ;;
        esac
    done
else
    echo "  ${DIM}(skipped: checking a tree, not the package database)${RESET}"
fi

echo
echo "${BOLD}Runtime - needed to RUN programs built against ESHM${RESET}"
if lib="$(found_in libeshm.so.1)"; then
    report ok "libeshm.so.1" "$lib"
else
    report no "libeshm.so.1" "install the 'libeshm1' package"
fi
if lib="$(found_in libeshm_data.so.1)"; then
    report ok "libeshm_data.so.1" "$lib"
else
    report no "libeshm_data.so.1" "install the 'libeshm1' package"
fi
if lib="$(found_in libeshm_cuda.so.1)"; then
    report ok "libeshm_cuda.so.1" "$lib (GPU VRAM sharing - dlopen's libcuda.so.1 lazily, works with no GPU present)"
else
    printf '  %s-%s %-28s %s\n' "$DIM" "$RESET" "libeshm_cuda.so.1" \
        "${DIM}not built (optional: needs a CUDA toolkit at build time, export_deb.sh --cuda ON|AUTO)${RESET}"
fi
if [ -z "$ROOT" ]; then
    # Capture first: `grep -q` exits early, and with pipefail that SIGPIPE
    # would look like "not found".
    ld_cache="$(ldconfig -p 2>/dev/null || true)"
    if [ "${ld_cache#*libeshm.so}" != "$ld_cache" ]; then
        report ok "linker cache" "ldconfig can resolve libeshm"
    else
        report no "linker cache" "run: sudo ldconfig"
    fi
fi

echo
echo "${BOLD}Development - needed to BUILD against ESHM${RESET}"
if [ -f "$ROOT$PREFIX/include/eshm.h" ]; then
    report ok "headers" "$ROOT$PREFIX/include/eshm.h"
else
    report no "headers" "install the 'libeshm-dev' package"
fi
if lib="$(found_in libeshm.so)"; then
    report ok "libeshm.so symlink" "$lib"
else
    report no "libeshm.so symlink" "install the 'libeshm-dev' package (needed by -leshm)"
fi
if cfg="$(found_in cmake/ESHM/ESHMConfig.cmake)"; then
    report ok "CMake package" "find_package(ESHM 1.2 REQUIRED)  # 1.0/1.1 also work (SameMajorVersion); use the version you actually rely on"
else
    report no "CMake package" "install the 'libeshm-dev' package"
fi
if [ -f "$ROOT$PREFIX/include/eshm_cuda.h" ]; then
    report ok "eshm_cuda.h" "$ROOT$PREFIX/include/eshm_cuda.h"
elif lib="$(found_in libeshm_cuda.so.1)"; then
    report no "eshm_cuda.h" "libeshm_cuda.so.1 is installed but the header is not - install 'libeshm-dev'"
else
    printf '  %s-%s %-28s %s\n' "$DIM" "$RESET" "eshm_cuda.h" \
        "${DIM}not built (optional GPU VRAM sharing header)${RESET}"
fi

echo
echo "${BOLD}Python - needed for 'from eshm import ESHM'${RESET}"
PYDIR="$ROOT$PREFIX/lib/python3/dist-packages/eshm"
if [ -f "$PYDIR/__init__.py" ]; then
    report ok "python3-eshm files" "$PYDIR"
    if [ -z "$ROOT" ] && python3 -c 'import eshm' 2>/dev/null; then
        report ok "import eshm" "$(python3 -c 'import eshm; print(eshm.library_path())' 2>/dev/null)"
    elif [ -z "$ROOT" ]; then
        report no "import eshm" "files present but import failed: $(python3 -c 'import eshm' 2>&1 | tail -1)"
    fi
else
    report no "python3-eshm files" "install the 'python3-eshm' package"
fi

echo
echo "${BOLD}Tools${RESET}"
for tool in eshm_demo test_selftest; do
    if [ -x "$ROOT$PREFIX/bin/$tool" ]; then
        report ok "$tool" "$ROOT$PREFIX/bin/$tool"
    else
        printf '  %s-%s %-28s %s\n' "$DIM" "$RESET" "$tool" \
            "${DIM}not installed (optional: export_deb.sh --with-tools)${RESET}"
    fi
done

echo
if [ "$missing" -eq 0 ]; then
    echo "${GREEN}Everything checked is present.${RESET}"
else
    echo "$missing item(s) missing - see the hints above."
fi
