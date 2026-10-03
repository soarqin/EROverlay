#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
// clang-format off
#include <windows.h>
#include <shlwapi.h>
#include <xmllite.h>
// clang-format on

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>

#include "layout.hpp"
#include "resources.hpp"

namespace er::minimap {
const EROverlayNativeAPI *nativeApi = nullptr;
Resources gResources;
namespace {
constexpr const wchar_t *PATHS[] = {L"menu:/02_120_WorldMap.gfx", L"menu:/Hi/01_Common.sblytbnd.dcx", L"menu:/71_MapTile.tpfbhd", L"menu:/71_MapTile.mtmskbnd.dcx",
                                    L"menu:/Hi/01_Common.tpf.dcx"};
constexpr size_t TILE_CACHE_BYTES = 64 * 1024 * 1024;
constexpr size_t TILE_CACHE_ENTRIES = 512;
constexpr uint64_t CACHE_IDLE_MS = 10000;
bool canQueueTextures() { return nativeApi && nativeApi->size >= offsetof(EROverlayNativeAPI, queueDdsTexture) + sizeof(nativeApi->queueDdsTexture) && nativeApi->queueDdsTexture; }
size_t textureBytes(const util::DdsImage &image) {
    size_t bytes = 0;
    for (const auto &level: image.levels)
        bytes += static_cast<size_t>(level.rowBytes) * level.rows;
    // Committed DDS textures use at least 64 KiB; account for allocation
    // granularity as well as compressed mip payloads when trimming the cache.
    return (bytes + 65535) & ~size_t(65535);
}
bool tileKey(std::wstring_view name, TileKey &key) {
    constexpr std::wstring_view prefix = L"MENU_MapTile_M";
    if (!name.starts_with(prefix) || name.size() != prefix.size() + 20)
        return false;
    name.remove_prefix(prefix.size());
    if (name[2] != L'_' || name[3] != L'L' || name[5] != L'_' || name[8] != L'_' || name[11] != L'_')
        return false;
    auto number = [&](size_t start, size_t count, uint32_t base, uint32_t &out) {
        out = 0;
        for (auto c: name.substr(start, count)) {
            uint32_t digit = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : base;
            if (digit >= base)
                return false;
            out = out * base + digit;
        }
        return true;
    };
    uint32_t map, level, x, y, variant;
    if (!number(0, 2, 10, map) || !number(4, 1, 10, level) || !number(6, 2, 10, x) || !number(9, 2, 10, y) || !number(12, 8, 16, variant) || (map != 0 && map != 1 && map != 10) ||
        level > 2)
        return false;
    key = {static_cast<uint8_t>(map), static_cast<uint8_t>(level), static_cast<uint16_t>(x), static_cast<uint16_t>(y), variant};
    return true;
}
std::string narrow(std::wstring_view value) {
    if (value.empty())
        return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count)
        return {};
    std::string result(count, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring wide(std::string_view value) {
    if (value.empty() || value.size() > 4096)
        return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!count)
        return {};
    std::wstring result(count, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
// TextureAtlas imagePath resolves to a DDS name in the native 01_Common TPF,
// using the same path/extension stripping as the game.
bool atlasSource(std::string_view source, std::wstring &path, std::wstring &name) {
    path = PATHS[4];
    name = wide(util::assetKey(source));
    return !path.empty() && !name.empty();
}
using Attributes = std::map<std::string, std::string>;
bool xml(util::Bytes bytes, const std::function<bool(const std::string &, const Attributes &)> &element) {
    while (!bytes.empty() && bytes.back() == 0)
        bytes = bytes.first(bytes.size() - 1);
    if (bytes.empty() || bytes.size() > 4 * 1024 * 1024)
        return false;
    IStream *stream = SHCreateMemStream(bytes.data(), static_cast<UINT>(bytes.size()));
    if (!stream)
        return false;
    IXmlReader *reader = nullptr;
    bool ok = SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void **>(&reader), nullptr));
    if (ok)
        ok = SUCCEEDED(reader->SetInput(stream)) && SUCCEEDED(reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit));
    XmlNodeType node;
    HRESULT hr = S_OK;
    while (ok && (hr = reader->Read(&node)) == S_OK) {
        if (node != XmlNodeType_Element)
            continue;
        const wchar_t *name = nullptr;
        UINT count = 0;
        if (FAILED(reader->GetLocalName(&name, &count))) {
            ok = false;
            break;
        }
        std::string tag = narrow(std::wstring_view(name, count));
        Attributes attributes;
        HRESULT attribute = reader->MoveToFirstAttribute();
        while (attribute == S_OK) {
            const wchar_t *value = nullptr;
            UINT size = 0;
            if (FAILED(reader->GetLocalName(&name, &count)) || FAILED(reader->GetValue(&value, &size))) {
                ok = false;
                break;
            }
            if (!attributes.emplace(narrow(std::wstring_view(name, count)), narrow(std::wstring_view(value, size))).second) {
                ok = false;
                break;
            }
            attribute = reader->MoveToNextAttribute();
        }
        if (FAILED(attribute) || !ok) {
            ok = false;
            break;
        }
        if (!attributes.empty() && FAILED(reader->MoveToElement())) {
            ok = false;
            break;
        }
        ok = element(tag, attributes);
    }
    ok = ok && hr == S_FALSE;
    if (reader)
        reader->Release();
    stream->Release();
    return ok;
}
bool number(const Attributes &attributes, const char *key, uint32_t &value) {
    auto found = attributes.find(key);
    if (found == attributes.end())
        return false;
    const auto &s = found->second;
    auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), value);
    return error == std::errc{} && end == s.data() + s.size();
}
} // namespace

