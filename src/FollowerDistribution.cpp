#include "FollowerDistribution.h"
#include "WhoEditThatAPI.h"
#include "Manager.h"
#include "Prisma.h"
#include <memory>
#include <atomic>

namespace FollowerDistribution
{
    namespace
    {
        std::atomic_uint64_t epoch{0};
        bool loading = true;
        std::set<RE::FormID> busy, restored, failed;
        std::map<RE::FormID, Values> contributions;
        using Reply = std::function<void(bool)>;
        template<class Result>
        void ReplyTo(const Result* result, void* data)
        {
            std::unique_ptr<Reply> reply(static_cast<Reply*>(data));
            (*reply)(result && static_cast<unsigned>(result->status) == 0);
        }

        bool WriteValue(RE::FormID actorID, const std::string& name, float value, Reply reply)
        {
            auto* api = WhoEditThat::API::GetAPI();
            if (!api || !api->IsReady()) return false;
            WhoEditThat::API::ClientRegistration registration;
            registration.clientID = "NSM";
            registration.displayName = "New Skill Menu";
            const auto client = api->RegisterClient(&registration);
            if (!client) return false;
            const auto key = "nsm:progression:" + name;
            auto callback = std::make_unique<Reply>(std::move(reply));
            WhoEditThat::API::ActorValueContributionRequest request;
            request.client = client;
            request.actorFormID = actorID;
            request.mutationKey = key.c_str();
            request.targetActorValue = name.c_str();
            request.fixedValue = value;
            request.channel = WhoEditThat::API::ModifierChannel::kPermanent;
            request.operation = WhoEditThat::API::NumericOperation::kFlat;
            if (!api->QueueUpsertActorValue(&request,
                ReplyTo<WhoEditThat::API::Result>, callback.get())) return false;
            callback.release();
            return true;
        }

        bool RemoveValue(RE::FormID actorID, const std::string& name, Reply reply)
        {
            auto* api = WhoEditThat::API::GetAPI();
            if (!api || !api->IsReady()) return false;
            WhoEditThat::API::ClientRegistration registration;
            registration.clientID = "NSM";
            registration.displayName = "New Skill Menu";
            const auto client = api->RegisterClient(&registration);
            if (!client) return false;
            const auto key = "nsm:progression:" + name;
            auto callback = std::make_unique<Reply>(std::move(reply));
            WhoEditThat::API::ContributionRequest request;
            request.client = client;
            request.actorFormID = actorID;
            request.mutationKey = key.c_str();
            if (!api->QueueRemoveActorValue(&request,
                ReplyTo<WhoEditThat::API::Result>, callback.get())) return false;
            callback.release();
            return true;
        }

        struct Operation : std::enable_shared_from_this<Operation>
        {
            RE::FormID actorID{};
            std::uint64_t generation{};
            Perks beforePerks, afterPerks;
            Values before, after;
            std::vector<std::string> names;
            std::size_t index = 0;
            bool rollback = false;
            Completion completion;
            bool Current() const { return generation == epoch && !loading; }

            void Finish(bool success)
            {
                if (!Current()) return;
                if (success) {
                    contributions[actorID] = after;
                    restored.insert(actorID);
                    failed.erase(actorID);
                }
                busy.erase(actorID);
                if (!success) {
                    restored.erase(actorID);
                    failed.insert(actorID);
                }

                if (completion) completion(success);
                Prisma::SendUpdateToUI();
            }

            void Fail()
            {
                if (!Current()) return;
                if (rollback) {
                    logger::error("[Distribution] Compensation failed actor={:08X}; reconciliation pending", actorID);
                    Finish(false);
                    return;
                }
                rollback = true;
                index = 0;
                Step();
            }

