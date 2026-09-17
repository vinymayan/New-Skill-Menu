#pragma once

#include <Windows.h>
#include <cstdint>

namespace WhoEditThat::API
{
    inline constexpr std::uint32_t kInterfaceVersion = 1;
    using ClientHandle = std::uint64_t;
    inline constexpr ClientHandle kInvalidClient = 0;

    enum class Operation : std::uint32_t
    {
        kUpsertActorValue = 1,
        kRemoveActorValue,
        kLookupActorValue,
        kUpsertDisplayName,
        kRemoveDisplayName,
        kListActorValues,
        kRemoveActorValuesByPrefix,
        kUpsertActorScale,
        kRemoveActorScale,
        kLookupActorScale,
        kListActorScales,
        kRemoveActorScalesByPrefix
    };

    enum class Status : std::uint32_t
    {
        kSuccess = 0,
        kNotReady,
        kLoadInProgress,
        kContextChanged,
        kCancelled,
        kInvalidArgument,
        kNotOwner,
        kNotFound,
        kActorUnavailable,
        kUnsupportedActorValue,
        kConflict,
        kPersistenceFailed,
        kInternalError
    };

    enum class NumericOperation : std::uint32_t
    {
        kFlat = 0,
        kPercent,
        kMultiply
    };

    enum class NumericSource : std::uint32_t
    {
        kFixed = 0,
        kGlobal,
        kActorValue
    };

    enum class ModifierChannel : std::uint32_t
    {
        kPermanent = 0,
        kTemporary
    };

    struct ClientRegistration
    {
        std::uint32_t structSize{ sizeof(ClientRegistration) };
        const char* clientID{ nullptr };
        const char* displayName{ nullptr };
    };

    struct ActorValueContributionRequest
    {
        std::uint32_t structSize{ sizeof(ActorValueContributionRequest) };
        ClientHandle client{ kInvalidClient };
        std::uint32_t actorFormID{ 0 };
        const char* mutationKey{ nullptr };
        const char* targetActorValue{ nullptr };
        NumericOperation operation{ NumericOperation::kFlat };
        NumericSource source{ NumericSource::kFixed };
        ModifierChannel channel{ ModifierChannel::kPermanent };
        // Percent uses percentage points (30 means +30%). Multiply uses a
        // factor (1.30 means x1.30). Source values are captured internally.
        float fixedValue{ 0.0F };
        std::uint32_t sourceGlobalFormID{ 0 };
        const char* sourceActorValue{ nullptr };
        float sourceMultiplier{ 1.0F };
    };

    struct ContributionRequest
    {
        std::uint32_t structSize{ sizeof(ContributionRequest) };
        ClientHandle client{ kInvalidClient };
        std::uint32_t actorFormID{ 0 };
        const char* mutationKey{ nullptr };
    };

    struct ActorScaleContributionRequest
    {
        std::uint32_t structSize{ sizeof(ActorScaleContributionRequest) };
        ClientHandle client{ kInvalidClient };
        std::uint32_t actorFormID{ 0 };
        const char* mutationKey{ nullptr };
        NumericOperation operation{ NumericOperation::kFlat };
        NumericSource source{ NumericSource::kFixed };
        // Flat adds scale units (0.10 means +0.10). Percent uses percentage
        // points, while Multiply uses a factor. The final scale must remain
        // between 0.10 and 10.00.
        float fixedValue{ 0.0F };
        std::uint32_t sourceGlobalFormID{ 0 };
        const char* sourceActorValue{ nullptr };
        float sourceMultiplier{ 1.0F };
    };

    // A non-empty prefix scopes the operation to contributions owned by the
    // registered client. actorFormID must identify one actor.
    struct ContributionScopeRequest
    {
        std::uint32_t structSize{ sizeof(ContributionScopeRequest) };
        ClientHandle client{ kInvalidClient };
        std::uint32_t actorFormID{ 0 };
        const char* mutationKeyPrefix{ nullptr };
    };

    struct DisplayNameRequest
    {
        std::uint32_t structSize{ sizeof(DisplayNameRequest) };
        ClientHandle client{ kInvalidClient };
        std::uint32_t actorFormID{ 0 };
        const char* mutationKey{ nullptr };
        const char* displayName{ nullptr };
        std::int32_t priority{ 0 };
    };

