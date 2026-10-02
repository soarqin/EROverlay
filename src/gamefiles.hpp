#pragma once

#include <memory>

#include "nativeapi.h"

namespace er {

class GameFiles {
public:
    GameFiles();
    ~GameFiles() noexcept;
    GameFiles(const GameFiles &) = delete;
    GameFiles &operator=(const GameFiles &) = delete;
    GameFiles(GameFiles &&) = delete;
    GameFiles &operator=(GameFiles &&) = delete;
    [[nodiscard]] bool compatible() const;
    [[nodiscard]] uint64_t request(const ERFileRequest &request);
    [[nodiscard]] ERFileStatus poll(uint64_t token, const wchar_t *part, ERFileData &data);
    void release(uint64_t token);
    void stop();
    [[nodiscard]] bool readMapState(ERMapState &state);
    [[nodiscard]] uintptr_t findParamTable(uint32_t group) const;
    [[nodiscard]] bool readEventFlag(uint32_t id) const;
    [[nodiscard]] bool readGameLayout(ERGameLayout &layout) const;

private:
    friend struct NativeFileVerifier;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

inline std::unique_ptr<GameFiles> gGameFiles;

} // namespace er
