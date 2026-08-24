#include "RequirementService.h"

#include <unordered_set>

RequirementService::ValidationResult
RequirementService::ValidatePurchaseSnapshot(
    const json& snapshot,
    std::string_view perkId)
{
    if (!snapshot.contains("trees") || !snapshot["trees"].is_array()) {
        return { false, "snapshot_missing_trees" };
    }

    for (const auto& tree : snapshot["trees"]) {
        if (!tree.contains("nodes") || !tree["nodes"].is_array()) continue;
        for (const auto& node : tree["nodes"]) {
            if (node.value("perk", "") == perkId) {
                if (node.value("isUnlocked", false)) {
                    return { false, "already_owned" };
                }
                return node.value("canUnlock", false) ?
                    ValidationResult{ true, "allowed" } :
                    ValidationResult{ false, "requirements_not_met" };
            }

            if (!node.contains("nextRanks") ||
                !node["nextRanks"].is_array()) continue;
            for (const auto& rank : node["nextRanks"]) {
                if (rank.value("perk", "") != perkId) continue;
                if (rank.value("isUnlocked", false)) {
                    return { false, "already_owned" };
                }
                return rank.value("canUnlock", false) ?
                    ValidationResult{ true, "allowed" } :
                    ValidationResult{ false, "rank_requirements_not_met" };
            }
        }
    }
    return { false, "perk_not_found_in_snapshot" };
}

RequirementService::json RequirementService::ValidateRules(const json& rules)
{
    json errors = json::array();
    if (!rules.is_array()) {
        errors.push_back({
            { "index", -1 },
            { "field", "rules" },
            { "message", "Rules must be an array." }
        });
        return errors;
    }

    static const std::unordered_set<std::string> scopes{
        "", "all", "player", "followers", "actor"
    };

    for (std::size_t index = 0; index < rules.size(); ++index) {
        const auto& rule = rules[index];
        if (!rule.is_object()) {
            errors.push_back({
                { "index", index },
                { "field", "rule" },
                { "message", "Rule must be an object." }
            });
            continue;
        }

        if (!rule.contains("level") ||
            !rule["level"].is_number_integer() ||
            rule["level"].get<int>() < 0) {
            errors.push_back({
                { "index", index },
                { "field", "level" },
                { "message", "Level must be a non-negative integer." }
            });
        }

        const auto scope = rule.value("scope", "");
        if (!scopes.contains(scope)) {
            errors.push_back({
                { "index", index },
                { "field", "scope" },
                { "message", "Unknown rule scope." }
            });
        }
        if (scope == "actor" && rule.value("actorKey", "").empty()) {
            errors.push_back({
                { "index", index },
                { "field", "actorKey" },
                { "message", "Actor-scoped rules require actorKey." }
            });
        }

        for (const auto field : {
            "perksPerLevel",
            "skillPointsPerLevel",
            "maxSkillPointsSpendablePerLevel",
            "skillCap" }) {
            if (rule.contains(field) &&
                (!rule[field].is_number_integer() ||
                    rule[field].get<int>() < -1)) {
                errors.push_back({
                    { "index", index },
                    { "field", field },
                    { "message", "Value must be an integer greater than or equal to -1." }
                });
            }
        }
        if (rule.contains("maxPerkPoints") &&
            (!rule["maxPerkPoints"].is_number_integer() ||
                rule["maxPerkPoints"].get<int>() < 0)) {
            errors.push_back({
                { "index", index },
                { "field", "maxPerkPoints" },
                { "message", "Maximum perk points must be a non-negative integer." }
            });
        }
        if (rule.contains("maxResetsPerActor") &&
            (!rule["maxResetsPerActor"].is_number_integer() ||
                rule["maxResetsPerActor"].get<int>() < -1)) {
            errors.push_back({
                { "index", index },
                { "field", "maxResetsPerActor" },
                { "message", "Maximum resets must be -1 or a non-negative integer." }
            });
        }

        if (rule.contains("resourceRewards")) {
            if (!rule["resourceRewards"].is_array()) {
                errors.push_back({
                    { "index", index },
                    { "field", "resourceRewards" },
                    { "message", "Resource rewards must be an array." }
                });
            }
            else {
                for (const auto& reward : rule["resourceRewards"]) {
                    if (!reward.is_object() ||
                        reward.value("resourceId", "").empty() ||
                        !reward.contains("amount") ||
                        !reward["amount"].is_number() ||
                        reward["amount"].get<float>() < 0.0f) {
                        errors.push_back({
                            { "index", index },
                            { "field", "resourceRewards" },
                            { "message", "Each resource reward needs resourceId and a non-negative amount." }
                        });
                        break;
                    }
                }
            }
        }
    }
    return errors;
}
