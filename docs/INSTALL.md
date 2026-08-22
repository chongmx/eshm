# Installing, packaging and removing ESHM

ESHM installs as three logical pieces, mirroring how Debian ships any C library:

| Piece | Contents | Needed to |
|---|---|---|
| runtime (`libeshm1`) | `libeshm.so.1`, `libeshm_data.so.1` | **run** programs built against ESHM |
| development (`libeshm-dev`) | headers, unversioned `.so` symlinks, `lib/cmake/ESHM/` | **build** against ESHM |
| Python (`python3-eshm`) | `python3/dist-packages/eshm/` | `from eshm import ESHM` |

Check what a machine has at any time:

```bash
./scripts/check_install.sh              # ✓/✗ per component, with the fix for each
./scripts/check_install.sh --root DIR   # inspect a staged or extracted tree
```

## Install from source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure     # optional but recommended
sudo cmake --install build
sudo ldconfig                                   # refresh the linker cache
```

This installs:

```
<prefix>/lib/libeshm.so{,.1,.1.0.0}         <prefix>/include/eshm.h
<prefix>/lib/libeshm_data.so{,.1,.1.0.0}    <prefix>/include/eshm_data.h
<prefix>/lib/cmake/ESHM/*.cmake             <prefix>/include/eshm_config.h
<prefix>/lib/python3*/dist-packages/eshm/   <prefix>/include/asn1_der.h
<prefix>/share/doc/eshm/{LICENSE,README.md} <prefix>/include/data_handler.h
                                            <prefix>/include/data_handler_c_api.h
                                            <prefix>/include/eshm_data_api.h
```

Build options that affect what is installed:

| Option | Default | Effect |
|---|---|---|
| `ESHM_INSTALL_PYTHON` | `ON` | install the Python bindings |
| `ESHM_PYTHON_INSTALL_DIR` | auto | where they go; override on non-Debian systems (`site-packages`) |
| `ESHM_BUILD_DEMO` | standalone | build and install `eshm_demo` |
| `ESHM_MAX_DATA_SIZE` | `4096` | channel size baked into `eshm_config.h` |

## Uninstall a source install

```bash
sudo cmake --build build --target uninstall
sudo ldconfig
```

It removes exactly what the last `cmake --install` recorded in
`build/install_manifest.txt`. If that build directory is gone, remove the files
listed above by hand.

## Build Debian packages

```bash
./scripts/export_deb.sh                 # all three, into dist/
./scripts/export_deb.sh --only dev      # one of: runtime | dev | python | all
./scripts/export_deb.sh --with-tools    # also ship eshm_demo and test_selftest
./scripts/export_deb.sh --help          # name, version, maintainer, prefix, jobs …
```

The script configures a separate `build-deb/` tree with
`-DCMAKE_INSTALL_PREFIX=/usr`, runs the test suite (`--skip-tests` to bypass),
stages a `cmake --install`, splits the staged tree into package roots and builds
each `.deb` with `dpkg-deb`. It needs `cmake` and `dpkg-dev`.

Install and remove them:

```bash
cd dist
sudo apt install ./libeshm1_1.0.0_amd64.deb ./libeshm-dev_1.0.0_amd64.deb \
                 ./python3-eshm_1.0.0_all.deb
sudo apt remove python3-eshm libeshm-dev libeshm1     # or: apt purge
```

apt may print `Download is performed unsandboxed as root as file '…deb'
couldn't be accessed by user '_apt'`. That is a notice, not a failure — apt's
sandbox user cannot read the directory holding the file, so it copies as root
instead. Copy the packages to `/tmp` first to avoid it.

### What the packaging follows

* [Policy §8.1](https://www.debian.org/doc/debian-policy/ch-sharedlibs.html):
  the runtime package is named after the SONAME (`libeshm.so.1` → `libeshm1`),
  with `libeshm-dev` beside it holding the headers and the unversioned symlink.
* Policy §8.6.1: `libeshm-dev` has an exact dependency, `libeshm1 (= <version>)`.
* Policy §8.1.1: the runtime activates libc's `ldconfig` trigger
  (`DEBIAN/triggers`) instead of calling `ldconfig` from maintainer scripts.
* Policy §8.6: the runtime ships a `shlibs` file, so `dpkg-shlibdeps` gives
  downstream packages `Depends: libeshm1 (>= 1.0.0)` automatically.
* Policy §12.5: every package carries `copyright` and `changelog.Debian.gz`.
* `Multi-Arch: same` on the runtime (dropped under `--with-tools`, which adds
  non-multiarch paths).
* [Python policy](https://www.debian.org/doc/packaging-manuals/python-policy/):
  `python3-eshm` is `Architecture: all`, installs into
  `/usr/lib/python3/dist-packages/eshm/` and depends on the runtime — never on
  `-dev`.

Remaining gaps before an archive (PPA) upload: a `symbols` file would give
tighter downstream dependencies than `shlibs`, and the runtime ships two
libraries in one package — allowed while their SONAMEs always change together,
which is the case here, but policy prefers one library per package.

To publish so that plain `apt install libeshm1` works, push to a Launchpad PPA
(add a `debian/` directory, `debuild -S`, `dput`) or host a signed repository
with `aptly` or `reprepro`.

## Using it afterwards

C++ (needs `libeshm-dev`):

```cmake
find_package(ESHM 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

```bash
g++ -std=c++17 app.cpp -o app -leshm -lpthread -lrt      # without CMake
```

Python (needs `python3-eshm`):

```python
from eshm import ESHM, ESHMRole

with ESHM("my_channel", role=ESHMRole.MASTER) as conn:
    conn.write(b"hello\0")
    reply = conn.read(buffer_size=4096, timeout_ms=200)
```

The bindings locate `libeshm.so` themselves — build tree, then the system
directories, then the linker cache. `ESHM_LIB=/path/to/libeshm.so` overrides
that, and `eshm.library_path()` reports what was picked.

The Python package holds five modules: `eshm` (channel API), `data_handler`
(pure-Python DER codec), `data_handler_native` and `eshm_data` (the same work
done in C++ through the C API, for throughput), and the package `__init__`.

Worked examples live in [examples/getting_started/](../examples/getting_started/)
and [py/examples/getting_started.py](../py/examples/getting_started.py).

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `fatal error: eshm.h: No such file or directory` | install `libeshm-dev` |
| `/usr/bin/ld: cannot find -leshm` | same — the `.so` symlink is in `libeshm-dev` |
| `error while loading shared libraries: libeshm.so.1` | install `libeshm1`, then `sudo ldconfig` |
| `Could not find a package configuration file provided by "ESHM"` | install `libeshm-dev`, or pass `-DCMAKE_PREFIX_PATH=/usr/local` |
| `ModuleNotFoundError: No module named 'eshm'` | install `python3-eshm` |
| `RuntimeError: libeshm.so not found` | the bindings are installed but the runtime is not |
| `[ESHM] Slave failed to attach to SHM: No such file or directory` | the master is not running yet — slaves must retry |
| `dpkg-query` shows state `rc` after removal | removed but not purged; `sudo apt purge <pkg>` clears it |
