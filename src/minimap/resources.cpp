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

#include "resources.hpp"

namespace er::minimap {
const EROverlayNativeAPI *nativeApi = nullptr;
Resources gResources;
namespace {
constexpr const wchar_t *ATLAS_NAMES[] = {L"SB_MapCursor", L"SB_MapCursor_02", L"SB_MapCursor_03_dlc", L"SB_Chara"};
constexpr const char *ATLAS_KEYS[] = {"SB_MapCursor", "SB_MapCursor_02", "SB_MapCursor_03_dlc", "SB_Chara"};
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
    GfxMovie movie;
    std::vector<util::BinderEntry> entries;
    if (!movie.parse(gfx) || !util::parseBinder(layouts, entries))
        return false;
    std::unordered_map<std::string, AtlasRegion> regions;
    for (const auto &entry: entries) {
        int atlas = -1;
        if (!xml(entry.bytes, [&](const std::string &tag, const Attributes &a) {
                if (tag == "TextureAtlas") {
                    auto image = a.find("imagePath");
                    if (image == a.end())
                        return false;
                    auto key = util::assetKey(image->second);
                    for (int i = 0; i < 4; ++i)
                        if (key == ATLAS_KEYS[i])
                            atlas = i;
                } else if (tag == "SubTexture" && atlas >= 0) {
                    auto name = a.find("name");
                    AtlasRegion region{};
                    uint32_t half;
                    region.atlas = atlas;
                    if (name == a.end() || !number(a, "x", region.x) || !number(a, "y", region.y) || !number(a, "width", region.width) || !number(a, "height", region.height) ||
                        !number(a, "half", half) || half || !region.width || !region.height || region.x > 16384 || region.y > 16384 || region.width > 16384 ||
                        region.height > 16384 || !regions.emplace(util::assetKey(name->second), region).second)
                        return false;
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
    // Do not accept missing aliases while silently keeping a partial atlas.
    for (const auto &[id, recipe]: icons)
        for (const auto &layer: recipe.layers) {
            if (layer.bitmap() && !regions.contains(layer.image))
                return false;
        }
    regions_ = std::move(regions);
    icons_ = std::move(icons);
    specials_ = std::move(specials);
    playerMarkerText_ = std::move(markerText);
    definitionsReady_ = true;
    return true;
}

bool Resources::loadDirectory(util::Bytes index, util::Bytes masks) {
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
    for (const auto &values: mapMasks)
        if (values.empty())
            return false;
    tileNames_ = std::move(names);
    masks_ = std::move(mapMasks);
    directoryReady_ = true;
    return true;
}

bool Resources::request(File &file, const wchar_t *path, uint32_t flags, bool atlas) {
    if (!nativeApi || file.complete || file.token || file.retry > GetTickCount64())
        return false;
    ERFileRequest request{path, flags, atlas ? ATLAS_NAMES : nullptr, atlas ? 4u : 0u, 256ull * 1024 * 1024};
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
    auto &file = files_[4];
    request(file, PATHS[4], 0x40, true);
    if (file.token) {
        bool ready = true;
        for (size_t i = 0; i < 4; ++i) {
            ERFileData data;
            auto state = nativeApi->pollFile(file.token, ATLAS_NAMES[i], &data);
            if (state == ER_FILE_SUCCEEDED) {
                util::Bytes bytes(static_cast<const uint8_t *>(data.data), static_cast<size_t>(data.size));
                util::DdsImage image;
                if (!util::parseDds(bytes, image)) {
                    status_ = "图集 DDS 格式不受支持";
                    ready = false;
                    break;
                }
                atlases_[i].dds.assign(bytes.begin(), bytes.end());
                atlases_[i].width = image.width;
                atlases_[i].height = image.height;
            } else {
                ready = false;
                if (state != ER_FILE_QUEUED && state != ER_FILE_PENDING) {
                    release(file);
                    file.retry = GetTickCount64() + 3000;
                }
                break;
            }
        }
        if (ready) {
            release(file);
            file.complete = true;
            atlasesReady_.store(true, std::memory_order_release);
        }
    }
    if (definitionsReady_ && directoryReady_ && files_[4].complete)
        status_ = "";
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
    if (!nativeApi || !atlasesReady_.load(std::memory_order_acquire))
        return;
    for (auto &atlas: atlases_) {
        if (!atlas.texture && !atlas.dds.empty() && atlas.retry <= GetTickCount64())
            atlas.texture = nativeApi->createDdsTexture(atlas.dds.data(), atlas.dds.size());
    }
    if (nativeApi->log && std::all_of(atlases_.begin(), atlases_.end(), [](const auto &atlas) { return atlas.texture != 0; })) {
        static bool logged = false;
        if (!logged) {
            nativeApi->log("minimap-atlases-enqueued=4\n");
            logged = true;
        }
    }
}
void Resources::resetTextures() {
    clearTiles();
    for (auto &atlas: atlases_) {
        if (nativeApi && atlas.texture)
            nativeApi->retireTexture(atlas.texture);
        atlas.texture = 0;
    }
}
void Resources::stop() {
    resetTextures();
    for (auto &file: files_)
        release(file);
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
    if (!nativeApi || !directoryReady_ || map_ > 2 || map > 2 || level_ > 2)
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
    if (!atlasesReady_.load(std::memory_order_acquire) || !definitionsReady_.load(std::memory_order_acquire))
        return false;
    auto found = regions_.find(layer.image);
    if (found == regions_.end() || !nativeApi)
        return false;
    region = found->second;
    auto &atlas = atlases_[region.atlas];
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