Resources::~Resources() noexcept { stop(); }
bool Resources::loadDefinitions(util::Bytes gfx, util::Bytes layouts) {
    // Published recipes and atlas storage are immutable until plugin shutdown.
    if (definitionsReady_.load(std::memory_order_acquire))
        return false;
    GfxMovie movie;
    std::vector<util::BinderEntry> entries;
    if (!movie.parse(gfx) || !util::parseBinder(layouts, entries))
        return false;
    std::unordered_map<std::string, AtlasRegion> regions;
    std::vector<std::unique_ptr<Atlas>> sources;
    std::map<std::pair<std::wstring, std::wstring>, uint32_t> sourceIds;
    for (const auto &entry: entries) {
        uint32_t atlas = UINT32_MAX;
        if (!xml(entry.bytes, [&](const std::string &tag, const Attributes &a) {
                if (tag == "TextureAtlas") {
                    auto image = a.find("imagePath");
                    if (image == a.end())
                        return false;
                    auto source = std::make_unique<Atlas>();
                    if (!atlasSource(image->second, source->path, source->name))
                        return false;
                    auto key = std::pair{source->path, source->name};
                    auto [found, inserted] = sourceIds.emplace(key, static_cast<uint32_t>(sources.size()));
                    atlas = found->second;
                    if (inserted)
                        sources.push_back(std::move(source));
                } else if (tag == "SubTexture" && atlas != UINT32_MAX) {
                    auto name = a.find("name");
                    AtlasRegion region{};
                    uint32_t half;
                    region.atlas = atlas;
                    if (name == a.end() || !number(a, "x", region.x) || !number(a, "y", region.y) || !number(a, "width", region.width) || !number(a, "height", region.height) ||
                        !number(a, "half", half) || !region.width || !region.height || region.x > 16384 || region.y > 16384 || region.width > 16384 || region.height > 16384)
                        return false;
                    // Unrelated half-resolution layouts are not used by the
                    // WorldMap GFX. Reject only a requested missing region.
                    if (!half)
                        // Native 0xD643A0 discards a duplicate key, retaining
                        // the first atlas alias in binder order.
                        regions.try_emplace(util::assetKey(name->second), region);
                }
                return true;
            }))
            return false;
    }
    std::unordered_map<uint32_t, IconRecipe> icons;
    for (uint32_t i = 1; i <= movie.iconFrameCount(); ++i) {
        IconRecipe recipe;
        if (movie.icon(i, recipe))
            icons.emplace(i, std::move(recipe));
        else if (movie.iconLayerCount(i))
            return false;
    }
    if (icons.empty())
        return false;
    std::unordered_map<std::string, IconRecipe> specials;
    for (auto [name, path]: {std::pair{"home", "Body/_/Base/Home"}, std::pair{"arrow", "Body/_/Base/Player/Rotate"}, std::pair{"death", "Body/_/Base/Dead"},
                             std::pair{"marker", "Body/_/Base/MarkerList/Item_0/Icon_0"}}) {
        IconRecipe recipe;
        if (!movie.special(path, recipe))
            return false;
        specials.emplace(name, std::move(recipe));
    }
    IconRecipe cleared;
    if (movie.itemOverlay("Cleared", cleared))
        specials.emplace("cleared", std::move(cleared));
    TextLayout markerText;
    if (!movie.text("Body/_/Base/MarkerList/Item_0/Text_0", markerText))
        return false;
    for (auto [name, image]: {std::pair{"player", "MENU_MAP_Host"}, std::pair{"bearing", "MENU_MAP_Bearing"}}) {
        IconRecipe recipe;
        if (!movie.image(image, recipe))
            return false;
        recipe.layers[0].matrix[4] = -static_cast<float>(recipe.layers[0].width) * .5f;
        recipe.layers[0].matrix[5] = -static_cast<float>(recipe.layers[0].height) * .5f;
        specials.emplace(name, std::move(recipe));
    }
    // Arrow's GFX matrix uses half-scale; compatibility mode retains the old
    // pixel size and adopts the exact native pivot.
    for (auto &layer: specials["arrow"].layers)
        for (auto &v: layer.matrix)
            v *= 2;
    // Completion glyphs are optional decorations, not required player/map UI.
    // An unavailable mod overlay must not disable otherwise valid resources.
    std::erase_if(specials, [&](const auto &item) {
        return item.first == "cleared" && std::any_of(item.second.layers.begin(), item.second.layers.end(), [&](const auto &layer) {
                   auto region = regions.find(layer.image);
                   return layer.bitmap() && (region == regions.end() || region->second.width != layer.width || region->second.height != layer.height);
               });
    });
    for (const auto &[name, recipe]: specials)
        for (const auto &layer: recipe.layers) {
            auto region = regions.find(layer.image);
            if (layer.bitmap() && (region == regions.end() || region->second.width != layer.width || region->second.height != layer.height))
                return false;
        }
    // Old resources need not define DLC frames that the game never requests.
    // Keep complete recipes; do not fabricate a fallback for missing aliases.
    std::erase_if(icons, [&](const auto &item) {
        return std::any_of(item.second.layers.begin(), item.second.layers.end(), [&](const auto &layer) { return layer.bitmap() && !regions.contains(layer.image); });
    });
    std::unordered_set<std::string> needed;
    auto collect = [&](const auto &recipes) {
        for (const auto &[key, recipe]: recipes)
            for (const auto &layer: recipe.layers)
                if (layer.bitmap())
                    needed.insert(layer.image);
    };
    collect(icons);
    collect(specials);
    for (const auto &[key, recipe]: specials)
        for (const auto &layer: recipe.layers)
            if (key != "cleared" && layer.bitmap())
                sources[regions.at(layer.image).atlas]->required = true;
    std::vector<std::unique_ptr<Atlas>> atlases;
    std::vector<uint32_t> remap(sources.size(), UINT32_MAX);
    // Preserve binder source order for deterministic atlas indices.
    for (size_t i = 0; i < sources.size(); ++i) {
        bool used = std::any_of(needed.begin(), needed.end(), [&](const auto &image) { return regions.at(image).atlas == i; });
        if (used) {
            remap[i] = static_cast<uint32_t>(atlases.size());
            atlases.push_back(std::move(sources[i]));
        }
    }
    std::erase_if(regions, [&](const auto &item) { return !needed.contains(item.first); });
    for (auto &[image, region]: regions)
        region.atlas = remap[region.atlas];
    std::vector<AtlasRegion> resolved;
    auto resolve = [&](auto &recipes) {
        for (auto &[id, recipe]: recipes)
            for (auto &layer: recipe.layers)
                if (layer.bitmap()) {
                    layer.resourceRegion = static_cast<uint32_t>(resolved.size());
                    resolved.push_back(regions.at(layer.image));
                }
    };
    resolve(icons);
    resolve(specials);
    std::vector<AtlasFile> atlasFiles;
    for (uint32_t i = 0; i < atlases.size(); ++i) {
        auto group = std::find_if(atlasFiles.begin(), atlasFiles.end(), [&](const auto &file) { return file.path == atlases[i]->path; });
        if (group == atlasFiles.end()) {
            atlasFiles.push_back({{}, atlases[i]->path, {}});
            group = atlasFiles.end() - 1;
        }
        group->atlases.push_back(i);
    }
    atlases_ = std::move(atlases);
    atlasFiles_ = std::move(atlasFiles);
    regions_ = std::move(regions);
    resolvedRegions_ = std::move(resolved);
    icons_ = std::move(icons);
    specials_ = std::move(specials);
    playerMarkerText_ = std::move(markerText);
    definitionsReady_.store(true, std::memory_order_release);
    if (nativeApi && nativeApi->log) {
        char line[160];
        std::snprintf(line, sizeof(line), "minimap-definitions frames=%u recipes=%zu atlases=%zu\n", movie.iconFrameCount(), icons_.size(), atlases_.size());
        nativeApi->log(line);
    }
    return true;
}

