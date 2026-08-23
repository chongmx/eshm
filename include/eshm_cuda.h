#ifndef ESHM_CUDA_H
#define ESHM_CUDA_H

/*
 * Zero-copy NVIDIA VRAM sharing between processes, alongside ESHM's normal
 * host-memory channels.
 *
 * One process calls eshm_cuda_create() to allocate a block of device memory
 * and publish it under a name; any number of other processes call
 * eshm_cuda_attach() to map the SAME physical VRAM into their own address
 * space (via the CUDA driver's VMM API - cuMemCreate/cuMemExportToShareableHandle
 * with a POSIX file descriptor, not the legacy cudaIpcGetMemHandle path,
 * which is unsupported on WSL2). Each process gets its own CUdeviceptr value
 * for the same bytes - the pointer is process-local, do not send it to a peer.
 *
 * This module has no opinion on *when* the data is ready to read. Signal
 * that yourself over an ordinary ESHM channel or an eshm_rpc trigger
 * (include/eshm_rpc.h) after your cudaMemcpy/kernel has completed and its
 * stream has been synchronized - there is no implicit cross-process stream
 * ordering here, only the memory mapping.
 *
 * Rendezvous mechanism: the fd behind the VRAM allocation is handed to
 * attachers over an abstract-namespace AF_UNIX socket (SCM_RIGHTS), because a
 * raw fd number is meaningless across processes and POSIX shared memory
 * cannot carry a live fd. The producer keeps accepting connections for its
 * whole lifetime, so late or reconnecting attachers are always served the
 * current allocation.
 *
 * Requires an NVIDIA driver/CUDA version with VMM POSIX-fd export support
 * (R470+) and, per NVIDIA's WSL guidance, is the path known to work under
 * WSL2 where the legacy cudaIpc* API is not reliable.
 */

#include "eshm_data.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle to a mapped (or owned) VRAM buffer. */
typedef struct EshmCudaBuffer EshmCudaBuffer;

typedef struct {
    const char* name;    /* Shared identifier, like ESHMConfig.shm_name */
    size_t size;         /* Bytes to allocate - eshm_cuda_create() only */
    int device_ordinal;  /* CUDA device index to allocate on; default 0 */
} EshmCudaConfig;

static inline EshmCudaConfig eshm_cuda_default_config(const char* name, size_t size) {
    EshmCudaConfig config;
    config.name = name;
    config.size = size;
    config.device_ordinal = 0;
    return config;
}

/* Wait forever in eshm_cuda_attach(). Distinct from 0, which tries once and
 * returns immediately - same asymmetry as ESHM_TIMEOUT_INFINITE vs 0 in
 * eshm_read_ex(), kept for consistency across the library. */
#define ESHM_CUDA_TIMEOUT_INFINITE 0xFFFFFFFFu

/*
 * Allocate config->size bytes of VRAM on config->device_ordinal and start
 * serving it to attachers under config->name. Exactly one process should
 * hold this role per name at a time - conceptually ESHM_ROLE_MASTER for a
 * GPU buffer instead of a host channel.
 *
 * The allocation and the listener thread live until eshm_cuda_destroy().
 * Returns NULL on failure; see eshm_cuda_get_last_error().
 */
EshmCudaBuffer* eshm_cuda_create(const EshmCudaConfig* config);

/*
 * Attach to a buffer published by eshm_cuda_create() under `name`, mapping
 * the same physical VRAM into this process with zero copy. Retries the
 * rendezvous connection internally, since the producer may not have started
 * yet:
 *   timeout_ms: 0                     - try once, fail immediately
 *               1..UINT32_MAX-1       - retry for up to this many ms
 *               ESHM_CUDA_TIMEOUT_INFINITE - retry until it succeeds
 * Returns NULL on failure.
 */
EshmCudaBuffer* eshm_cuda_attach(const char* name, uint32_t timeout_ms);

/*
 * Process-local device pointer and size of the mapped region. The pointer
 * is valid for CUDA calls (cudaMemcpy, kernels, cupy/torch wrapping) in THIS
 * process only.
 * Returns ESHM_SUCCESS, or ESHM_ERROR_INVALID_PARAM if buf/devptr is NULL.
 */
int eshm_cuda_get_ptr(EshmCudaBuffer* buf, void** devptr, size_t* size);

/* CUDA device ordinal this buffer is mapped on. */
int eshm_cuda_device(EshmCudaBuffer* buf);

/*
 * Generation counter, starting at 1. Reserved for future re-allocation
 * support (a producer replacing its buffer without changing its name); a
 * consumer would compare this against its last-seen value to notice a
 * stale mapping. Currently constant for the life of a buffer.
 */
uint64_t eshm_cuda_generation(EshmCudaBuffer* buf);

/*
 * Unmap and release. A producer also stops accepting new attachers and
 * closes its exported fd. Already-attached consumers are unaffected - VRAM
 * behind a CUDA IPC mapping is refcounted by the driver, not by ESHM, so
 * their mapping stays valid until they call this too.
 */
void eshm_cuda_destroy(EshmCudaBuffer* buf);

/* Description of the last failure on this thread; never NULL. */
const char* eshm_cuda_get_last_error(void);

#ifdef __cplusplus
}
#endif

#endif /* ESHM_CUDA_H */
