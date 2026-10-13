#ifndef MHW_MORPH_API_H
#define MHW_MORPH_API_H
#include <stdint.h>
#ifdef _WIN32
#define MHM_CALL __cdecl
#ifdef MHM_BUILD
#define MHM_EXPORT __declspec(dllexport)
#else
#define MHM_EXPORT
#endif
#else
#define MHM_CALL
#define MHM_EXPORT
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental v1 C ABI. Identifiers are ASCII; display labels belong in asset metadata.
   All functions are thread-safe. No callback runs on a game/render thread.
   The containing DLL must remain loaded while any client uses this table. */
#define MHM_API_VERSION 0x00010000u
typedef int32_t MhmResult;
enum { MHM_OK=0, MHM_INVALID=1, MHM_STALE=2, MHM_CAPACITY=3,
       MHM_CONFLICT=4, MHM_NOT_READY=5, MHM_BUFFER_SMALL=6,
       MHM_VERSION=7, MHM_INTERNAL=8, MHM_EVENTS_LOST=9 };
enum { MHM_HEAD=0, MHM_BODY=1, MHM_ARMS=2, MHM_WAIST=3, MHM_LEGS=4 };
enum { MHM_AUTHOR_SHADING=1 };
enum { MHM_TARGET_ADDED=1, MHM_TARGET_REMOVED=2, MHM_SOURCE_EXPIRED=3 };
enum { MHM_CAP_BATCH=1, MHM_CAP_LAYERS=2, MHM_CAP_LEASE=4,
       MHM_CAP_EVENTS=8, MHM_CAP_CPU_COMPOSE=16, MHM_CAP_PART_LIFECYCLE=32 };
typedef struct MhmLimits { uint32_t targets, keys_per_target, sources, batch_updates, events; } MhmLimits;
typedef struct MhmTargetInfo { uint64_t handle; uint32_t part, key_count; char model_id[128]; } MhmTargetInfo;
typedef struct MhmKeyInfo { char id[64]; float minimum, maximum; uint32_t flags, exclusive_group; } MhmKeyInfo;
typedef struct MhmUpdate { uint64_t target; uint32_t key; float weight; } MhmUpdate;
typedef struct MhmWeightState { float requested, applied; uint64_t source; } MhmWeightState;
typedef struct MhmEvent { uint64_t sequence, handle, revision; uint32_t type, reserved; } MhmEvent;
typedef struct MhmApi {
    uint32_t version, bytes, capabilities, reserved;
    MhmResult (MHM_CALL *limits)(MhmLimits*);
    MhmResult (MHM_CALL *target_at)(uint32_t, MhmTargetInfo*);
    MhmResult (MHM_CALL *key_at)(uint64_t, uint32_t, MhmKeyInfo*);
    MhmResult (MHM_CALL *open_source)(const char*, uint32_t, uint32_t, uint64_t*);
    MhmResult (MHM_CALL *close_source)(uint64_t);
    MhmResult (MHM_CALL *heartbeat)(uint64_t);
    /* Submit REPLACES this source's entire contribution set, atomically.
       Empty batch releases all its weights. Explicit zero overrides lower priority.
       Per-key highest priority wins; equal priority uses older source ID.
       Invalid, duplicate, stale or conflicting batches leave the previous set intact. */
    MhmResult (MHM_CALL *submit)(uint64_t, const MhmUpdate*, uint32_t, uint64_t*);
    /* Request revision is global. Applied revision is per target and may differ
       between independently published parts; equality is not a batch receipt. */
    MhmResult (MHM_CALL *read_weights)(uint64_t, MhmWeightState*, uint32_t, uint32_t*, uint64_t*, uint64_t*);
    /* Cursor 0 starts at the first retained event. EVENTS_LOST supplies the oldest
       retained event so consumers can rescan targets and continue from its sequence. */
    MhmResult (MHM_CALL *poll_event)(uint64_t, MhmEvent*);
} MhmApi;
MHM_EXPORT MhmResult MHM_CALL MhwMorphGetApi(uint32_t version, uint32_t bytes, MhmApi* output);
#ifdef __cplusplus
}
#endif
#endif
