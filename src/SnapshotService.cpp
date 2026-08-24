#include "SnapshotService.h"

#include "ActorIdentityService.h"
#include "Manager.h"

SnapshotService::PerkOwnership SnapshotService::GetPerkOwnership(
    RE::Actor* actor,
    RE::BGSPerk* perk)
{
    PerkOwnership result;
    if (!actor || !perk) return result;

    result.runtime = actor->HasPerk(perk);

    auto actorBase = actor->GetActorBase();
    result.actorBase =
        actorBase && actorBase->GetPerkIndex(perk).has_value();

    auto templateBase = actor->GetTemplateBase();
    result.templateBase =
        templateBase &&
        templateBase != actorBase &&
        templateBase->GetPerkIndex(perk).has_value();

    result.purchased =
        Manager::GetSingleton()->WasPerkPurchasedForActor(
            actor,
            perk->GetFormID());

    result.owned =
        result.runtime ||
        result.actorBase ||
        result.templateBase ||
        result.purchased;

    if (result.purchased) result.source = "purchased";
    else if (result.actorBase || result.templateBase) result.source = "natural";
    else if (result.runtime) result.source = "external";

    if (!actor->IsPlayerRef() && result.owned) {
        const auto key =
            (static_cast<std::uint64_t>(actor->GetFormID()) << 32) |
            perk->GetFormID();
        static std::unordered_set<std::uint64_t> logged;
        if (logged.insert(key).second) {
            logger::info(
                "[ActorPerks] actor='{}' actorID={:08X} perk='{}' perkID={:08X} "
                "source={} runtime={} actorBase={} templateBase={} ledger={}",
                actor->GetName(),
                actor->GetFormID(),
                perk->GetName(),
                perk->GetFormID(),
                result.source,
                result.runtime,
                result.actorBase,
                result.templateBase,
                result.purchased);
        }
    }

    return result;
}

nlohmann::json SnapshotService::BuildActorSummary(RE::Actor* actor)
{
    if (!actor) return nullptr;
    return {
        { "id", ActorIdentityService::RuntimeKey(actor) },
        { "stableKey", ActorIdentityService::StableKey(actor) },
        { "ruleKey", ActorIdentityService::RuleKey(actor) },
        { "name", actor->GetName() },
        { "level", actor->GetLevel() },
        { "kind", actor->IsPlayerRef() ? "player" : "follower" }
    };
}