bool Resources::loadDirectory(util::Bytes index, util::Bytes masks) {
    if (directoryReady_.load(std::memory_order_acquire))
        return false;
    std::vector<util::BinderEntry> entries;
    if (!util::parseBinder(index, entries))
        return false;
    std::unordered_map<uint64_t, std::wstring> paths;
    for (const auto &entry: entries) {
        auto name = std::wstring_view(entry.name);
        name = name.substr(name.find_last_of(L"/\\") == std::wstring_view::npos ? 0 : name.find_last_of(L"/\\") + 1);
        auto extension = name.find(L'.');
        if (extension != std::wstring_view::npos)
            name = name.substr(0, extension);
        TileKey key;
        if (tileKey(name, key))
            paths.emplace(key.value(), L"menutpfbnd:/71_MapTile/" + std::wstring(name) + L".tpf.dcx");
    }
    if (!util::parseBinder(masks, entries))
        return false;
    std::array<std::unordered_map<uint32_t, Mask>, 3> mapMasks;
    for (const auto &entry: entries) {
        auto name = util::assetKey(narrow(entry.name));
        int map = name == "MENU_MapTile_M00" ? 0 : name == "MENU_MapTile_M01" ? 1 : name == "MENU_MapTile_M10" ? 2 : -1;
        if (map < 0)
            continue;
        if (!xml(entry.bytes, [&](const std::string &tag, const Attributes &a) {
                if (tag == "MapTileMaskList")
                    return true;
                uint32_t key, mask, exists;
                if (!number(a, "id", key) || !number(a, "mask", mask) || !number(a, "exists", exists) || exists > 1)
                    return false;
                return mapMasks[map].emplace(key, Mask{mask, exists != 0}).second;
            }))
            return false;
    }
    uint32_t mapMask = gameLayout().mapMask;
    for (size_t i = 0; i < mapMasks.size(); ++i)
        if ((mapMask & (1u << i)) && mapMasks[i].empty())
            return false;
    tilePaths_ = std::move(paths);
    masks_ = std::move(mapMasks);
    mapMask_ = mapMask;
    directoryReady_ = true;
    return true;
}

