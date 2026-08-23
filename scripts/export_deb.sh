#!/usr/bin/env bash
#
# Export ESHM as Debian packages, split the way Debian Policy expects:
#
#   libeshm1      runtime     - the shared libraries only, what you deploy
#   libeshm-dev   development - headers, .so symlinks and the CMake package files,
#                               so `find_package(ESHM)` works; depends on libeshm1
#   python3-eshm  Python      - `from eshm import ESHM`; depends on libeshm1
#
# The packages are built from a staged `cmake --install`, so they contain
# exactly what installing from source would put on the system.
#
#   scripts/export_deb.sh                     # all three packages into dist/
#   scripts/export_deb.sh --only dev          # just libeshm-dev
#   scripts/export_deb.sh --with-tools        # add eshm_demo + test_selftest
#   scripts/export_deb.sh -v 1.1.0 -m "Me <me@example.com>"
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

NAME="libeshm1"
VERSION=""
MAINTAINER="${DEBFULLNAME:-ESHM Maintainer} <${DEBEMAIL:-maintainer@example.com}>"
HOMEPAGE="https://github.com/chongmx/eshm"
OUTPUT="$REPO_ROOT/dist"
BUILD_DIR="$REPO_ROOT/build-deb"
PREFIX="/usr"
ONLY="all"
WITH_TOOLS=0
RUN_TESTS=1
CLEAN=0
DATA_SIZE=""
CUDA="AUTO"
JOBS="$(nproc 2>/dev/null || echo 2)"

usage() {
    sed -n '2,/^set -euo/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//; $d'
    cat <<EOF
Options:
  -n, --name NAME        runtime package name (default: $NAME; dev package: NAME-dev)
  -v, --version VER      package version (default: read from CMakeLists.txt)
  -m, --maintainer STR   Maintainer field (default: \$DEBFULLNAME <\$DEBEMAIL>)
  -o, --output DIR       where to write the .deb files (default: dist/)
  -b, --build-dir DIR    build directory (default: build-deb/)
      --prefix DIR       install prefix baked into the packages (default: /usr)
      --only WHICH       runtime | dev | python | all (default: all)
      --with-tools       also ship test_eshm and the Python example (runtime package)
      --data-size N      build with -DESHM_MAX_DATA_SIZE=N
      --cuda WHICH       AUTO | ON | OFF - GPU VRAM sharing module (default: AUTO,
                         same as the library's own default). libeshm_cuda has no
                         hard runtime dependency on the NVIDIA driver (it is
                         dlopen'd lazily) and rides in the same libeshm1/-dev
                         packages when built - OFF only matters if you want a
                         build that does not compile the GPU code in at all.
      --skip-tests       do not run ctest before packaging
      --clean            remove the build directory first
  -j, --jobs N           parallel build jobs (default: $JOBS)
  -h, --help             this message
EOF
}

if [ -t 1 ]; then BOLD=$'\033[1m'; RED=$'\033[31m'; YELLOW=$'\033[33m'; RESET=$'\033[0m'
else BOLD=""; RED=""; YELLOW=""; RESET=""; fi

die() { printf '%serror:%s %s\n' "$RED" "$RESET" "$*" >&2; exit 1; }
info() { printf '%s==>%s %s\n' "$BOLD" "$RESET" "$*"; }
warn() { printf '%swarning:%s %s\n' "$YELLOW" "$RESET" "$*" >&2; }

while [ $# -gt 0 ]; do
    case "$1" in
        -n|--name)        NAME="${2:?}"; shift 2 ;;
        -v|--version)     VERSION="${2:?}"; shift 2 ;;
        -m|--maintainer)  MAINTAINER="${2:?}"; shift 2 ;;
        -o|--output)      OUTPUT="${2:?}"; shift 2 ;;
        -b|--build-dir)   BUILD_DIR="${2:?}"; shift 2 ;;
        --prefix)         PREFIX="${2:?}"; shift 2 ;;
        --only)           ONLY="${2:?}"; shift 2 ;;
        --data-size)      DATA_SIZE="${2:?}"; shift 2 ;;
        --cuda)           CUDA="${2:?}"; shift 2 ;;
        -j|--jobs)        JOBS="${2:?}"; shift 2 ;;
        --with-tools)     WITH_TOOLS=1; shift ;;
        --skip-tests)     RUN_TESTS=0; shift ;;
        --clean)          CLEAN=1; shift ;;
        -h|--help)        usage; exit 0 ;;
        *=*)              set -- "${1%%=*}" "${1#*=}" "${@:2}" ;;   # --opt=value
        *)                die "unknown option '$1' (try --help)" ;;
    esac