            void Step()
            {
                if (!Current()) return;
                auto self = shared_from_this();

                if (index < names.size()) {
                    const auto name = names[index++];
                    const auto& values = rollback ? before : after;
                    const auto found = values.find(name);
                    const auto reply = [self](bool ok) {
                        if (ok) self->Step();
                        else self->Fail();
                    };
                    const bool queued = found == values.end() ?
                        RemoveValue(actorID, name, reply) :
                        WriteValue(actorID, name, found->second, reply);
                    if (!queued) Fail();
                    return;
                }

                auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
                if (!actor) { Fail(); return; }
                const auto& desired = rollback ? beforePerks : afterPerks;
                const auto& previous = rollback ? afterPerks : beforePerks;
                for (auto id : previous) {
                    if (desired.contains(id)) continue;
                    auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(id);
                    if (perk && actor->HasPerk(perk)) actor->RemovePerk(perk);
                    if (perk && actor->HasPerk(perk)) { Fail(); return; }
                }
                for (auto id : desired) {
                    auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(id);
                    if (!perk) { Fail(); return; }
                    if (!actor->HasPerk(perk)) actor->AddPerk(perk);
                    if (!actor->HasPerk(perk)) { Fail(); return; }
                }
                Finish(!rollback);
            }
        };
    }

