# POSIX Shared Memory Migration

## Summary

The ESHM library has been successfully migrated from **System V shared memory** to **POSIX shared memory**.

## Why POSIX?

### System V Shared Memory (Old)
- Uses integer keys generated from hash functions
- Managed via `shmget()`, `shmat()`, `shmdt()`, `shmctl()`
- **Not visible in `/dev/shm/`** - requires `ipcs -m` to view
- Cleanup via `ipcrm` command
- Legacy API dating from 1980s

### POSIX Shared Memory (New)
- Uses string names with `/` prefix (e.g., `/eshm_demo`)
- Managed via `shm_open()`, `mmap()`, `munmap()`, `shm_unlink()`
- **Visible as files in `/dev/shm/`** - standard file tools work
- Cleanup via `rm /dev/shm/eshm_*`
- Modern, POSIX-standard API

## Technical Changes

### 1. Updated Includes

**Removed:**
```cpp
#include <sys/ipc.h>
#include <sys/shm.h>
```

**Added:**
```cpp
#include <sys/mman.h>   // mmap, munmap, shm_open, shm_unlink
#include <sys/stat.h>   // Mode constants
#include <fcntl.h>      // O_* constants
```

### 2. Updated ESHMHandle Structure

**Changed:**
```cpp
// OLD
int shm_id;              // System V SHM identifier
key_t shm_key;          // System V SHM key

// NEW
int shm_fd;             // POSIX SHM file descriptor
char shm_name[256];     // POSIX SHM name (e.g., "/eshm_demo")
```

### 3. API Mapping

| Operation | System V (Old) | POSIX (New) |
|-----------|----------------|-------------|
| **Create SHM** | `shmget(key, size, IPC_CREAT\|0666)` | `shm_open(name, O_CREAT\|O_RDWR, 0666)` + `ftruncate(fd, size)` |
| **Open SHM** | `shmget(key, size, 0666)` | `shm_open(name, O_RDWR, 0666)` |
| **Map memory** | `shmat(shm_id, NULL, 0)` | `mmap(NULL, size, PROT_READ\|PROT_WRITE, MAP_SHARED, fd, 0)` |
| **Unmap memory** | `shmdt(addr)` | `munmap(addr, size)` |
| **Delete SHM** | `shmctl(shm_id, IPC_RMID, NULL)` | `shm_unlink(name)` |
| **Check exists** | `shmget(key, 0, 0) != -1` | `shm_open(name, O_RDONLY, 0) != -1` |
| **Error value** | `(void*)-1` | `MAP_FAILED` |

### 4. Name Generation

**System V (hash-based key):**
```cpp
key_t generate_shm_key(const char* name) {
    key_t key = 0;
    for (const char* p = name; *p != '\0'; p++) {
        key = (key << 5) + key + *p;
    }
    return key;
}
```

**POSIX (string name with validation):**
```cpp
void generate_shm_name(char* dest, size_t dest_size, const char* name) {
    snprintf(dest, dest_size, "/eshm_%s", name);
    // Replace any '/' in name with '_' (POSIX names can't have multiple '/')
    for (char* p = dest + 1; *p != '\0'; p++) {
        if (*p == '/') *p = '_';
    }
}
```

### 5. Helper Functions Updated

**shm_exists():**
```cpp
// OLD
bool shm_exists(key_t key) {
    int shm_id = shmget(key, 0, 0);
    return (shm_id != -1);
}

// NEW
bool shm_exists(const char* shm_name) {
    int fd = shm_open(shm_name, O_RDONLY, 0);
    if (fd == -1) return false;
    close(fd);
    return true;
}
```

**delete_shm():**
```cpp
// OLD
int delete_shm(key_t key) {
    int shm_id = shmget(key, 0, 0);
    if (shm_id != -1) {
        if (shmctl(shm_id, IPC_RMID, NULL) == -1) {
            return ESHM_ERROR_SHM_DELETE;
        }
    }
    return ESHM_SUCCESS;
}

// NEW
int delete_shm(const char* shm_name) {
    if (shm_unlink(shm_name) == -1 && errno != ENOENT) {
        return ESHM_ERROR_SHM_DELETE;
    }
    return ESHM_SUCCESS;
}
```