done

case "$ONLY" in runtime|dev|python|all|both) ;;
    *) die "--only takes runtime, dev, python or all" ;;
esac
[ "$ONLY" = "both" ] && ONLY="all"   # accepted spelling for runtime + dev + python

case "$CUDA" in AUTO|ON|OFF) ;;
    *) die "--cuda takes AUTO, ON or OFF" ;;
esac

# Policy names a runtime library package after its SONAME (libeshm.so.1 ->
# libeshm1), with <libname>-dev beside it.
case "$NAME" in
    libeshm1) DEV_NAME="libeshm-dev" ;;
    *)        DEV_NAME="${NAME}-dev" ;;
esac
PY_NAME="python3-eshm"

# Read the default version from project(...) rather than the CMake cache, which
# would otherwise keep a --version from an earlier run of this script.
if [ -z "$VERSION" ]; then
    VERSION="$(sed -n 's/^project(ESHM VERSION \([0-9][0-9.]*\).*/\1/p' "$REPO_ROOT/CMakeLists.txt")"
    [ -n "$VERSION" ] || die "could not determine the version from CMakeLists.txt - pass --version"
fi

# --------------------------------------------------------------------------
# Prerequisites
# --------------------------------------------------------------------------
for tool in cmake dpkg-deb dpkg; do
    command -v "$tool" >/dev/null || die "$tool is required (sudo apt install cmake dpkg-dev)"
done
[ -f "$REPO_ROOT/CMakeLists.txt" ] || die "run this from the ESHM source tree"

ARCH="$(dpkg --print-architecture)"

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
[ "$CLEAN" -eq 1 ] && rm -rf "$BUILD_DIR"

info "Configuring $NAME $VERSION ($BUILD_DIR, prefix $PREFIX)"
cmake_args=(
    -S "$REPO_ROOT" -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
    -DESHM_INSTALL_PYTHON=ON
    -DESHM_BUILD_TESTS="$([ "$RUN_TESTS" -eq 1 ] || [ "$WITH_TOOLS" -eq 1 ] && echo ON || echo OFF)"
    -DESHM_BUILD_DEMO="$([ "$WITH_TOOLS" -eq 1 ] && echo ON || echo OFF)"
    -DESHM_BUILD_EXAMPLES=OFF
    -DESHM_ENABLE_CUDA="$CUDA"
)
[ -n "$DATA_SIZE" ] && cmake_args+=(-DESHM_MAX_DATA_SIZE="$DATA_SIZE")
cmake "${cmake_args[@]}" > /dev/null

info "Building (-j$JOBS)"
cmake --build "$BUILD_DIR" -j"$JOBS" > /dev/null

if [ "$RUN_TESTS" -eq 1 ]; then
    info "Running the test suite"
    ctest --test-dir "$BUILD_DIR" --output-on-failure > /dev/null ||
        die "tests failed - packaging aborted (use --skip-tests to override)"
fi

# --------------------------------------------------------------------------
# Stage `cmake --install` and split it into package roots
# --------------------------------------------------------------------------
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
STAGE="$WORK/stage"

info "Staging the install tree"
DESTDIR="$STAGE" cmake --install "$BUILD_DIR" > /dev/null

