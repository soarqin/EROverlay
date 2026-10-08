#pragma once

#include <string>
#include <string_view>
#include <map>
#include <type_traits>

namespace er {

// Parse a leading number like std::stoll/std::stod without throwing; a
// trailing '%' scales the floating value by 1/100.
[[nodiscard]] bool parseConfigNumber(std::string_view text, long long &value);
[[nodiscard]] bool parseConfigNumber(std::string_view text, double &value);

class Config {
public:
    void loadDir(const wchar_t *dir);
    void loadFile(const wchar_t *filename);

    [[nodiscard]] const std::string &operator[](const std::string &key) const;
    [[nodiscard]] const std::string &get(const std::string &key, const std::string &defaultValue) const;
    [[nodiscard]] std::wstring getw(const std::string &key, const std::wstring &defaultValue) const;
    [[nodiscard]] bool enabled(const std::string &key) const;
    // Invalid or empty values return the default instead of throwing.
    template<typename T, std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
    [[nodiscard]] T get(const std::string &key, const T &defaultValue) const {
        const auto it = entries_.find(key);
        std::conditional_t<std::is_integral_v<T>, long long, double> value;
        if (it == entries_.end() || !parseConfigNumber(it->second, value)) {
            return defaultValue;
        }
        return static_cast<T>(value);
    }
    [[nodiscard]] int getVirtualKey(const std::string &key, int defaultValue) const;

private:
    void loadSingleFile(const std::wstring &filename, const std::string &modname = "");

private:
    std::map<std::string, std::string> entries_;
};

extern Config gConfig;

}