#pragma once

#include <map>
#include <string>
#include <vector>

struct PaidResource
{
    std::string resourceId;
    std::string sourceType;
    std::string sourceLocator;
    float amount = 0.0f;
    bool shared = false;
};

struct PerkPurchaseRecord
{
    int perkPointCost = 0;
    int actorLevelAtPurchase = 0;
    std::vector<PaidResource> resources;
};

struct ActorProgressState
{
    std::string actorKey;
    int perkPoints = 0;
    int lastObservedLevel = 0;
    int highestRewardedLevel = 0;
    int pendingLevelUps = 0;
    int resetCount = 0;
    std::map<RE::FormID, PerkPurchaseRecord> purchasedPerks;
};
