#define NOMINMAX
#include <cstdio>
#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "minimap/resources.hpp"

static std::vector<uint8_t> file(const char *path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    auto size = stream.tellg();
    if (size < 0)
        return {};
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char *>(bytes.data()), size);
    return bytes;
}
int main() {
    using namespace er::minimap;
    auto gfx = file("build/ida/sprite-probe/run-24636-34293203/worldmap.gfx");
    auto layouts = file("build/ida/probes/run-26836-32694765/common-layouts.bin");
    auto index = file("build/ida/probes/run-26836-32694765/map-index.bin");
    auto masks = file("build/ida/probes/run-26836-32694765/map-masks.bin");
    Resources resources;
    bool definitions = resources.loadDefinitions(gfx, layouts);
    bool directory = resources.loadDirectory(index, masks);
    std::printf("definitions=%d directory=%d icons=%zu\n", definitions, directory, resources.iconCount());
    if (!definitions || !directory)
        return 1;
    for (auto id: {1u, 2u, 3u, 15u, 49u, 83u, 84u, 102u, 103u, 107u, 348u}) {
        auto recipe = resources.icon(id);
        if (!recipe)
            return 2;
        std::printf("icon %u:", id);
        for (const auto &layer: recipe->layers)
            std::printf(" %s", layer.bitmap() ? layer.image.c_str() : "vector");
        std::printf("\n");
    }
    auto death = resources.special("death");
    if (!death || death->layers.size() != 1 || death->layers[0].image != "MENU_MAP_DropSoul" || death->layers[0].width != 86 || death->layers[0].height != 92)
        return 3;
    GfxMovie movie;
    if (!movie.parse(gfx))
        return 4;
    nlohmann::json output;
    for (uint32_t i = 1; i <= movie.iconFrameCount(); ++i) {
        IconRecipe recipe;
        bool present = movie.icon(i, recipe);
        auto &layers = output["icons"][std::to_string(i)] = nlohmann::json::array();
        if (!present)
            continue;
        for (const auto &layer: recipe.layers) {
            nlohmann::json value = {{"image", layer.image}, {"matrix", layer.matrix}, {"depth", layer.depth}, {"width", layer.width}, {"height", layer.height}};
            if (!layer.bitmap()) {
                value["strokes"] = nlohmann::json::array();
                for (const auto &stroke: layer.shape.strokes)
                    value["strokes"].push_back({{"color", stroke.color}, {"width", stroke.width}});
                value["commands"] = nlohmann::json::array();
                for (const auto &command: layer.shape.commands)
                    value["commands"].push_back(
                        {{"op", command.op}, {"to", {command.to.x, command.to.y}}, {"control", {command.control.x, command.control.y}}, {"stroke", command.stroke}});
            }
            layers.push_back(std::move(value));
        }
    }
    std::ofstream("build/native/production-recipes.json") << output.dump(2);
    for (auto size: {size_t{7}, gfx.size() - 6})
        if (movie.parse(er::util::Bytes(gfx).first(size)))
            return 4;
    auto tpf = file("build/ida/probes/run-26836-32694765/surface-v8000.tpf");
    std::vector<er::util::TpfEntry> entries;
    er::util::DdsImage dds;
    if (!er::util::parseTpf(tpf, entries) || entries.size() != 1 || !er::util::parseDds(entries[0].dds, dds) || dds.width != 256 || dds.height != 256 || dds.format != 98)
        return 5;
    if (er::util::parseDds(entries[0].dds.first(147), dds))
        return 6;
    er::util::Bytes extracted;
    if (!er::util::findTpfDds(tpf, entries[0].name, extracted) || extracted.size() != entries[0].dds.size() ||
        !std::equal(extracted.begin(), extracted.end(), entries[0].dds.begin()) || er::util::findTpfDds(tpf, L"not-a-texture", extracted))
        return 7;
    std::puts("PASS: production GFX/XML/BHF4/TPF/DDS parsers and death recipe.");
}
