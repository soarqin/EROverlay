#pragma once

#include "api.h"

#if defined(__cplusplus)
extern "C" {
#endif

// This extension is discovered independently of EROverlayAPI. Never extend its
// unversioned layout, which is also used by previously built overlay plugins.
typedef enum { ER_FILE_QUEUED, ER_FILE_PENDING, ER_FILE_SUCCEEDED, ER_FILE_FAILED, ER_FILE_CANCELLED, ER_FILE_UNSUPPORTED, ER_FILE_INVALID } ERFileStatus;

typedef struct {
    const wchar_t *path;
    uint32_t flags;
    // When nonempty, copy only these DDS entries from a TPF in the callback.
    // Names and paths are copied before requestFile() returns.
    const wchar_t *const *tpfNames;
    uint32_t tpfNameCount;
    uint64_t maxBytes;
} ERFileRequest;

typedef struct {
    const void *data;
    uint64_t size;
    int32_t nativeStatus;
} ERFileData;

typedef enum { ER_TEXTURE_PENDING, ER_TEXTURE_READY, ER_TEXTURE_FAILED, ER_TEXTURE_INVALID } ERTextureStatus;

typedef struct {
    void *gpuHandle;
    uint32_t width;
    uint32_t height;
} ERTextureView;

typedef struct {
    uint64_t generation;
    uint32_t rawMapId;
    int32_t mapId;
    float x;
    float y;
    int32_t underground;
    float oriDeg;
    uint32_t activeMasks[3];
    bool deathValid;
    float deathX;
    float deathY;
    int32_t deathMapId;
    // The exact native view model is exposed only as a transient identity;
    // clients must not keep this pointer beyond the current update.
    uintptr_t viewModel;
} ERMapState;

typedef struct {
    uint32_t size;
    uint32_t version;
    bool (*gameCompatible)();
    uint64_t (*requestFile)(const ERFileRequest *request);
    ERFileStatus (*pollFile)(uint64_t token, const wchar_t *part, ERFileData *data);
    void (*releaseFile)(uint64_t token); // Also cancels queued/pending requests.
    // Texture methods run on the plugin render/create/destroy renderer thread.
    // CPU file methods can be used from update; poll data remains valid until
    // releaseFile(), which must not race a consumer of that data.
    uint64_t (*createDdsTexture)(const void *dds, uint64_t size);
    ERTextureStatus (*pollTexture)(uint64_t token, ERTextureView *view);
    void (*retireTexture)(uint64_t token);
    bool (*readMapState)(ERMapState *state);
    bool (*beginOffscreen)(void *offscreen);
    uintptr_t (*findParamTable)(uint32_t repositoryGroup);
    void (*log)(const char *message);
    bool (*readEventFlag)(uint32_t id);
} EROverlayNativeAPI;

API_EXPORT const EROverlayNativeAPI *getEROverlayNativeAPI(uint32_t version);

#if defined(__cplusplus)
}
#endif