# Runtime: the versioned shared libraries (plus the tools, if asked for).
# Development: headers, the unversioned .so symlinks and the CMake package.
( cd "$STAGE" && find . \( -name '*.so.*' -o -path '*/bin/*' -o -path '*/share/doc/eshm/*' \) \
    -mindepth 1 | sed 's|^\./||' | sort ) > "$WORK/runtime.list"
( cd "$STAGE" && find . -path '*/dist-packages/*' -mindepth 1 \
    | sed 's|^\./||' | sort ) > "$WORK/python.list"
( cd "$STAGE" && find . \( -path '*/include/*' -o -path '*/cmake/*' -o -name '*.so' \) -mindepth 1 \
    | sed 's|^\./||' | sort ) > "$WORK/dev.list"

[ -s "$WORK/runtime.list" ] || die "nothing staged for the runtime package"
[ -s "$WORK/dev.list" ] || die "nothing staged for the dev package"

# Nothing installed may fall outside the three package lists unnoticed.
( cd "$STAGE" && find . ! -type d | sed 's|^\./||' | sort ) > "$WORK/all.list"
cat "$WORK/runtime.list" "$WORK/dev.list" "$WORK/python.list" | sort -u > "$WORK/assigned.list"
if [ -n "$(comm -23 "$WORK/all.list" "$WORK/assigned.list")" ]; then
    warn "staged files that no package claims:"
    comm -23 "$WORK/all.list" "$WORK/assigned.list" | sed 's|^|    |' >&2
fi

# Copy a file list into a package root, preserving symlinks, with Debian modes:
# directories 0755, regular files 0644, executables in bin/ 0755.
build_root() {
    local list="$1" root="$2" rel
    while IFS= read -r rel; do
        [ -d "$STAGE/$rel" ] && continue
        mkdir -p "$root/$(dirname "$rel")"
        cp -a "$STAGE/$rel" "$root/$rel"
    done < "$list"
    find "$root" -type d -exec chmod 755 {} +
    find "$root" -type f -exec chmod 644 {} +
    [ -d "$root/${PREFIX#/}/bin" ] && chmod 755 "$root/${PREFIX#/}/bin/"* || true
}

# Every binary package must ship /usr/share/doc/<pkg>/{copyright,changelog.Debian.gz}
install_docs() {
    local root="$1" pkg="$2"
    local docdir="$root${PREFIX}/share/doc/$pkg"   # separate statement: bash does
    mkdir -p "$docdir"                             # not expand $root within one `local`

    # cmake --install puts LICENSE/README in share/doc/eshm; Debian wants them
    # under the binary package's own directory.
    if [ -d "$root${PREFIX}/share/doc/eshm" ] && [ "$pkg" != "eshm" ]; then
        mv "$root${PREFIX}/share/doc/eshm/"* "$docdir/" 2>/dev/null || true
        rmdir "$root${PREFIX}/share/doc/eshm" 2>/dev/null || true
    fi

    install -m 644 "$REPO_ROOT/packaging/copyright" "$docdir/copyright"
    {
        echo "eshm ($VERSION) unstable; urgency=medium"
        echo
        echo "  * Package built from the ESHM source tree by scripts/export_deb.sh."
        echo
        echo " -- $MAINTAINER  $(date -R)"
    } > "$docdir/changelog.Debian"
    gzip -9n -f "$docdir/changelog.Debian"
    chmod 644 "$docdir/changelog.Debian.gz"
}

