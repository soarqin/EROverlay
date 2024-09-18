#pragma once

#include <string>
#include <map>
#include <vector>
#include <type_traits>

namespace er {

class Config {
public:
    void load(const std::wstring &filename);

    [[nodiscard]] const std::string &operator[](const std::string &key) const;
    [[nodiscard]] const std::string &get(const std::string &key, const std::string &defaultValue) const;
    [[nodiscard]] std::wstring getw(const std::string &key, const std::wstring &defaultValue) const;
    [[nodiscard]] bool enabled(const std::string &key) const;
    template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    [[nodiscard]] T get(const std::string &key, const T &defaultValue) const {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            return defaultValue;
        }
        return static_cast<T>(std::stoll(it->second));
    }
    template<typename T, std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
    [[nodiscard]] T get(const std::string &key, const T &defaultValue) const {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            return defaultValue;
        }
        return static_cast<T>(std::stod(it->second));
    }
    [[nodiscard]] std::vector<int> getVirtualKey(const std::string &key, const std::vector<int> &defaultValue) const;

private:
    std::map<std::string, std::string> entries_;
};

extern Config gConfig;

}