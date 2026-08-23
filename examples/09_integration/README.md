# 09 - Integrating ESHM into your project

The other examples show the API. This one shows the **build**: how a project
that is not ESHM pulls ESHM in. [`CMakeLists.txt`](CMakeLists.txt) in this
directory is the example — it carries all three supported options side by side,
commented, so you can pick one and delete the rest.

## The three options

| | How | Best when |
|---|---|---|
| **1. Installed package** | `find_package(ESHM 1.0 REQUIRED)` | ESHM is deployed as a system dependency (`libeshm-dev`, or `cmake --install`) |
| **2. Git submodule** | `add_subdirectory(3rdparty/eshm)` | You want ESHM pinned in your tree and built with your flags |
| **3. FetchContent** | `FetchContent_MakeAvailable(eshm)` | Same as 2, without vendoring the source |

All three end at the same line, which is the whole integration:

```cmake
target_link_libraries(your_app PRIVATE ESHM::eshm)
```

`ESHM::eshm` carries the include directory, `libeshm`, `libeshm_data` and
`pthread`/`rt`. There is nothing else to configure. Link `ESHM::eshm_data`
instead if you only want the ASN.1 codec and never open a channel.

### Option 1 — installed

```bash
sudo apt install ./libeshm1_1.1.0_amd64.deb ./libeshm-dev_1.1.0_amd64.deb
cmake -S . -B build && cmake --build build
```

Installed under a prefix that is not on the default search path? Point CMake at
it: `cmake -S . -B build -DCMAKE_PREFIX_PATH=/usr/local`.

### Option 2 — submodule

```bash
cd your_project/
git submodule add <eshm-git-url> 3rdparty/eshm
git submodule update --init --recursive
```

```cmake
add_subdirectory(3rdparty/eshm)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

ESHM detects that it is a subproject and turns off its own tests, examples and
demo, so your build stays fast. To pin the memory layout, set the option
**before** `add_subdirectory`:

```cmake
set(ESHM_MAX_DATA_SIZE 8388608 CACHE STRING "" FORCE)
add_subdirectory(3rdparty/eshm)
```

That constant is baked into the headers at configure time, so everything
sharing a channel must be built with the same value — see
[06](../06_large_payload/).

## Build and run this one

```bash
cmake -S . -B build && cmake --build build

./build/integration_master     # terminal 1
./build/integration_slave      # terminal 2
```

Both programs use the channel `demo_shm`, and the Python peer defaults to the
same name — so an integrated C++ application is reachable from Python with no
extra work on your side:

```bash
./build/integration_master     +   python3 peer.py slave
python3 peer.py master         +   ./build/integration_slave
```

That is worth designing for. Install `python3-eshm` on the same machine and
your test harnesses, monitoring scripts and one-off debugging tools can attach
to a running C++ process through a channel it already has, without it needing
to expose an HTTP port or a socket.

## Verifying an install

```bash
../../scripts/check_install.sh      # what this machine actually has
```

[`docs/INSTALL.md`](../../docs/INSTALL.md) covers the package split, the
`/usr` vs `/usr/local` Python path question, and removal.
