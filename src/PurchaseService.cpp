#include "PurchaseService.h"

#include "Manager.h"
#include "ResourceService.h"
#include "SnapshotService.h"

PurchaseService::Result PurchaseService::Purchase(
    RE::Actor* actor,
    RE::BGSPerk* perk,
    int perkPointCost,
    const nlohmann::json& customCosts,
    const nlohmann::json& resources)
{
    if (!actor || !perk) return { false, "invalid_actor_or_perk" };
    if (perkPointCost < 0) return { false, "invalid_perk_point_cost" };

    auto ownership = SnapshotService::GetPerkOwnership(actor, perk);
    if (ownership.owned) return { false, "already_owned" };

    auto manager = Manager::GetSingleton();
    const int previousPerkPoints = manager->GetActorPerkPoints(actor);
    if (previousPerkPoints < perkPointCost) {
        return { false, "insufficient_perk_points" };
    }

    if (!manager->SpendActorPerkPoints(actor, perkPointCost)) {
        return { false, "perk_point_debit_failed" };
    }

    std::vector<PaidResource> paidResources;
    std::string resourceError;
    if (!ResourceService::Debit(
        actor,
        customCosts,
        resources,
        paidResources,
        resourceError)) {
        manager->ModActorPerkPoints(actor, perkPointCost);
        return { false, resourceError };
    }

    actor->AddPerk(perk);
    if (!actor->HasPerk(perk)) {
        ResourceService::Refund(actor, paidResources);
        manager->ModActorPerkPoints(actor, perkPointCost);
        logger::error(
            "[Economy] Purchase rolled back: AddPerk failed actor={:08X} perk={:08X}",
            actor->GetFormID(),
            perk->GetFormID());
        return { false, "add_perk_failed" };
    }

    manager->RecordPurchasedPerk(
        actor,
        perk->GetFormID(),
        perkPointCost,
        paidResources);

    logger::info(
        "[Economy] Purchase committed actor='{}' actorID={:08X} perk={:08X} "
        "pointsBefore={} pointsPaid={} pointsAfter={} customResources={}",
        actor->GetName(),
        actor->GetFormID(),
        perk->GetFormID(),
        previousPerkPoints,
        perkPointCost,
        manager->GetActorPerkPoints(actor),
        paidResources.size());
    return { true, "purchased" };
}