# Write DEBIAN/control, md5sums and (for the runtime) the ldconfig hooks.
write_metadata() {
    local root="$1" pkg="$2" section="$3" depends="$4" summary="$5" body="$6" extra="${7:-}"
    mkdir -p "$root/DEBIAN"
    {
        echo "Package: $pkg"
        echo "Version: $VERSION"
        echo "Section: $section"
        echo "Priority: optional"
        echo "Architecture: $ARCH"
        echo "Maintainer: $MAINTAINER"
        [ -n "$depends" ] && echo "Depends: $depends"
        echo "Homepage: $HOMEPAGE"
        [ -n "$extra" ] && printf '%s\n' "$extra"
        echo "Installed-Size: $(du -ks --exclude=DEBIAN "$root" | cut -f1)"
        echo "Description: $summary"
        printf '%s\n' "$body" | sed 's/^/ /'
    } > "$root/DEBIAN/control"

    ( cd "$root" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
        | sort -z | xargs -0 --no-run-if-empty md5sum > DEBIAN/md5sums )
    chmod 644 "$root/DEBIAN/control" "$root/DEBIAN/md5sums"
}

# Runtime dependencies straight from the ELF files. The staged binaries carry no
# RPATH, so -l must point at the package's own libraries for the sibling
# libeshm_data.so.1 to resolve.
shlibdeps_for() {
    local root="$1"; shift
    local tmp="$WORK/shlibdeps" out dir
    local libdirs=()
    rm -rf "$tmp"; mkdir -p "$tmp/debian"
    printf 'Source: %s\n\nPackage: %s\nArchitecture: any\nDepends: ${shlibs:Depends}\n' \
        "$NAME" "$NAME" > "$tmp/debian/control"
    while IFS= read -r dir; do libdirs+=("-l$dir"); done \
        < <(find "$root" -name '*.so.*' -printf '%h\n' | sort -u)
    if command -v dpkg-shlibdeps >/dev/null &&
       out="$( cd "$tmp" && dpkg-shlibdeps -O --ignore-missing-info "${libdirs[@]}" "$@" 2>/dev/null )"; then
        # Drop a self-dependency: when this package is already installed,
        # dpkg-shlibdeps resolves our own libraries through it.
        printf '%s' "${out#shlibs:Depends=}" \
            | sed -E "s/(^|, )$NAME \\([^)]*\\)//g; s/^, //; s/, $//"
    else
        warn "dpkg-shlibdeps unavailable or failed - falling back to a generic dependency list"
        printf 'libc6, libgcc-s1, libstdc++6'
    fi
}

mkdir -p "$OUTPUT"
built=()

if [ "$ONLY" = "runtime" ] || [ "$ONLY" = "all" ]; then
    info "Packaging $NAME (runtime)"
    ROOT="$WORK/root-runtime"
    build_root "$WORK/runtime.list" "$ROOT"

    mapfile -t elfs < <(find "$ROOT" -type f \( -name '*.so.*' -o -path '*/bin/*' \))
    deps="$(shlibdeps_for "$ROOT" "${elfs[@]}")"

    body="Master/slave IPC over POSIX shared memory with lock-free sequence locks,
heartbeat based stale detection and automatic reconnection.
This package ships the runtime libraries needed to run programs built
against ESHM. Install ${DEV_NAME} to build against it."
    [ "$WITH_TOOLS" -eq 1 ] && body="$body
It also ships eshm_demo and test_selftest for verifying an installation."

    install_docs "$ROOT" "$NAME"
    multiarch=""
    [ "$WITH_TOOLS" -eq 0 ] && multiarch="Multi-Arch: same"   # invalid once /usr/bin is in the package
    write_metadata "$ROOT" "$NAME" "libs" "$deps" \
        "ESHM shared memory IPC library" "$body" "$multiarch"
    # Policy 8.1.1: activate libc's ldconfig trigger (dpkg runs it on install
    # and removal), rather than calling ldconfig from maintainer scripts.
    install -m 644 "$REPO_ROOT/packaging/triggers" "$ROOT/DEBIAN/triggers"

    # Policy 8.6: a shlibs file lets dpkg-shlibdeps generate a dependency on
    # this package for anything that links against these libraries.
    {
        echo "libeshm 1 $NAME (>= $VERSION)"
        echo "libeshm_data 1 $NAME (>= $VERSION)"
    } > "$ROOT/DEBIAN/shlibs"
    chmod 644 "$ROOT/DEBIAN/shlibs"

    deb="$OUTPUT/${NAME}_${VERSION}_${ARCH}.deb"
    dpkg-deb --root-owner-group --build "$ROOT" "$deb" > /dev/null
    built+=("$deb")
