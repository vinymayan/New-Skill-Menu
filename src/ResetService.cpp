#include "ResetService.h"

#include "Manager.h"
#include "ResourceService.h"

namespace
{
    std::vector<std::string> Split(std::string_view value, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream stream{ std::string(value) };
        std::string item;
        while (std::getline(stream, item, delimiter)) result.push_back(item);
        return result;
    }

    RE::FormID ResolvePerkID(std::string_view value)
    {
        const auto tokens = Split(value, '|');
        if (tokens.size() != 2) return 0;
        try {
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ?
                dataHandler->LookupFormID(
                    static_cast<RE::FormID>(
                        std::stoul(tokens[1], nullptr, 16)),
                    tokens[0]) :
                0;
        }
        catch (...) {
            return 0;
        }
    }

    bool Includes(
        const std::unordered_set<RE::FormID>& allowed,
        RE::FormID perkId)
    {
        return allowed.empty() || allowed.contains(perkId);
    }
}

std::unordered_set<RE::FormID> ResetService::PerksInTree(const json& tree)
{
    std::unordered_set<RE::FormID> result;
    if (!tree.contains("nodes") || !tree["nodes"].is_array()) return result;
    for (const auto& node : tree["nodes"]) {
        if (auto id = ResolvePerkID(node.value("perk", "")); id != 0) {
            result.insert(id);
        }
        if (!node.contains("nextRanks") || !node["nextRanks"].is_array()) continue;
        for (const auto& rank : node["nextRanks"]) {
            if (auto id = ResolvePerkID(rank.value("perk", "")); id != 0) {
                result.insert(id);
            }
        }
    }
    return result;
}

ResetService::json ResetService::Preview(
    RE::Actor* actor,
    const std::unordered_set<RE::FormID>& allowedPerks,
    int maxResets)
{
    json preview{
        { "allowed", false },
        { "reason", "invalid_actor" },
        { "perkCount", 0 },
        { "perkPoints", 0 },
        { "resources", json::object() },
        { "perks", json::array() }
    };
    if (!actor) return preview;

    auto manager = Manager::GetSingleton();
    const int resetCount = manager->GetActorResetCount(actor);
    preview["resetCount"] = resetCount;
    preview["maxResets"] = maxResets;
    if (maxResets >= 0 && resetCount >= maxResets) {
        preview["reason"] = "reset_limit_reached";
        return preview;
    }

    auto purchases = manager->GetPurchasedPerks(actor);
    for (const auto& [perkId, purchase] : purchases) {
        if (!Includes(allowedPerks, perkId)) continue;
        preview["perkCount"] = preview["perkCount"].get<int>() + 1;
        preview["perkPoints"] =
            preview["perkPoints"].get<int>() +
            purchase.perkPointCost;

        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkId);
        preview["perks"].push_back({
            { "id", fmt::format("{:08X}", perkId) },
            { "name", perk ? perk->GetName() : "" },
            { "perkPoints", purchase.perkPointCost }
        });

        for (const auto& resource : purchase.resources) {
            auto& amount = preview["resources"][resource.resourceId];
            amount = amount.is_number() ? amount.get<float>() + resource.amount :
                resource.amount;
        }
    }

    preview["allowed"] = preview["perkCount"].get<int>() > 0;
    preview["reason"] = preview["allowed"] ? "allowed" : "nothing_to_reset";
    return preview;
}

ResetService::json ResetService::Execute(
    RE::Actor* actor,
    const std::unordered_set<RE::FormID>& allowedPerks,
    const json& resources,
    int maxPerkPoints,
    int maxResets,
    bool countReset)
{
    (void)resources;
    auto result = Preview(actor, allowedPerks, maxResets);
    if (!result.value("allowed", false)) return result;

    auto manager = Manager::GetSingleton();
    auto purchases = manager->GetPurchasedPerks(actor);
    int refundedPoints = 0;
    int removed = 0;

    for (const auto& [perkId, ignored] : purchases) {
        (void)ignored;
        if (!Includes(allowedPerks, perkId)) continue;
        auto purchase = manager->RemovePurchasedPerkRecord(actor, perkId);
        if (!purchase) continue;

        if (auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkId)) {
            if (actor->HasPerk(perk)) actor->RemovePerk(perk);
        }
        refundedPoints += purchase->perkPointCost;
        ResourceService::Refund(actor, purchase->resources);
        removed++;

        logger::info(
            "[Economy] Reset removed actor={:08X} perk={:08X} points={} resources={}",
            actor->GetFormID(),
            perkId,
            purchase->perkPointCost,
            purchase->resources.size());
    }

    if (refundedPoints > 0) {
        manager->ModActorPerkPoints(actor, refundedPoints, maxPerkPoints);
    }
    if (countReset && removed > 0) {
        manager->RecordActorReset(actor);
    }

    result["removed"] = removed;
    result["refundedPerkPoints"] = refundedPoints;
    result["newPerkPointBalance"] = manager->GetActorPerkPoints(actor);
    result["resetCount"] = manager->GetActorResetCount(actor);
    result["success"] = removed > 0;
    return result;
}
