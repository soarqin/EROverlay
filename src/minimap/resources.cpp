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
            if (layer.bitmap())
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
    icons_ = std::move(icons);
    specials_ = std::move(specials);
    playerMarkerText_ = std::move(markerText);
    definitionsReady_.store(true, std::memory_order_release);
    return true;
}

bool Resources::loadDirectory(util::Bytes index, util::Bytes masks) {
    if (directoryReady_.load(std::memory_order_acquire))
        return false;
    std::vector<util::BinderEntry> entries;
    if (!util::parseBinder(index, entries))
        return false;
    std::unordered_set<std::string> names;
    for (const auto &entry: entries) {
        auto name = narrow(entry.name);
        if (name.ends_with(".dcx"))
            name.resize(name.size() - 4);
        names.insert(util::assetKey(name));
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
    tileNames_ = std::move(names);
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
        std::vector<const wchar_t *> names;
        for (auto index: group.atlases)
            if (!atlases_[index]->ready.load(std::memory_order_acquire))
                names.push_back(atlases_[index]->name.c_str());
        if (names.empty()) {
            file.complete = true;
            continue;
        }
        request(file, group.path.c_str(), 0x40, names);
        if (!file.token)
            continue;
        bool pending = false, failed = false;
        for (auto index: group.atlases) {
            auto &atlas = *atlases_[index];
            if (atlas.ready.load(std::memory_order_acquire))
                continue;
            ERFileData data{};
            auto state = nativeApi->pollFile(file.token, atlas.name.c_str(), &data);
            if (state == ER_FILE_SUCCEEDED && data.data && data.size <= SIZE_MAX) {
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

void Resources::clearTiles() {
    for (auto &[key, tile]: tiles_) {
        release(tile.file);
        if (nativeApi && tile.texture)
            nativeApi->retireTexture(tile.texture);
    }
    tiles_.clear();
}
void Resources::prepareTextures() {
    if (!nativeApi || !definitionsReady_.load(std::memory_order_acquire))
        return;
    for (auto &entry: atlases_) {
        auto &atlas = *entry;
        if (!atlas.ready.load(std::memory_order_acquire))
            continue;
        if (!atlas.texture && !atlas.dds.empty() && atlas.retry <= GetTickCount64())
            atlas.texture = nativeApi->createDdsTexture(atlas.dds.data(), atlas.dds.size());
    }
    if (nativeApi->log && !atlasesLogged_ && std::all_of(atlases_.begin(), atlases_.end(), [](const auto &atlas) { return atlas->texture != 0; })) {
        char line[80];
        std::snprintf(line, sizeof(line), "minimap-atlases-enqueued=%zu\n", atlases_.size());
        nativeApi->log(line);
        atlasesLogged_ = true;
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
        }
    atlasesLogged_ = false;
}
void Resources::stop() {
    resetTextures();
    for (auto &file: files_)
        release(file);
    for (auto &file: atlasFiles_)
        release(file.file);
}
void Resources::beginFrame(uint64_t generation, uint32_t map, const uint32_t (&masks)[3], uint8_t level) {
    if (generation_ != generation || map_ != map || !std::equal(activeMasks_.begin(), activeMasks_.end(), std::begin(masks)) || level_ != level)
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
    char name[128];
    std::snprintf(name, sizeof(name), "MENU_MapTile_M%02u_L%u_%02u_%02u_%08x", tileKey.map, tileKey.level, x, y, tileKey.variant);
    if (!tileNames_.contains(name))
        return false;
    auto &tile = tiles_[tileKey.value()];
    tile.key = tileKey;
    tile.touched = frame_;
    if (!tile.texture) {
        if (!tile.file.token) {
            size_t pending = 0;
            for (const auto &[id, t]: tiles_)
                if (t.file.token)
                    ++pending;
            if (pending < 8) {
                std::string virtualPath = std::string("menutpfbnd:/71_MapTile/") + name + ".tpf.dcx";
                std::wstring path(virtualPath.begin(), virtualPath.end());
                request(tile.file, path.c_str(), 0x40);
            }
        }
        util::Bytes bytes;
        if (poll(tile.file, bytes)) {
            std::vector<util::TpfEntry> entries;
            if (util::parseTpf(bytes, entries) && entries.size() == 1)
                tile.texture = nativeApi->createDdsTexture(entries[0].dds.data(), entries[0].dds.size());
            release(tile.file);
            if (!tile.texture)
                tile.file.retry = GetTickCount64() + 1000;
        }
    }
    ERTextureView texture;
    if (!tile.texture)
        return false;
    auto state = nativeApi->pollTexture(tile.texture, &texture);
    if (state == ER_TEXTURE_FAILED || state == ER_TEXTURE_INVALID) {
        nativeApi->retireTexture(tile.texture);
        tile.texture = 0;
        tile.file.retry = GetTickCount64() + 1000;
    }
    if (state != ER_TEXTURE_READY)
        return false;
    float span = SPANS[level_], top = (COUNTS[level_] - 1 - y) * span;
    view = {tile.texture, x * span, top, (x + 1) * span, top + span};
    return true;
}
void Resources::endFrame() {
    while (tiles_.size() > 512) {
        auto oldest = std::min_element(tiles_.begin(), tiles_.end(), [](const auto &a, const auto &b) { return a.second.touched < b.second.touched; });
        if (oldest == tiles_.end() || oldest->second.touched == frame_)
            break;
        release(oldest->second.file);
        if (nativeApi && oldest->second.texture)
            nativeApi->retireTexture(oldest->second.texture);
        tiles_.erase(oldest);
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
    auto found = regions_.find(layer.image);
    if (found == regions_.end() || !nativeApi)
        return false;
    region = found->second;
    if (region.atlas >= atlases_.size())
        return false;
    auto &atlas = *atlases_[region.atlas];
    if (!atlas.ready.load(std::memory_order_acquire))
        return false;
    if (region.x + region.width > atlas.width || region.y + region.height > atlas.height)
        return false;
    if (!atlas.texture && !atlas.dds.empty() && atlas.retry <= GetTickCount64())
        atlas.texture = nativeApi->createDdsTexture(atlas.dds.data(), atlas.dds.size());
    if (!atlas.texture)
        return false;
    auto state = nativeApi->pollTexture(atlas.texture, &view);
    if (state == ER_TEXTURE_FAILED || state == ER_TEXTURE_INVALID) {
        nativeApi->retireTexture(atlas.texture);
        atlas.texture = 0;
        atlas.retry = GetTickCount64() + 1000;
    }
    return state == ER_TEXTURE_READY;
}

} // namespace er::minimap
