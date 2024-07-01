#pragma once

#include <string>
#include <map>
#include <vector>

namespace er {

class Config {
public:
    void load(const std::wstring &filename);

    [[nodiscard]] const std::string &operator[](const std::string &key) const;
    [[nodiscard]] const std::string &get(const std::string &key, const std::string &defaultValue) const;
    [[nodiscard]] std::wstring getw(const std::string &key, const std::wstring &defaultValue) const;
    [[nodiscard]] bool enabled(const std::string &key) const;
    [[nodiscard]] std::vector<int> getVirtualKey(const std::string &key, const std::vector<int> &defaultValue) const;

private:
    std::map<std::string, std::string> entries_;
};

extern Config gConfig;

}