bool Resources::request(File &file, const wchar_t *path, uint32_t flags, const std::vector<const wchar_t *> &names) {
    if (!nativeApi || file.complete || file.token || file.retry > GetTickCount64())
        return false;
    ERFileRequest request{path, flags, names.empty() ? nullptr : names.data(), static_cast<uint32_t>(names.size()), 256ull * 1024 * 1024};
    file.token = nativeApi->requestFile(&request);
    if (!file.token)
        file.retry = GetTickCount64() + 1000;
    return file.token != 0;
}
bool Resources::poll(File &file, util::Bytes &bytes) {
    if (!nativeApi || !file.token)
        return false;
    ERFileData data;
    auto status = nativeApi->pollFile(file.token, nullptr, &data);
    if (status == ER_FILE_SUCCEEDED) {
        bytes = util::Bytes(static_cast<const uint8_t *>(data.data), static_cast<size_t>(data.size));
        return true;
    }
    if (status != ER_FILE_PENDING && status != ER_FILE_QUEUED) {
        status_ = status == ER_FILE_UNSUPPORTED ? "当前游戏版本不支持原生资源" : "资源读取失败，正在重试";
        release(file);
        file.retry = GetTickCount64() + 3000;
    }
    return false;
}
void Resources::release(File &file) {
    if (nativeApi && file.token)
        nativeApi->releaseFile(file.token);
    file.token = 0;
}
void Resources::update() {
    if (!nativeApi) {
        status_ = "需要支持原生资源的 EROverlay 核心";
        return;
    }
    if (canQueueTextures())
        updateJobs();
    // The mod loader may finish mounting overrides after the overlay starts.
    // Do not permanently publish vanilla GFX/layouts from the title screen.
    // Probe readiness only until definitions are published, not every frame.
    if (!definitionsReady_.load(std::memory_order_acquire) && !files_[0].token && !files_[0].complete && nativeApi->readMapState) {
        ERMapState state{};
        if (!nativeApi->readMapState(&state)) {
            status_ = nativeApi->gameCompatible && !nativeApi->gameCompatible() ? "当前游戏版本不支持原生资源" : "正在等待游戏资源";
            return;
        }
    }
    for (size_t i = 0; i < 4; ++i) {
        request(files_[i], PATHS[i], i == 1 || i == 3 ? 0x40 : 0);
        util::Bytes bytes;
        if (poll(files_[i], bytes)) {
            metadata_[i].assign(bytes.begin(), bytes.end());
            release(files_[i]);
            files_[i].complete = true;
        }
    }
    if (!definitionsReady_ && files_[0].complete && files_[1].complete) {
        if (!loadDefinitions(metadata_[0], metadata_[1])) {
            status_ = "图标定义格式不受支持";
            return;
        }
        std::vector<uint8_t>().swap(metadata_[0]);
        std::vector<uint8_t>().swap(metadata_[1]);
    }
    if (!directoryReady_ && files_[2].complete && files_[3].complete) {
        if (!loadDirectory(metadata_[2], metadata_[3])) {
            status_ = "地图索引格式不受支持";
            return;
        }
        std::vector<uint8_t>().swap(metadata_[2]);
        std::vector<uint8_t>().swap(metadata_[3]);
    }
    if (!definitionsReady_.load(std::memory_order_acquire))
        return;
    bool requiredFailed = false;
    for (auto &group: atlasFiles_) {
        auto &file = group.file;
        if (file.complete)
            continue;
        if (!file.token) {
            if (file.retry && file.retry > GetTickCount64())
                continue;
            std::vector<const wchar_t *> names;
            for (auto index: group.atlases)
                if (!atlases_[index]->ready.load(std::memory_order_acquire))
                    names.push_back(atlases_[index]->name.c_str());
            if (names.empty()) {
                file.complete = true;
                continue;
            }
            request(file, group.path.c_str(), 0x40, names);
        }
        if (!file.token)
            continue;
        bool pending = false, failed = false;
        for (auto index: group.atlases) {
            auto &atlas = *atlases_[index];
            if (atlas.ready.load(std::memory_order_acquire))
                continue;
            ERFileData data{};
            auto state = nativeApi->pollFile(file.token, atlas.name.c_str(), &data);
            if (state == ER_FILE_SUCCEEDED && data.data && data.size <= 32ull * 1024 * 1024) {
                util::Bytes bytes(static_cast<const uint8_t *>(data.data), static_cast<size_t>(data.size));
                util::DdsImage image;
                if (util::parseDds(bytes, image)) {
                    atlas.dds.assign(bytes.begin(), bytes.end());
                    atlas.width = image.width;
                    atlas.height = image.height;
                    atlas.ready.store(true, std::memory_order_release);
                    continue;
                }
                status_ = "图集 DDS 格式不受支持，正在重试";
            }
            if (state == ER_FILE_QUEUED || state == ER_FILE_PENDING)
                pending = true;
            else {
                failed = true;
                requiredFailed |= atlas.required;
                if (nativeApi->log) {
                    auto name = narrow(atlas.name);
                    char line[4600];
                    std::snprintf(line, sizeof(line), "minimap-atlas name=%s status=%d native=%d\n", name.c_str(), int(state), data.nativeStatus);
                    nativeApi->log(line);
                }
            }
        }
        if (!pending) {
            release(file);
            if (failed)
                file.retry = GetTickCount64() + 3000;
            else
                file.complete = true;
        }
    }
    if (definitionsReady_ && directoryReady_) {
        bool requiredReady = std::all_of(atlases_.begin(), atlases_.end(), [](const auto &atlas) { return !atlas->required || atlas->ready.load(std::memory_order_acquire); });
        if (requiredReady)
            status_ = "";
        else if (requiredFailed)
            status_ = "必要图集读取失败，正在重试";
        else if (!*status_.load(std::memory_order_acquire) || std::strcmp(status_.load(std::memory_order_acquire), "正在等待游戏资源") == 0)
            status_ = "正在读取游戏图集";
    }
}