fi

if [ "$ONLY" = "dev" ] || [ "$ONLY" = "all" ]; then
    info "Packaging $DEV_NAME (development)"
    ROOT="$WORK/root-dev"
    build_root "$WORK/dev.list" "$ROOT"

    install_docs "$ROOT" "$DEV_NAME"
    write_metadata "$ROOT" "$DEV_NAME" "libdevel" "${NAME} (= ${VERSION})" \
        "ESHM shared memory IPC library - development files" \
        "Headers, the unversioned .so symlinks and the ESHM CMake package files.
Install this to build software against ESHM:
 .
   find_package(ESHM 1.0 REQUIRED)
   target_link_libraries(my_app PRIVATE ESHM::eshm)
 .
The runtime libraries themselves live in the ${NAME} package."

    deb="$OUTPUT/${DEV_NAME}_${VERSION}_${ARCH}.deb"
    dpkg-deb --root-owner-group --build "$ROOT" "$deb" > /dev/null
    built+=("$deb")
fi

if [ "$ONLY" = "python" ] || [ "$ONLY" = "all" ]; then
    if [ -s "$WORK/python.list" ]; then
        info "Packaging $PY_NAME (Python bindings)"
        ROOT="$WORK/root-python"
        build_root "$WORK/python.list" "$ROOT"

        install_docs "$ROOT" "$PY_NAME"
        write_metadata "$ROOT" "$PY_NAME" "python" "${NAME} (= ${VERSION}), python3 (>= 3.6)" \
            "ESHM shared memory IPC library - Python bindings" \
            "ctypes bindings for ESHM plus the pure-Python ASN.1 DER codec:
 .
   from eshm import ESHM, ESHMRole
   with ESHM(\"my_channel\", role=ESHMRole.MASTER) as conn:
       conn.write(b\"hello\\0\")
 .
The library itself lives in the ${NAME} package."

        deb="$OUTPUT/${PY_NAME}_${VERSION}_all.deb"
        sed -i 's/^Architecture: .*/Architecture: all/' "$ROOT/DEBIAN/control"
        dpkg-deb --root-owner-group --build "$ROOT" "$deb" > /dev/null
        built+=("$deb")
    else
        warn "no Python files staged - configure with -DTESTESHM_INSTALL_PYTHON=ON"
    fi
fi

# --------------------------------------------------------------------------
# Summary
# --------------------------------------------------------------------------
echo
for deb in "${built[@]}"; do
    printf '\033[1m%s\033[0m  (%s, %s files)\n' \
        "$deb" \
        "$(du -h "$deb" | cut -f1)" \
        "$(dpkg-deb --contents "$deb" | grep -cv '/$')"
    dpkg-deb --field "$deb" Package Version Depends | sed 's/^/    /'
    dpkg-deb --contents "$deb" | awk '{print "      " $6}' | sed 's|^      \./|      /|' | grep -v '/$'
    echo
done

cat <<EOF
Install:    cd $OUTPUT && sudo apt install $(for d in "${built[@]}"; do printf './%s ' "$(basename "$d")"; done | sed 's/ $//')
            (apt accepts local paths and resolves the dependencies)
            apt may print "Download is performed unsandboxed as root ...
            couldn't be accessed by user '_apt'" - that is a notice, not an
            error; copy the .deb files to /tmp first to avoid it.
Verify:     scripts/check_install.sh   (or: dpkg -L $NAME)
Remove:     sudo apt remove ${DEV_NAME} ${PY_NAME} $NAME
EOF
