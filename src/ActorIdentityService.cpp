#include "ActorIdentityService.h"

namespace
{
    std::uint32_t LocalFormID(RE::FormID formID)
    {
        return (formID & 0xFF000000) == 0xFE000000 ?
            (formID & 0xFFF) :
            (formID & 0xFFFFFF);
    }

    std::string FormKey(RE::TESForm* form)
    {
        if (!form) {
            return {};
        }

        if (auto file = form->GetFile(0)) {
            return fmt::format(
                "{}|{:X}",
                file->GetFilename(),
                LocalFormID(form->GetFormID()));
        }
        return {};
    }
}

std::string ActorIdentityService::RuntimeKey(RE::Actor* actor)
{
    return actor ? fmt::format("{:08X}", actor->GetFormID()) : "";
}

std::string ActorIdentityService::StableKey(RE::Actor* actor)
{
    if (!actor) {
        return {};
    }
    if (actor->IsPlayerRef()) {
        return "player";
    }

    if (auto referenceKey = FormKey(actor); !referenceKey.empty()) {
        return "ref:" + referenceKey;
    }

    auto actorBase = actor->GetActorBase();
    if (actorBase && actorBase->IsUnique()) {
        if (auto baseKey = FormKey(actorBase); !baseKey.empty()) {
            return "unique-base:" + baseKey;
        }
    }

    // Dynamic non-unique actors cannot be made globally stable without a
    // framework-provided identity. Keep them isolated inside the current save.
    return "runtime:" + RuntimeKey(actor);
}

std::string ActorIdentityService::RuleKey(RE::Actor* actor)
{
    if (!actor) {
        return {};
    }
    if (actor->IsPlayerRef()) {
        return "player";
    }

    if (auto referenceKey = FormKey(actor); !referenceKey.empty()) {
        return referenceKey;
    }
    if (auto baseKey = FormKey(actor->GetActorBase()); !baseKey.empty()) {
        return baseKey;
    }
    return RuntimeKey(actor);
}
