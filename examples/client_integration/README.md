# ESHM Client Integration Example

This directory demonstrates how to integrate ESHM into your own project.

## Directory Structure

```
your_project/
├── 3rdparty/
│   └── eshm/              # ESHM as git submodule
├── CMakeLists.txt         # Your project's build file
├── master.cpp             # Master application
└── slave.cpp              # Slave application
```

## Setup Steps

### 1. Add ESHM as a Git Submodule

```bash
cd your_project/
git submodule add <eshm-git-url> 3rdparty/eshm
git submodule update --init --recursive
```

### 2. Update Your CMakeLists.txt

See [CMakeLists.txt](CMakeLists.txt) in this directory for a complete example.

Key lines:
```cmake
# Add ESHM subdirectory
add_subdirectory(3rdparty/eshm)

# Link your executable
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

### 3. Build Your Project

```bash
mkdir build && cd build
cmake ..
make
```

## Running the Example

```bash
# Terminal 1 - Start master
./my_master

# Terminal 2 - Start slave
./my_slave
```

The master will send messages every second, and the slave will respond with acknowledgments.

## Key Features Demonstrated

- **Automatic role negotiation**: Master/slave roles are handled automatically
- **Reconnection**: Slave automatically reconnects if master restarts
- **Bidirectional communication**: Both processes can read and write
- **Clean integration**: Uses standard CMake target linking

## Alternative: System-Wide Installation

If you prefer to install ESHM system-wide instead of using a submodule:

```bash
# In ESHM directory
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
sudo make install
```

Then in your CMakeLists.txt, use:
```cmake
find_package(ESHM 1.0 REQUIRED)
target_link_libraries(my_app PRIVATE ESHM::eshm)
```

## Notes

- When ESHM is added as a subdirectory, tests and examples are automatically disabled
- The shared libraries (`libeshm.so`, `libeshm_data.so`) are built automatically
- Include paths are handled automatically through CMake targets
