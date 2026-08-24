#pragma once

#include <string>

namespace ActorIdentityService
{
    std::string RuntimeKey(RE::Actor* actor);
    std::string StableKey(RE::Actor* actor);
    std::string RuleKey(RE::Actor* actor);
}
