#ifndef SHIP_INIT_HPP
#define SHIP_INIT_HPP

#ifdef __cplusplus

#include <vector>
#include <set>
#include <unordered_map>
#include <string>
#include <functional>

#ifdef COMBO_BUILD
#include <ship/resource/CrossRMRegistry.h>
#endif

struct ShipInit {
    static std::unordered_map<std::string, std::vector<std::function<void()>>>& GetAll() {
        static std::unordered_map<std::string, std::vector<std::function<void()>>> shipInitFuncs;
        return shipInitFuncs;
    }

    static void InitAll() {
        ShipInit::Init("*");
    }

    static void Init(const std::string& path) {
#ifdef COMBO_BUILD
        // ComboShip: init funcs load MM resources; pin MM's RM (callers may run with the other game's active).
        Ship::OwnRMScope rmScope("mm");
#endif
        auto& shipInitFuncs = ShipInit::GetAll();
        for (const auto& initFunc : shipInitFuncs[path]) {
            initFunc();
        }
    }
};

struct RegisterShipInitFunc {
    RegisterShipInitFunc(std::function<void()> initFunc, const std::set<std::string>& updatePaths = {}) {
        auto& shipInitFuncs = ShipInit::GetAll();

        shipInitFuncs["*"].push_back(initFunc);

        for (const auto& path : updatePaths) {
            shipInitFuncs[path].push_back(initFunc);
        }
    }
};

#endif // __cplusplus

#endif // SHIP_INIT_HPP