    struct Result
    {
        std::uint32_t structSize{ sizeof(Result) };
        Operation operation{ Operation::kLookupActorValue };
        Status status{ Status::kInternalError };
        std::uint32_t actorFormID{ 0 };
        NumericOperation numericOperation{ NumericOperation::kFlat };
        ModifierChannel channel{ ModifierChannel::kPermanent };
        float previousDelta{ 0.0F };
        float appliedDelta{ 0.0F };
        std::uint32_t affectedCount{ 0 };
        char ownerID[96]{};
        char mutationKey[128]{};
        char actorValue[64]{};
        char message[256]{};
    };

    struct ActorValueEntry
    {
        std::uint32_t structSize{ sizeof(ActorValueEntry) };
        std::uint32_t actorFormID{ 0 };
        NumericOperation numericOperation{ NumericOperation::kFlat };
        ModifierChannel channel{ ModifierChannel::kPermanent };
        float appliedDelta{ 0.0F };
        char mutationKey[128]{};
        char actorValue[64]{};
    };

    struct ActorValueListResult
    {
        std::uint32_t structSize{ sizeof(ActorValueListResult) };
        Status status{ Status::kInternalError };
        std::uint32_t actorFormID{ 0 };
        std::uint32_t entryCount{ 0 };
        // Valid only for the duration of the callback.
        const ActorValueEntry* entries{ nullptr };
        char message[256]{};
    };

    struct ActorScaleEntry
    {
        std::uint32_t structSize{ sizeof(ActorScaleEntry) };
        std::uint32_t actorFormID{ 0 };
        NumericOperation numericOperation{ NumericOperation::kFlat };
        float appliedDelta{ 0.0F };
        char mutationKey[128]{};
    };

    struct ActorScaleListResult
    {
        std::uint32_t structSize{ sizeof(ActorScaleListResult) };
        Status status{ Status::kInternalError };
        std::uint32_t actorFormID{ 0 };
        std::uint32_t entryCount{ 0 };
        // Valid only for the duration of the callback.
        const ActorScaleEntry* entries{ nullptr };
        char message[256]{};
    };

    using Callback = void (*)(const Result*, void* userData);
    using ListCallback = void (*)(const ActorValueListResult*, void* userData);
    using ScaleListCallback = void (*)(const ActorScaleListResult*, void* userData);

    class IWhoEditThatAPI
    {
    public:
        virtual ~IWhoEditThatAPI() = default;
        virtual std::uint32_t GetVersion() const noexcept = 0;
        virtual bool IsReady() const noexcept = 0;
        virtual ClientHandle RegisterClient(
            const ClientRegistration*) noexcept = 0;
        virtual bool QueueUpsertActorValue(
            const ActorValueContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueRemoveActorValue(
            const ContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueLookupActorValue(
            const ContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueUpsertDisplayName(
            const DisplayNameRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueRemoveDisplayName(
            const ContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueListActorValues(
            const ContributionScopeRequest*, ListCallback, void*) noexcept = 0;
        virtual bool QueueRemoveActorValuesByPrefix(
            const ContributionScopeRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueUpsertActorScale(
            const ActorScaleContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueRemoveActorScale(
            const ContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueLookupActorScale(
            const ContributionRequest*, Callback, void*) noexcept = 0;
        virtual bool QueueListActorScales(
            const ContributionScopeRequest*, ScaleListCallback, void*) noexcept = 0;
        virtual bool QueueRemoveActorScalesByPrefix(
            const ContributionScopeRequest*, Callback, void*) noexcept = 0;
    };

    inline IWhoEditThatAPI* GetAPI() noexcept
    {
        const auto module = GetModuleHandleA("WhoEditThat.dll");
        if (!module) {
            return nullptr;
        }
        using Getter = void* (*)();
        const auto getter = reinterpret_cast<Getter>(
            GetProcAddress(module, "GetWhoEditThatAPI"));
        if (!getter) {
            return nullptr;
        }
        auto* api = static_cast<IWhoEditThatAPI*>(getter());
        return api && api->GetVersion() == kInterfaceVersion ? api : nullptr;
    }
}
