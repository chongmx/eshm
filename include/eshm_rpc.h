#ifndef ESHM_RPC_H
#define ESHM_RPC_H

/*
 * Named triggers across a channel: run a function on the other side.
 *
 * A trigger carries a name and nothing else - no arguments, no return value.
 * Values travel through whatever data structure the two sides already share;
 * the trigger only says "go look". That makes handlers **level-triggered**:
 *
 *     write the data  ->  fire the trigger  ->  handler reads current state
 *
 * and it means two triggers that coalesce into one delivery are *correct*, not
 * a lost message. The handler runs once and reads the latest state, which is
 * what both firings wanted. Write handlers to be idempotent; never count them.
 * (eshm_rpc_missed() reports coalescing if you need to know it happened.)
 *
 * Threading
 * ---------
 * eshm_rpc_start() spawns one dispatcher thread that owns the control channel
 * end to end. Handlers run **on that thread**, so keep them short or hand off
 * to your own queue - a slow handler delays every later trigger. Firing a
 * trigger from inside a handler is fine.
 *
 * Channel naming
 * --------------
 * eshm_rpc_create("demo", ...) opens the channel "demo_ctl", never "demo".
 * The control channel is kept separate from your data channel on purpose: the
 * dispatcher is the single reader of its channel, and a trigger written onto a
 * data channel would overwrite the data it was announcing.
 */

#include "eshm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque dispatcher instance. */
typedef struct EshmRpc EshmRpc;

/* A trigger handler. `user` is the pointer given at registration.
 * Takes nothing and returns nothing - that is the whole contract. */
typedef void (*eshm_rpc_handler)(void* user);

/* Description of the last failure on this thread; never NULL. */
const char* eshm_rpc_get_last_error(void);

/*
 * Create a dispatcher on "<channel>_ctl".
 *
 * role is ESHM_ROLE_MASTER (creates the control channel) or ESHM_ROLE_SLAVE
 * (attaches to it; retries for a few seconds, since a slave cannot attach
 * before the master exists). Returns NULL on failure.
 */
EshmRpc* eshm_rpc_create(const char* channel, enum ESHMRole role);

/* Stop the dispatcher if running, then release everything. */
void eshm_rpc_destroy(EshmRpc* rpc);

/*
 * Register a call handler: exactly one per name. Registering a name twice
 * replaces the first handler. An incoming call for an unknown name is logged
 * as an error.
 * Returns ESHM_SUCCESS, or ESHM_ERROR_INVALID_PARAM.
 */
int eshm_rpc_on_call(EshmRpc* rpc, const char* name,
                     eshm_rpc_handler fn, void* user);

/*
 * Register an event handler: any number per name, all run in registration
 * order. An incoming event with no handlers is ignored, not an error.
 * Returns ESHM_SUCCESS, or ESHM_ERROR_INVALID_PARAM.
 */
int eshm_rpc_on_event(EshmRpc* rpc, const char* name,
                      eshm_rpc_handler fn, void* user);

/* Start / stop the dispatcher thread. Both are idempotent.
 * Returns ESHM_SUCCESS, or an error code. */
int eshm_rpc_start(EshmRpc* rpc);
int eshm_rpc_stop(EshmRpc* rpc);

/*
 * Fire a trigger at the peer. Both return as soon as the write lands - there
 * is no reply to wait for.
 *   eshm_rpc_call  - "run your handler named this"   (one handler expected)
 *   eshm_rpc_emit  - "this happened"                 (zero or more handlers)
 * Returns ESHM_SUCCESS, or an error code.
 */
int eshm_rpc_call(EshmRpc* rpc, const char* name);
int eshm_rpc_emit(EshmRpc* rpc, const char* name);

/* How many triggers were coalesced away before reaching us, detected as gaps
 * in the peer's sequence numbers. Non-zero is normal under load and harmless
 * for idempotent handlers; it is a red flag if you have made one stateful. */
uint64_t eshm_rpc_missed(const EshmRpc* rpc);

/* Triggers dispatched so far (handlers actually run). */
uint64_t eshm_rpc_dispatched(const EshmRpc* rpc);

#ifdef __cplusplus
}
#endif

#endif /* ESHM_RPC_H */
