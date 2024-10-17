#pragma once

#include <vector>
#include <string>

namespace er::achievements {

struct Achievement {
    std::string name;
    std::string displayName;
    std::string description;
};

class Data {
public:
    void load();
    void update();

private:
    std::vector<Achievement> achievements_;
    std::vector<int> locked_;
};

extern Data gData;

}