    bool Loading() { return loading; }
    std::uint64_t Epoch() { return epoch; }
    bool Busy(RE::Actor* actor) { return loading || (actor && busy.contains(actor->GetFormID())); }
    bool Ready()
    {
        auto* wet = WhoEditThat::API::GetAPI();
        return !loading && wet && wet->IsReady();
    }
    Perks Purchased(RE::Actor* actor)
    {
        Perks result;
        for (const auto& [id, purchase] : Manager::GetSingleton()->GetPurchasedPerks(actor)) result.insert(id);
        return result;
    }
    Values Contributions(RE::Actor* actor)
    {
        auto it = actor ? contributions.find(actor->GetFormID()) : contributions.end();
        return it == contributions.end() ? Values{} : it->second;
    }
    bool Apply(RE::Actor* actor, Perks perks, Values values, Completion completion)
    {
        if (!actor || actor->IsPlayerRef() || Busy(actor)) return false;
        for (const auto& [name, value] : values)
            if (name.empty() || name.size() > 63 || !std::isfinite(value)) return false;
        auto operation = std::make_shared<Operation>();
        operation->actorID = actor->GetFormID();
        operation->generation = epoch;
        operation->beforePerks = Purchased(actor);
        operation->afterPerks = std::move(perks);
        operation->before = Contributions(actor);
        operation->after = std::move(values);
        operation->completion = std::move(completion);
        std::set<std::string> names;
        for (const auto& [name, value] : operation->before) {
            const auto found = operation->after.find(name);
            if (found == operation->after.end() || found->second != value) names.insert(name);
        }
        for (const auto& [name, value] : operation->after) {
            const auto found = operation->before.find(name);
            if (found == operation->before.end() || found->second != value) names.insert(name);
        }
        operation->names.assign(names.begin(), names.end());
        if (!operation->names.empty()) {
            auto* wet = WhoEditThat::API::GetAPI();
            if (!wet || !wet->IsReady()) {
                logger::warn("[Distribution] WhoEditThat integration is unavailable");
                return false;
            }
        }
        busy.insert(actor->GetFormID());
        operation->Step();
        return true;
    }
    void BeginLoad()
    {
        loading = true;
        ++epoch;
        busy.clear();
        restored.clear();
        failed.clear();
        contributions.clear();
    }
    void Restore(RE::Actor* actor)
    {
        if (!actor || actor->IsPlayerRef() || Busy(actor)) return;
        const auto id = actor->GetFormID();
        auto* manager = Manager::GetSingleton();
        if (!manager->actorProgressStates.contains(id) && !contributions.contains(id)) return;
        for (auto perkID : Purchased(actor)) {
            auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
            if (perk && !actor->HasPerk(perk)) actor->AddPerk(perk);
        }
        if (restored.contains(id) || failed.contains(id) || !Ready()) return;
        restored.insert(id);
        // A save can be taken between asynchronous steps. Read the owned ledger
        // so uncommitted numeric keys absent from NSM's save are also undone.
        auto* api = WhoEditThat::API::GetAPI();
        WhoEditThat::API::ClientRegistration registration;
        registration.clientID = "NSM";
        registration.displayName = "New Skill Menu";
        WhoEditThat::API::ContributionScopeRequest request;
        request.client = api->RegisterClient(&registration);
        request.actorFormID = id;
        request.mutationKeyPrefix = "nsm:progression:";
        struct Context { RE::FormID id; std::uint64_t generation; };
        auto context = std::make_unique<Context>(Context{id, epoch.load()});
        busy.insert(id);
        if (api->QueueListActorValues(&request, [](const WhoEditThat::API::ActorValueListResult* result, void* data) {
            std::unique_ptr<Context> context(static_cast<Context*>(data));
            if (context->generation != epoch || loading) return;
            busy.erase(context->id);
            auto* target = RE::TESForm::LookupByID<RE::Actor>(context->id);
            if (!result || result->status != WhoEditThat::API::Status::kSuccess || !target) {
                restored.erase(context->id);
                failed.insert(context->id);
                return;
            }
            auto desired = Contributions(target);
            Values actual;
            for (std::uint32_t i = 0; i < result->entryCount; ++i)
                actual[result->entries[i].actorValue] = result->entries[i].appliedDelta;
            const auto committed = desired;
            contributions[context->id] = std::move(actual);
            const bool accepted = Apply(target, Purchased(target), std::move(desired), {});
            contributions[context->id] = committed;
            if (!accepted) restored.erase(context->id);
        }, context.get())) context.release();
        else { busy.erase(id); restored.erase(id); failed.insert(id); }
    }
    void Resume()
    {
        loading = false;
        // Loaded actors also reconcile through the roster and object-load sink.
        std::vector<RE::FormID> ids;
        for (const auto& [id, state] : Manager::GetSingleton()->actorProgressStates) ids.push_back(id);
        for (auto id : ids) Restore(RE::TESForm::LookupByID<RE::Actor>(id));
    }
    void RegisterEvents()
    {
        struct Sink final : RE::BSTEventSink<RE::TESObjectLoadedEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* event,
                RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
            {
                if (event && event->loaded && !loading) {
                    const auto id = event->formID;
                    const auto generation = epoch.load();
                    SKSE::GetTaskInterface()->AddTask([id, generation] {
                        if (generation != epoch || loading) return;
                        restored.erase(id);
                        failed.erase(id);
                        Restore(RE::TESForm::LookupByID<RE::Actor>(id));
                    });
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };
        static Sink sink;
        RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink(&sink);
    }
    void Save(SKSE::SerializationInterface* serialization)
    {
        nlohmann::json data = nlohmann::json::array();
        for (const auto& [id, values] : contributions) data.push_back({{"actor", id}, {"values", values}});
        const auto text = data.dump();
        if (serialization->OpenRecord('NDST', 1)) serialization->WriteRecordData(text.data(), static_cast<std::uint32_t>(text.size()));
    }
    void Load(SKSE::SerializationInterface* serialization, std::uint32_t length)
    {
        if (length > 16 * 1024 * 1024) return;
        std::string text(length, '\0');
        if (!serialization->ReadRecordData(text.data(), length)) return;
        try {
            for (const auto& entry : nlohmann::json::parse(text)) {
                RE::FormID id = 0;
                if (!serialization->ResolveFormID(entry.at("actor").get<RE::FormID>(), id)) continue;
                auto values = entry.at("values").get<Values>();
                bool valid = values.size() <= 1024;
                for (const auto& [name, value] : values) valid &= !name.empty() && name.size() <= 63 && std::isfinite(value);
                if (valid) contributions[id] = std::move(values);
            }
        } catch (const std::exception& e) { logger::error("[Distribution] Invalid save record: {}", e.what()); }
    }
}