## Verification

### 1. SHM Files Are Now Visible

```bash
# Start a master process
./build/eshm_demo master my_test &

# Check /dev/shm/
ls -la /dev/shm/
# Output:
# -rw-rw-r-- 1 user user 8576 Dis 6 22:57 eshm_my_test
```

### 2. Communication Still Works

**C++ ↔ C++:**
```bash
./build/eshm_demo master test &
./build/eshm_demo slave test &
# [MASTER] Received: ACK from slave #0
# [SLAVE] Received (21 bytes): Hello from master #2
```

**C++ ↔ Python:**
```bash
./build/eshm_demo master interop &
python3 py/examples/simple_slave.py interop &
# [MASTER] Received: ACK from Python slave #0
# [SLAVE] Received (21 bytes): Hello from master #1
```

### 3. Automatic Cleanup Works

When a process exits normally (with `auto_cleanup=true`):
```bash
./build/eshm_demo master test &
sleep 2
kill $!
ls /dev/shm/eshm_test
# File is automatically removed
```

### 4. Manual Cleanup

To remove all ESHM shared memory segments:
```bash
rm -f /dev/shm/eshm_*
```

## Benefits

1. **Visibility**: SHM segments now visible as regular files in `/dev/shm/`
2. **Standard tools**: Can use `ls`, `rm`, `stat` to inspect and manage SHM
3. **Modern API**: POSIX SHM is the modern standard, better supported
4. **Debugging**: Easier to debug - can see exactly what SHM exists
5. **Portability**: POSIX is more portable across Unix-like systems
6. **Documentation**: Better documented than System V SHM

## Compatibility

- ✅ All existing functionality preserved
- ✅ C++ master/slave communication works
- ✅ Python wrapper works (via ctypes)
- ✅ C++ ↔ Python interoperability works
- ✅ Automatic reconnection works
- ✅ Heartbeat and monitoring work
- ✅ All tests pass

## Testing

Run the comprehensive test suite:

```bash
# Build
cd build && make && cd ..

# Test C++ communication
./build/eshm_demo master test &
./build/eshm_demo slave test &

# Test C++ ↔ Python interop
./scripts/test_interop.sh

# Check /dev/shm/
ls -la /dev/shm/
```

## Migration Notes

### For Users

**No API changes required!** The public API (`eshm_init()`, `eshm_write()`, `eshm_read()`, etc.) remains identical. The change is purely internal.

### For Developers

If you've been using `ipcs -m` to view SHM segments, use this instead:

```bash
# OLD way (System V)
ipcs -m

# NEW way (POSIX)
ls -la /dev/shm/eshm_*
```

To clean up:

```bash
# OLD way (System V)
ipcrm -a   # Removes ALL SHM segments (dangerous!)

# NEW way (POSIX)
rm -f /dev/shm/eshm_*   # Only removes ESHM segments
```

## Implementation Details

### Size Requirement

POSIX shared memory requires explicit size setting via `ftruncate()`:

```cpp
int shm_fd = shm_open("/eshm_test", O_CREAT | O_RDWR, 0666);
ftruncate(shm_fd, sizeof(ESHMData));  // Set size to 8576 bytes
```

### File Descriptor Management

Unlike System V (which uses integer IDs), POSIX uses file descriptors:

```cpp
int shm_fd = shm_open("/eshm_test", O_RDWR, 0666);
// ... use the fd ...
close(shm_fd);  // Must close fd when done
```

### Naming Constraints

POSIX SHM names must:
- Start with `/`
- Contain at most one `/` (at the beginning)
- Be unique system-wide

Our implementation automatically handles this by:
1. Adding `/eshm_` prefix
2. Replacing any `/` characters in the user-provided name with `_`

## Files Modified

- [eshm.cpp](eshm.cpp) - Main implementation
- [eshm.h](eshm.h) - No changes (API unchanged)
- [eshm_data.h](eshm_data.h) - Updated structure
- Python wrapper automatically works (loads same C library via ctypes)

## Conclusion

The migration to POSIX shared memory is **complete and tested**. All functionality works as before, with the added benefit of SHM segments being visible in `/dev/shm/`.