void Resources::queueJob(const std::shared_ptr<TextureJob> &job) {
    std::lock_guard lock(jobMutex_);
    queuedJobs_.push_back(job);
    if (job->tile)
        pendingTiles_.fetch_add(1, std::memory_order_relaxed);
    jobsQueued_.store(true, std::memory_order_release);
}
void Resources::updateJobs() {
    // Older cores expose only render-thread upload methods. Keep a bounded
    // render-thread fallback there; current cores do all file/CPU work here.
    auto queueTexture = canQueueTextures() ? nativeApi->queueDdsTexture : nativeApi->createDdsTexture;
    if (jobsQueued_.exchange(false, std::memory_order_acquire)) {
        std::lock_guard lock(jobMutex_);
        incomingJobs_.swap(queuedJobs_);
    }
    activeJobs_.insert(activeJobs_.end(), std::make_move_iterator(incomingJobs_.begin()), std::make_move_iterator(incomingJobs_.end()));
    incomingJobs_.clear();
    size_t budget = 4 * 1024 * 1024;
    unsigned uploads = 0;
    size_t keep = 0;
    for (auto &job: activeJobs_) {
        bool done = false;
        if (job->cancelled.load(std::memory_order_acquire)) {
            release(job->file);
            job->state.store(JobState::Failed, std::memory_order_release);
            done = true;
        } else {
            if (job->source.empty()) {
                request(job->file, job->path.c_str(), 0x40);
                util::Bytes bytes;
                if (poll(job->file, bytes)) {
                    std::vector<util::TpfEntry> entries;
                    util::DdsImage image;
                    if (util::parseTpf(bytes, entries) && entries.size() == 1 && entries[0].dds.size() <= 32ull * 1024 * 1024 && util::parseDds(entries[0].dds, image)) {
                        job->source = entries[0].dds;
                        job->gpuBytes = textureBytes(image);
                    } else {
                        job->state.store(JobState::Failed, std::memory_order_release);
                        done = true;
                    }
                    if (done)
                        release(job->file);
                }
            }
            auto bytes = job->source;
            if (!done && queueTexture && !bytes.empty() && budget && uploads < 4 && (bytes.size() <= budget || !uploads) && (!job->retry || GetTickCount64() >= job->retry)) {
                if (!job->gpuBytes) {
                    util::DdsImage image;
                    if (util::parseDds(bytes, image))
                        job->gpuBytes = textureBytes(image);
                }
                job->texture = queueTexture(bytes.data(), bytes.size());
                if (job->texture) {
                    budget = bytes.size() >= budget ? 0 : budget - bytes.size();
                    ++uploads;
                    release(job->file);
                    job->source = {};
                    job->state.store(JobState::Ready, std::memory_order_release);
                    done = true;
                } else
                    job->retry = GetTickCount64() + 1000;
            }
        }
        if (done) {
            if (job->tile)
                pendingTiles_.fetch_sub(1, std::memory_order_relaxed);
        } else {
            if (&job != &activeJobs_[keep])
                activeJobs_[keep] = std::move(job);
            ++keep;
        }
    }
    activeJobs_.resize(keep);
}
void Resources::cancelJob(std::shared_ptr<TextureJob> &job) {
    if (!job)
        return;
    job->cancelled.store(true, std::memory_order_release);
    cancelledJobs_.push_back(std::move(job));
}
void Resources::collectCancelledJobs() {
    size_t keep = 0;
    for (auto &job: cancelledJobs_) {
        auto state = job->state.load(std::memory_order_acquire);
        if (state == JobState::Ready) {
            if (nativeApi && job->texture)
                nativeApi->retireTexture(job->texture);
        } else if (state == JobState::Pending) {
            if (&job != &cancelledJobs_[keep])
                cancelledJobs_[keep] = std::move(job);
            ++keep;
        }
    }
    cancelledJobs_.resize(keep);
}
bool Resources::finishJob(std::shared_ptr<TextureJob> &job, uint64_t &texture, size_t &gpuBytes) {
    if (!job || job->state.load(std::memory_order_acquire) == JobState::Pending)
        return false;
    texture = job->texture;
    gpuBytes = texture ? job->gpuBytes : 0;
    job.reset();
    return true;
}
void Resources::eraseTile(std::unordered_map<uint64_t, Tile>::iterator it) {
    auto &tile = it->second;
    cancelJob(tile.job);
    if (nativeApi && tile.texture)
        nativeApi->retireTexture(tile.texture);
    tileBytes_ -= tile.gpuBytes;
    tileLru_.erase(tile.lru);
    tiles_.erase(it);
}
void Resources::clearTiles() {
    while (!tiles_.empty())
        eraseTile(tiles_.begin());
    tileJobs_.clear();
}
void Resources::prepareTextures() {
    if (!nativeApi)
        return;
    now_ = GetTickCount64();
    ++atlasFrame_;
    frameUploadBytes_ = 0;
    frameUploads_ = 0;
    if (nativeApi && !canQueueTextures())
        updateJobs();
    collectCancelledJobs();
    size_t keep = 0;
    for (auto id: tileJobs_) {
        auto found = tiles_.find(id);
        if (found == tiles_.end() || !found->second.job)
            continue;
        auto &tile = found->second;
        size_t bytes = 0;
        if (finishJob(tile.job, tile.texture, bytes)) {
            tile.gpuBytes = bytes;
            tile.allocationKnown = false;
            tileBytes_ += bytes;
            if (!tile.texture)
                tile.retry = now_ + 1000;
        } else {
            tileJobs_[keep++] = id;
        }
    }
    tileJobs_.resize(keep);
    // Atlas uploads are requested only by visible recipes. Retire atlases
    // unused for ten seconds so optional mod sets need not remain resident.
    if (now_ >= cacheTrim_) {
        cacheTrim_ = now_ + 1000;
        if (definitionsReady_.load(std::memory_order_acquire))
            for (auto &entry: atlases_) {
                auto &atlas = *entry;
                if (atlas.texture && now_ - atlas.touched >= CACHE_IDLE_MS) {
                    nativeApi->retireTexture(atlas.texture);
                    atlas.texture = 0;
                    atlas.view = {};
                    atlas.polled = UINT64_MAX;
                }
                if (atlas.job && now_ - atlas.touched >= CACHE_IDLE_MS)
                    cancelJob(atlas.job);
            }
        while (!tileLru_.empty()) {
            auto it = tiles_.find(tileLru_.front());
            if (now_ - it->second.touched < CACHE_IDLE_MS)
                break;
            eraseTile(it);
        }
    }
}
void Resources::resetTextures() {
    clearTiles();
    if (definitionsReady_.load(std::memory_order_acquire))
        for (auto &atlas: atlases_) {
            if (nativeApi && atlas->texture)
                nativeApi->retireTexture(atlas->texture);
            atlas->texture = 0;
            atlas->retry = 0;
            atlas->view = {};
            atlas->polled = UINT64_MAX;
            cancelJob(atlas->job);
        }
    collectCancelledJobs();
}
void Resources::stop() {
    resetTextures();
    // Plugin shutdown excludes update/render. Finish cancellation before
    // releasing immutable CPU atlas storage or the native API.
    if (nativeApi)
        updateJobs();
    collectCancelledJobs();
    for (auto &file: files_)
        release(file);
    for (auto &file: atlasFiles_)
        release(file.file);
}
void Resources::beginFrame(uint64_t generation, uint32_t map, const uint32_t (&masks)[3], uint8_t level) {
    if (generation_ != generation)
        clearTiles();
    generation_ = generation;
    map_ = map;
    std::copy(std::begin(masks), std::end(masks), activeMasks_.begin());
    level_ = level;
    ++frame_;
}
bool Resources::tile(uint32_t map, uint16_t x, uint16_t y, TileView &view) {
    view = {};
    if (!nativeApi || !directoryReady_ || map_ > 2 || map > 2 || !(mapMask_ & (1u << map)) || level_ > 2)
        return false;
    static constexpr uint32_t COUNTS[] = {41, 31, 9};
    static constexpr float SPANS[] = {256, 342, 1288};
    if (x >= COUNTS[level_] || y >= COUNTS[level_])
        return false;
    uint32_t key = level_ * 10000 + x * 100 + y;
    auto found = masks_[map].find(key);
    if (found == masks_[map].end() || !found->second.exists)
        return false;
    TileKey tileKey{static_cast<uint8_t>(map == 2 ? 10 : map), level_, x, y, activeMasks_[map] & found->second.allowed};
    auto id = tileKey.value();
    auto it = tiles_.find(id);
    if (it == tiles_.end()) {
        if (!tilePaths_.contains(id))
            return false;
        it = tiles_.try_emplace(id).first;
        it->second.key = tileKey;
        tileLru_.push_back(id);
        it->second.lru = std::prev(tileLru_.end());
    }
    auto &tile = it->second;
    tileLru_.splice(tileLru_.end(), tileLru_, tile.lru);
    tile.touched = now_;
    tile.frame = frame_;
    if (!tile.texture) {
        size_t bytes = 0;
        if (finishJob(tile.job, tile.texture, bytes)) {
            if (!tile.texture)
                tile.retry = now_ + 1000;
            else {
                tile.gpuBytes = bytes;
                tile.allocationKnown = false;
                tileBytes_ += bytes;
            }
        }
        if (!tile.texture && !tile.job && tile.retry <= now_ && pendingTiles_.load(std::memory_order_relaxed) < 8) {
            tile.job = std::make_shared<TextureJob>();
            tile.job->path = tilePaths_.at(id);
            tile.job->tile = true;
            queueJob(tile.job);
            tileJobs_.push_back(id);
        }
    }
    ERTextureView texture;
    if (!tile.texture)
        return false;
    auto state = nativeApi->pollTexture(tile.texture, &texture);
    if (state == ER_TEXTURE_FAILED || state == ER_TEXTURE_INVALID) {
        nativeApi->retireTexture(tile.texture);
        tile.texture = 0;
        tileBytes_ -= tile.gpuBytes;
        tile.gpuBytes = 0;
        tile.allocationKnown = false;
        tile.retry = now_ + 1000;
    }
    if (state != ER_TEXTURE_READY)
        return false;
    if (!tile.allocationKnown && nativeApi->size >= offsetof(EROverlayNativeAPI, textureMemoryBytes) + sizeof(nativeApi->textureMemoryBytes) && nativeApi->textureMemoryBytes) {
        auto bytes = nativeApi->textureMemoryBytes(tile.texture);
        if (bytes && bytes <= SIZE_MAX) {
            tileBytes_ = tileBytes_ - tile.gpuBytes + static_cast<size_t>(bytes);
            tile.gpuBytes = static_cast<size_t>(bytes);
            tile.allocationKnown = true;
        }
    }
    float span = SPANS[level_], top = (COUNTS[level_] - 1 - y) * span;
    view = {tile.texture, x * span, top, (x + 1) * span, top + span, texture};
    return true;
}
void Resources::endFrame() {
    for (auto id: tileJobs_) {
        auto tile = tiles_.find(id);
        if (tile != tiles_.end() && tile->second.frame + 1 < frame_)
            cancelJob(tile->second.job);
    }
    while (tiles_.size() > TILE_CACHE_ENTRIES || tileBytes_ > TILE_CACHE_BYTES) {
        auto oldest = tiles_.find(tileLru_.front());
        // A currently visible working set may exceed the cache budget at a
        // very small zoom. Never evict an image already added to this frame.
        if (oldest->second.frame == frame_)
            break;
        eraseTile(oldest);
    }
}
const IconRecipe *Resources::icon(uint32_t id) const {
    if (!definitionsReady_.load(std::memory_order_acquire))
        return nullptr;
    auto it = icons_.find(id);
    return it == icons_.end() ? nullptr : &it->second;
}
const IconRecipe *Resources::special(const std::string &name) const {
    if (!definitionsReady_.load(std::memory_order_acquire))
        return nullptr;
    auto it = specials_.find(name);
    return it == specials_.end() ? nullptr : &it->second;
}
const TextLayout *Resources::playerMarkerText() const { return definitionsReady_.load(std::memory_order_acquire) ? &playerMarkerText_ : nullptr; }
bool Resources::layerView(const IconLayer &layer, AtlasRegion &region, ERTextureView &view) {
    if (!definitionsReady_.load(std::memory_order_acquire))
        return false;
    if (!nativeApi)
        return false;
    if (layer.resourceRegion < resolvedRegions_.size())
        region = resolvedRegions_[layer.resourceRegion];
    else {
        auto found = regions_.find(layer.image);
        if (found == regions_.end())
            return false;
        region = found->second;
    }
    if (region.atlas >= atlases_.size())
        return false;
    auto &atlas = *atlases_[region.atlas];
    if (!atlas.ready.load(std::memory_order_acquire))
        return false;
    if (region.x + region.width > atlas.width || region.y + region.height > atlas.height)
        return false;
    atlas.touched = now_;
    if (atlas.polled != atlasFrame_) {
        atlas.polled = atlasFrame_;
        size_t bytes = 0;
        if (finishJob(atlas.job, atlas.texture, bytes) && !atlas.texture)
            atlas.retry = now_ + 1000;
        if (!atlas.texture && !atlas.job && !atlas.dds.empty() && atlas.retry <= now_) {
            if (canQueueTextures()) {
                atlas.job = std::make_shared<TextureJob>();
                atlas.job->source = atlas.dds;
                queueJob(atlas.job);
            } else if (frameUploads_ < 4 && (frameUploadBytes_ + atlas.dds.size() <= 4 * 1024 * 1024 || !frameUploads_)) {
                // Compatibility with an earlier core keeps the CPU upload on
                // render, but bounds work and never repeats it for each icon.
                atlas.texture = nativeApi->createDdsTexture(atlas.dds.data(), atlas.dds.size());
                if (atlas.texture) {
                    frameUploadBytes_ += atlas.dds.size();
                    ++frameUploads_;
                } else
                    atlas.retry = now_ + 1000;
            }
        }
        atlas.view = {};
        atlas.state = atlas.texture ? nativeApi->pollTexture(atlas.texture, &atlas.view) : ER_TEXTURE_PENDING;
        if (atlas.state == ER_TEXTURE_FAILED || atlas.state == ER_TEXTURE_INVALID) {
            nativeApi->retireTexture(atlas.texture);
            atlas.texture = 0;
            atlas.retry = now_ + 1000;
        }
    }
    view = atlas.view;
    return atlas.state == ER_TEXTURE_READY;
}

} // namespace er::minimap
