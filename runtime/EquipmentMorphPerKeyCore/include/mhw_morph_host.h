#ifndef MHW_MORPH_HOST_H
#define MHW_MORPH_HOST_H
#include "mhw_morph_api.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Trusted host/testing interface, deliberately separate from the client ABI.
   Does not discover game objects, upload buffers or prove GPU publication. */
typedef struct MhmDelta { uint32_t offset; float x,y,z; } MhmDelta;
typedef struct MhmRotation { uint32_t offset; float x,y,z,angle; } MhmRotation;
typedef struct MhmHostKey { MhmKeyInfo info; const MhmDelta* deltas; uint32_t count; } MhmHostKey;
typedef struct MhmHostTarget {
    uint32_t part, basis_bytes;
    const char* model_id;
    const unsigned char* basis;
    const MhmHostKey* keys;
    uint32_t key_count, rotation_count;
    const MhmRotation* rotations;
} MhmHostTarget;
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostAttach(const MhmHostTarget*, uint64_t*);
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostDetach(uint64_t);
/* Host supplies monotonic elapsed milliseconds even when no weights change.
   Client heartbeat never advances the clock. Leases expire at their deadline. */
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostTick(uint64_t);
/* Immutable, complete snapshot of all live targets. At most 4 retained frames.
   Compose uses this snapshot even if a later request arrives. */
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostCapture(uint64_t*, uint64_t*);
/* One-target host smoothing, including while other parts are live: capture
   an immutable member snapshot. Unrelated target add/remove does not invalidate
   it; removing this member does. Capture actual interpolated weights against a
   checked request revision. Client requested weights remain unchanged. */
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostCaptureInterpolated(uint64_t target, const float*, uint32_t count, uint64_t expected_revision, uint64_t* frame);
/* Complete multi-target interpolated snapshot at one checked request revision.
   Every live target/key must occur exactly once; order is immaterial. Invalid,
   incomplete or stale groups never allocate a frame or modify applied weights.
   Acknowledge ONLY after every member has actually been published. This function
   does not upload, publish or synchronize GPU resources. Client ABI unchanged. */
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostCaptureInterpolatedGroup(const MhmUpdate*, uint32_t count, uint64_t expected_revision, uint64_t* frame);
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostCompose(uint64_t frame, uint64_t target, unsigned char*, uint32_t);
/* Call ONLY after the host has published this entire frame. Cannot ack a detached
   frame member or a frame older than that member's applied revision/frame.
   Unrelated target changes do not invalidate a single-part frame. */
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostAcknowledge(uint64_t frame);
MHM_EXPORT MhmResult MHM_CALL MhwMorphHostReleaseFrame(uint64_t frame);
#ifdef __cplusplus
}
#endif
#endif
