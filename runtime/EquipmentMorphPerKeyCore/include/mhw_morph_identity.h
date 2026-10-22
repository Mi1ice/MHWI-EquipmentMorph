#ifndef MHW_MORPH_IDENTITY_H
#define MHW_MORPH_IDENTITY_H
#include "mhw_morph_api.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Optional extension. The v1 MhmApi layout and getter remain unchanged.
   Digests identify the verified, loaded asset, not a later disk-file edit.
   NOT_READY means no verified identity is available; STALE means retired handle.
   On failure output is unchanged. Hashes are lowercase SHA-256 hex strings. */
#define MHM_IDENTITY_VERSION 1u
typedef struct MhmAssetIdentity { uint64_t handle; char model_sha256[65], pack_sha256[65]; } MhmAssetIdentity;
MHM_EXPORT MhmResult MHM_CALL MhwMorphGetAssetIdentity(uint32_t version, uint32_t bytes, uint64_t handle, MhmAssetIdentity* output);
#ifdef __cplusplus
}
#endif
#endif
