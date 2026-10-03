#pragma once

#include <stddef.h>

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
    // Names and paths are copied before requestFile() returns. Each named part
    // has its own success/failure result; missing entries do not fail siblings.
    const wchar_t *const *tpfNames;
    uint32_t tpfNameCount;
    // Maximum bytes copied into overlay memory (default/cap: 256 MiB). For
    // named TPF reads, this bounds the sum of selected DDS, not the container.
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

// Optional, size-gated CPU layout information for hash-verified executables.
typedef struct {
    uint32_t size;
    uint32_t menuOwnerOffset;
    uint32_t ownerViewOffset;
    uint32_t menuInfoOffset;
    uint32_t screenStateOffset;
    uint32_t graceStride;
    uint32_t graceNormalOffset;
    uint32_t alternateIcons;
    uint32_t mapMask; // Bit 0: M00/m60, bit 1: M01, bit 2: M10/m61.
    uint32_t repositoryGroups;
} ERGameLayout;

typedef struct {
    uint32_t size;
    uint32_t version;
    bool (*gameCompatible)();
    uint64_t (*requestFile)(const ERFileRequest *request);
    ERFileStatus (*pollFile)(uint64_t token, const wchar_t *part, ERFileData *data);
    void (*releaseFile)(uint64_t token); // Also cancels queued/pending requests.
    // createDdsTexture copies/queues CPU data and may also run on update with
    // current cores. pollTexture/retireTexture remain render-thread methods.
    // Check size for queueDdsTexture below before issuing update-thread calls.
    // CPU file methods can be used from update; poll data remains valid until
    // releaseFile(), which must not race a consumer of that data.
    uint64_t (*createDdsTexture)(const void *dds, uint64_t size);
    ERTextureStatus (*pollTexture)(uint64_t token, ERTextureView *view);
    void (*retireTexture)(uint64_t token);
    bool (*readMapState)(ERMapState *state);
    bool (*beginOffscreen)(void *offscreen);
    uintptr_t (*findParamTable)(uint32_t repositoryGroup);
    // Optional sink: nullptr when the diagnostic log file is disabled.
    void (*log)(const char *message);
    bool (*readEventFlag)(uint32_t id);
    // Appended to version 1. Check size before accessing this member. Older
    // cores without it support only the original latest-game layout.
    bool (*readGameLayout)(ERGameLayout *layout);
    // Appended to version 1: thread-safe CPU preparation/queueing. The returned
    // token is polled/retired on render just like createDdsTexture tokens.
    uint64_t (*queueDdsTexture)(const void *dds, uint64_t size);
    // Local offscreen target. Vertices/scissors generated between begin and
    // end are translated internally; composite it using UV (0,0)..(1,1).
    bool (*beginOffscreenRegion)(void *offscreen, float x, float y, float width, float height);
    // Actual committed GPU allocation; render-thread query, nonzero after
    // upload submission. Older cores can use a conservative DDS size estimate.
    uint64_t (*textureMemoryBytes)(uint64_t token);
} EROverlayNativeAPI;

#define ER_NATIVE_API_V1_SIZE offsetof(EROverlayNativeAPI, readGameLayout)

API_EXPORT const EROverlayNativeAPI *getEROverlayNativeAPI(uint32_t version);

#if defined(__cplusplus)
}
#endif
