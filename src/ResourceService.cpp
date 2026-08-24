#include "ResourceService.h"

namespace
{
    ResourceService::json g_definitions = ResourceService::json::array();

    std::vector<std::string> Split(std::string_view value, char delimiter)
    {
        std::vector<std::string> result;
        std::stringstream stream{ std::string(value) };
        std::string item;
        while (std::getline(stream, item, delimiter)) {
            result.push_back(item);
        }
        return result;
    }

    RE::FormID ResolveFormID(std::string_view value)
    {
        auto tokens = Split(value, '|');
        if (tokens.size() != 2) return 0;
        try {
            const auto localID = static_cast<RE::FormID>(
                std::stoul(tokens[1], nullptr, 16));
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ?
                dataHandler->LookupFormID(localID, tokens[0]) :
                0;
        }
        catch (...) {
            return 0;
        }
    }

    RE::ActorValue ResolveActorValue(std::string_view value)
    {
        if (value == "Vampirism") return RE::ActorValue::kVampirePerks;
        if (value == "Werewolf") return RE::ActorValue::kWerewolfPerks;
        if (value == "Health") return RE::ActorValue::kHealth;
        if (value == "Magicka") return RE::ActorValue::kMagicka;
        if (value == "Stamina") return RE::ActorValue::kStamina;

        auto normalized = std::string(value);
        std::erase(normalized, ' ');
        std::erase(normalized, '-');
        return RE::ActorValueList::LookupActorValueByName(normalized.c_str());
    }

    const ResourceService::json* FindDefinition(
        std::string_view resourceId,
        const ResourceService::json& definitions)
    {
        if (!definitions.is_array()) return nullptr;
        for (const auto& definition : definitions) {
            if (definition.value("id", "") == resourceId) {
                return &definition;
            }
        }
        return nullptr;
    }

    bool ApplyDelta(
        RE::Actor* actor,
        const PaidResource& resource,
        float delta)
    {
        if (resource.sourceType == "actorValue") {
            if (!actor) return false;
            auto actorValue = ResolveActorValue(resource.sourceLocator);
            auto owner = actor->AsActorValueOwner();
            if (actorValue == RE::ActorValue::kNone || !owner) return false;
            owner->ModBaseActorValue(actorValue, delta);
            return true;
        }

        if (resource.sourceType == "global") {
            auto formID = ResolveFormID(resource.sourceLocator);
            auto global = RE::TESForm::LookupByID<RE::TESGlobal>(formID);
            if (!global) return false;
            global->value += delta;
            return true;
        }
        return false;
    }

    bool ResolvePaidResource(
        const ResourceService::json& definition,
        std::string_view resourceId,
        float amount,
        PaidResource& result)
    {
        const auto actorValue = definition.value("actorValue", "");
        const auto global = definition.value("glob", "");
        if (!actorValue.empty()) {
            result = {
                std::string(resourceId),
                "actorValue",
                actorValue,
                amount,
                false
            };
            return true;
        }
        if (!global.empty()) {
            result = {
                std::string(resourceId),
                "global",
                global,
                amount,
                true
            };
            return true;
        }
        return false;
    }
}

void ResourceService::SetDefinitions(const json& definitions)
{
    g_definitions = definitions.is_array() ? definitions : json::array();
}

ResourceService::json ResourceService::GetDefinitions()
{
    return g_definitions;
}

float ResourceService::GetValue(
    RE::Actor* actor,
    std::string_view resourceId,
    const json& definitions)
{
    const auto definition = FindDefinition(resourceId, definitions);
    if (!definition) return 0.0f;

    PaidResource resource;
    if (!ResolvePaidResource(*definition, resourceId, 0.0f, resource)) {
        return 0.0f;
    }

    if (resource.sourceType == "actorValue") {
        if (!actor) return 0.0f;
        const auto actorValue = ResolveActorValue(resource.sourceLocator);
        auto owner = actor->AsActorValueOwner();
        return actorValue != RE::ActorValue::kNone && owner ?
            owner->GetActorValue(actorValue) :
            0.0f;
    }

    const auto formID = ResolveFormID(resource.sourceLocator);
    const auto global = RE::TESForm::LookupByID<RE::TESGlobal>(formID);
    return global ? global->value : 0.0f;
}

