#pragma once
#include "EconomyTypes.h"
#include <functional>
#include <set>

namespace FollowerDistribution
{
    using Values = std::map<std::string, float>;
    using Perks = std::set<RE::FormID>;
    using Completion = std::function<void(bool)>;
    bool Busy(RE::Actor* actor);
    bool Failed(RE::Actor* actor);
    bool Ready();
    bool Loading();
    std::uint64_t Epoch();
    Perks Purchased(RE::Actor* actor);
    Values Contributions(RE::Actor* actor);
    bool Apply(RE::Actor* actor, Perks perks, Values values, Completion completion);
    void RegisterEvents();
    void BeginLoad();
    void Resume();
    void Restore(RE::Actor* actor);
    void Save(SKSE::SerializationInterface* serialization);
    void Load(SKSE::SerializationInterface* serialization, std::uint32_t length);
}