ResourceService::json ResourceService::BuildValues(
    RE::Actor* actor,
    const json& definitions)
{
    json values = json::object();
    if (!definitions.is_array()) return values;
    for (const auto& definition : definitions) {
        const auto id = definition.value("id", "");
        if (!id.empty()) {
            values[id] = GetValue(actor, id, definitions);
        }
    }
    return values;
}

bool ResourceService::Debit(
    RE::Actor* actor,
    const json& costs,
    const json& definitions,
    std::vector<PaidResource>& paid,
    std::string& error)
{
    paid.clear();
    if (!costs.is_array()) return true;

    for (const auto& cost : costs) {
        const auto resourceId = cost.value("resourceId", "");
        const float amount = cost.value("amount", 0.0f);
        if (resourceId.empty() || !std::isfinite(amount) || amount <= 0.0f) {
            continue;
        }

        const auto definition = FindDefinition(resourceId, definitions);
        PaidResource resource;
        if (!definition ||
            !ResolvePaidResource(*definition, resourceId, amount, resource)) {
            error = "unknown_resource:" + resourceId;
            Refund(actor, paid);
            paid.clear();
            return false;
        }

        const float balance = GetValue(actor, resourceId, definitions);
        if (balance + 0.0001f < amount) {
            error = "insufficient_resource:" + resourceId;
            Refund(actor, paid);
            paid.clear();
            return false;
        }

        if (!ApplyDelta(actor, resource, -amount)) {
            error = "resource_debit_failed:" + resourceId;
            Refund(actor, paid);
            paid.clear();
            return false;
        }
        paid.push_back(resource);
    }
    return true;
}

void ResourceService::Refund(
    RE::Actor* actor,
    const std::vector<PaidResource>& paid)
{
    for (auto it = paid.rbegin(); it != paid.rend(); ++it) {
        if (ApplyDelta(actor, *it, it->amount)) {
            logger::info(
                "[Economy] Refunded resource={} amount={} actor={:08X} shared={}",
                it->resourceId,
                it->amount,
                actor ? actor->GetFormID() : 0,
                it->shared);
        }
    }
}

bool ResourceService::Credit(
    RE::Actor* actor,
    std::string_view resourceId,
    float amount,
    const json& definitions,
    std::string& error)
{
    if (!std::isfinite(amount) || amount <= 0.0f) {
        error = "invalid_amount";
        return false;
    }
    const auto definition = FindDefinition(resourceId, definitions);
    PaidResource resource;
    if (!definition ||
        !ResolvePaidResource(*definition, resourceId, amount, resource)) {
        error = "unknown_resource:" + std::string(resourceId);
        return false;
    }
    if (!ApplyDelta(actor, resource, amount)) {
        error = "resource_credit_failed:" + std::string(resourceId);
        return false;
    }
    logger::info(
        "[Economy] Credited resource={} amount={} actor={:08X} shared={}",
        resourceId,
        amount,
        actor ? actor->GetFormID() : 0,
        resource.shared);
    return true;
}

bool ResourceService::Modify(
    RE::Actor* actor,
    std::string_view resourceId,
    float amount,
    const json& definitions,
    std::string& error)
{
    if (!std::isfinite(amount) || amount == 0.0f) {
        error = "invalid_amount";
        return false;
    }
    if (amount > 0.0f) {
        return Credit(actor, resourceId, amount, definitions, error);
    }

    json costs = json::array({
        {
            { "resourceId", resourceId },
            { "amount", -amount }
        }
    });
    std::vector<PaidResource> paid;
    return Debit(actor, costs, definitions, paid, error);
}
