#include "Manager.h"
#include "ActorIdentityService.h"
#include "Prisma.h"
#include "API_ActorValueGenerator.h"

#include <cctype>
#include <filesystem>
#include <fstream>

void Manager::PopulateAllLists() {
    if (_isPopulated) return;

    logger::info("Iniciando escaneamento de FormTypes...");

    PopulateList<RE::TESRace>("Race", [](RE::TESRace* race) -> bool {
        return race->GetPlayable();
        });

    PopulateList<RE::BGSPerk>("Perk", [](RE::BGSPerk* perk) -> bool {
        if ((perk->formFlags & RE::BGSPerk::RecordFlags::kNonPlayable) != 0) return false;
        if (!perk->data.playable) return false;
        return true;
        });

    PopulateList<RE::TESQuest>("Quest", [](RE::TESQuest* quest) -> bool {
        return quest != nullptr;
        });

    PopulateList<RE::SpellItem>("Spell", [](RE::SpellItem* spell) -> bool {
        return spell != nullptr;
        });

    PopulateList<RE::BGSLocation>("Location", [](RE::BGSLocation* loc) -> bool {
        return loc != nullptr && loc->worldLocMarker;
        });

    PopulateList<RE::TESFaction>("Faction", [](RE::TESFaction* faction) -> bool {
        return faction != nullptr;
        });

    PopulateList<RE::TESObjectBOOK>("Book", [](RE::TESObjectBOOK* book) -> bool {
        return book != nullptr;
        });

    PopulateList<RE::TESShout>("Shout", [](RE::TESShout* shout) -> bool {
        return shout != nullptr;
        });

    PopulateList<RE::TESGlobal>("Global", [](RE::TESGlobal* glob) -> bool {
        return glob != nullptr;
        });


    /*PopulateList<RE::SpellItem>("Spell");
    PopulateList<RE::TESNPC>("NPC");
    PopulateList<RE::TESObjectWEAP>("Weapon");
    PopulateList<RE::TESObjectARMO>("Armor");*/
    
    // --- NOVOS TIPOS ADICIONADOS ---
    /*PopulateList<RE::AlchemyItem>("Potion");
    PopulateList<RE::IngredientItem>("Ingredient");
    PopulateList<RE::ScrollItem>("Scroll");
    PopulateList<RE::TESAmmo>("Ammo");
    PopulateList<RE::TESObjectMISC>("Misc");
    PopulateList<RE::TESKey>("Key");
    PopulateList<RE::TESClass>("Class");
    PopulateList<RE::BGSLocation>("Location");*/
    _isPopulated = true;
    for (auto cb : _readyCallbacks) {
        if (cb) cb();
    }
    _readyCallbacks.clear();
}

void Manager::RefreshLists(std::string_view a_signatures) {
    const auto includes = [a_signatures](std::string_view a_signature) {
        std::size_t begin = 0;
        while (begin <= a_signatures.size()) {
            const auto end = a_signatures.find(',', begin);
            auto token = a_signatures.substr(begin, end == std::string_view::npos ? a_signatures.size() - begin : end - begin);
            while (!token.empty() && token.front() == ' ') token.remove_prefix(1);
            while (!token.empty() && token.back() == ' ') token.remove_suffix(1);
            if (token == a_signature) return true;
            if (end == std::string_view::npos) break;
            begin = end + 1;
        }
        return false;
    };

    if (a_signatures.empty() || includes("All")) {
        _isPopulated = false;
        PopulateAllLists();
        return;
    }
    if (includes("PERK")) {
        PopulateList<RE::BGSPerk>("Perk", [](RE::BGSPerk* perk) {
            return (perk->formFlags & RE::BGSPerk::RecordFlags::kNonPlayable) == 0 && perk->data.playable;
        });
    }
    if (includes("SPEL")) PopulateList<RE::SpellItem>("Spell", [](RE::SpellItem* spell) { return spell != nullptr; });
    if (includes("FACT")) PopulateList<RE::TESFaction>("Faction", [](RE::TESFaction* faction) { return faction != nullptr; });
    if (includes("BOOK")) PopulateList<RE::TESObjectBOOK>("Book", [](RE::TESObjectBOOK* book) { return book != nullptr; });
    if (includes("SHOU")) PopulateList<RE::TESShout>("Shout", [](RE::TESShout* shout) { return shout != nullptr; });
    if (includes("GLOB")) PopulateList<RE::TESGlobal>("Global", [](RE::TESGlobal* global) { return global != nullptr; });
}

const std::vector<InternalFormInfo>& Manager::GetList(const std::string& typeName) {
    static std::vector<InternalFormInfo> empty;
    auto it = _dataStore.find(typeName);
    if (it != _dataStore.end()) {
        return it->second;
    }
    return empty;
}

void Manager::RegisterReadyCallback(std::function<void()> callback) {
    if (_isPopulated) {
        callback();
    } else {
        _readyCallbacks.push_back(callback);
    }
}

bool IsValidUTF8(const std::string& string) {
    int c, i, ix, n, j;
    for (i = 0, ix = string.length(); i < ix; i++) {
        c = (unsigned char)string[i];
        if (c <= 0x7f) n = 0; // 0bbbbbbb
        else if ((c & 0xE0) == 0xC0) n = 1; // 110bbbbb
        else if (c == 0xED && i < (ix - 1) && ((unsigned char)string[i + 1] & 0xA0) == 0xA0) return false; // U+d800 to U+dfff
        else if ((c & 0xF0) == 0xE0) n = 2; // 1110bbbb
        else if ((c & 0xF8) == 0xF0) n = 3; // 11110bbb
        else return false;
        for (j = 0; j < n && i < ix; j++) { // n bytes matching 10bbbbbb
            if ((++i == ix) || (((unsigned char)string[i] & 0xC0) != 0x80))
                return false;
        }
    }
    return true;
}

// Altere a implementação:
std::string Manager::ToUTF8(std::string_view a_str) {
    if (a_str.empty()) return "";

    std::string srcString(a_str);

    // 1. VERIFICAÇÃO DE SEGURANÇA: Se já for UTF-8 válido, não converta!
    // Isso corrige o problema onde o Skyrim SE já manda UTF-8 e o código quebrava.
    if (IsValidUTF8(srcString)) {
        return srcString;
    }

    // 2. Se não for UTF-8 (é ANSI legado do Windows, comum em mods antigos ou RU/CN), converta.
    // Nota: Para Russo específico, se CP_ACP (padrão do sistema) não funcionar, 
    // troque CP_ACP por 1251 (Cyrillic) hardcoded aqui.
    int wlen = MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return srcString; // Falha, retorna original

    std::wstring wstr(wlen, 0);
    MultiByteToWideChar(CP_ACP, 0, srcString.c_str(), -1, &wstr[0], wlen);

    int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (u8len <= 0) return srcString;

    std::string u8str(u8len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &u8str[0], u8len, nullptr, nullptr);

    // Remove null terminator extra se houver
    if (!u8str.empty() && u8str.back() == '\0') u8str.pop_back();

    return u8str;
}

template <typename T>
void Manager::PopulateList(const std::string& a_typeName, std::function<bool(T*)> a_filter) {
    auto dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) return;

    auto& list = _dataStore[a_typeName];
    list.clear();

    const auto& forms = dataHandler->GetFormArray<T>();
    list.reserve(forms.size());

    for (const auto& form : forms) {
        if (!form) continue;

        if (form->IsDeleted() || form->IsIgnored()) {
            continue;
        }

        if (a_filter && !a_filter(form)) {
            continue;
        }
        // Variáveis de auxílio para o log de erro caso o catch seja acionado
        RE::FormID currentID = 0;
        std::string currentPlugin = "Unknown";

        try {
            currentID = form->GetFormID();

            // Obtém o nome do plugin de origem antes de qualquer processamento complexo
            if (auto file = form->GetFile(0)) {
                currentPlugin = std::string(file->GetFilename());
            }
            else {
                currentPlugin = "Dynamic";
            }

            InternalFormInfo info;
            info.formID = currentID;
            info.formType = a_typeName;
            info.pluginName = ToUTF8(currentPlugin);

            // EditorID: clib_util pode lançar exceções em contextos raros de memória
            std::string rawEditorID = clib_util::editorID::get_editorID(form);
            info.editorID = ToUTF8(rawEditorID);

            std::string rawName = "";
            if (form->Is(RE::FormType::NPC)) {
                if (auto npc = form->As<RE::TESNPC>()) {
                    rawName = npc->fullName.c_str();
                }
            }
            else if (auto fullName = form->As<RE::TESFullName>()) {
                rawName = fullName->fullName.c_str();
            }

            // A conversão UTF-8 é um ponto comum de falha se a string estiver corrompida
            info.name = ToUTF8(rawName);

            // NOVO: Pegar a Descrição e o Próximo Perk (se for BGSPerk)
            info.description = "";
            info.nextPerkId = "";
            if (auto perk = form->As<RE::BGSPerk>()) {
                // Pega a descrição (Herda de TESDescription)
                RE::BSString descStr;
                perk->TESDescription::GetDescription(descStr, perk);
                info.description = ToUTF8(descStr.c_str());


                // Pega a ID do Rank seguinte
                if (perk->nextPerk) {
                    auto npFile = perk->nextPerk->GetFile(0);
                    // CORREÇÃO: Forçando a conversão explícita para std::string
                    std::string npPlugin = npFile ? std::string(npFile->GetFilename()) : "Dynamic";
                    uint32_t npLocalID = (perk->nextPerk->GetFormID() & 0xFF000000) == 0xFE000000 ? (perk->nextPerk->GetFormID() & 0xFFF) : (perk->nextPerk->GetFormID() & 0xFFFFFF);
                    info.nextPerkId = fmt::format("{}|{:X}", npPlugin, npLocalID);
                }
            }

            list.push_back(info);
        }
        catch (const std::exception& e) {
            // Log detalhado com FormID em Hexadecimal e o erro específico
            logger::error("[PopulateList] Critical error on item {:08X} of plugin '{}' (Type: {}). Error: {}",
                currentID, currentPlugin, a_typeName, e.what());
        }
        catch (...) {
            // Captura erros desconhecidos que não herdam de std::exception
            logger::error("[PopulateList] Uknown error on item {:08X} of plugin '{}' (Type: {})",
                currentID, currentPlugin, a_typeName);
        }
    }
    logger::info("Carregados {} itens do tipo {}", list.size(), a_typeName);
}

// Declare a função onde quer que ela esteja no seu código
extern nlohmann::json GetLoadedSkillTreeConfigs();

using json = nlohmann::json;
extern json GetEffectiveSettings(int targetLevel);
extern json GetUISettings();
extern json GetSettings();

// --- CONTROLE DE NOTIFICAÇÕES ---
static std::mutex _notificationMutex;
static std::unordered_set<std::string> _pendingNotifications;

static std::vector<std::string> SplitManagerString(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

static RE::FormID ParseManagerFormIDString(const std::string& formIDStr) {
    if (formIDStr.empty()) return 0;

    auto tokens = SplitManagerString(formIDStr, '|');
    if (tokens.size() == 2) {
        try {
            uint32_t localID = std::stoul(tokens[1], nullptr, 16);
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            return dataHandler ? dataHandler->LookupFormID(localID, tokens[0]) : 0;
        }
        catch (...) {
            return 0;
        }
    }

    try {
        return static_cast<RE::FormID>(std::stoul(formIDStr, nullptr, 16));
    }
    catch (...) {
        return 0;
    }
}

static RE::BGSPerk* ResolvePerkString(const std::string& perkId) {
    RE::FormID formID = ParseManagerFormIDString(perkId);
    return formID != 0 ? RE::TESForm::LookupByID<RE::BGSPerk>(formID) : nullptr;
}

std::string Manager::GetCustomSkillActorValueName(const std::string& skillId) const {
    std::string result = "NSM_";
    for (unsigned char c : skillId) {
        result.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
    }
    if (result == "NSM_") {
        result += "UnknownSkill";
    }
    return result;
}

RE::ActorValue Manager::ResolveCustomSkillActorValue(const std::string& skillId) const {
    if (skillId.empty() || AVG::API::RequestInterface(false) == nullptr) {
        return RE::ActorValue::kNone;
    }

    AVG::ExtraValue extraValue(GetCustomSkillActorValueName(skillId));
    RE::ActorValue av = extraValue.Resolve();
    if (av == RE::ActorValue::kNone || av == RE::ActorValue::kTotal) {
        return RE::ActorValue::kNone;
    }
    return av;
}

RE::Actor* Manager::ResolveActorFromFormID(RE::FormID actorFormID) const {
    if (actorFormID == 0) return nullptr;
    if (actorFormID == player_refid) {
        return RE::PlayerCharacter::GetSingleton();
    }
    return RE::TESForm::LookupByID<RE::Actor>(actorFormID);
}
void Manager::EnsureActorValueGeneratorConfig() {
    try {
        std::filesystem::path dir("Data\\SKSE\\Plugin\\ActorValueData");
        std::filesystem::create_directories(dir);
        std::filesystem::path filePath = dir / "NewSkillMenu_AVG.toml";

        std::ofstream file(filePath, std::ios::trunc);
        if (!file.is_open()) {
            logger::warn("Failed to write ActorValueGenerator config at {}", filePath.string());
            return;
        }

        file << "# Auto-generated by New Skill Menu. Regenerate by loading the game after editing custom skill trees.\n\n";
        for (const auto& [id, skill] : customSkillsData) {
            file << "[" << GetCustomSkillActorValueName(id) << "]\n";
            file << "type = \"Adaptive\"\n";
            file << "displayName = \"" << skill.displayName << "\"\n";
            file << "default.formula = \"" << skill.initialLevel << "\"\n";
            file << "default.type = \"Implicit\"\n\n";
        }
    }
    catch (const std::exception& e) {
        logger::warn("Failed to generate ActorValueGenerator config: {}", e.what());
    }
}

RE::FormID Manager::GetActorXPKey(RE::Actor* actor) const {
    return actor ? actor->GetFormID() : 0;
}

float Manager::GetActorXP(RE::Actor* actor, const std::string& skillId) {
    RE::FormID key = GetActorXPKey(actor);
    if (key == 0 || skillId.empty()) return 0.0f;

    auto actorIt = actorCustomSkillXP.find(key);
    if (actorIt != actorCustomSkillXP.end()) {
        auto skillIt = actorIt->second.find(skillId);
        if (skillIt != actorIt->second.end()) {
            return skillIt->second;
        }
    }

    if (actor->IsPlayerRef()) {
        auto legacyIt = playerCustomSkills.find(skillId);
        if (legacyIt != playerCustomSkills.end()) {
            return legacyIt->second.currentXP;
        }
    }

    return 0.0f;
}

void Manager::SetActorXP(RE::Actor* actor, const std::string& skillId, float xp) {
    RE::FormID key = GetActorXPKey(actor);
    if (key == 0 || skillId.empty() || !std::isfinite(xp)) return;

    if (xp <= 0.001f) {
        auto actorIt = actorCustomSkillXP.find(key);
        if (actorIt != actorCustomSkillXP.end()) {
            actorIt->second.erase(skillId);
            if (actorIt->second.empty()) {
                actorCustomSkillXP.erase(actorIt);
            }
        }
        xp = 0.0f;
    }
    else {
        actorCustomSkillXP[key][skillId] = xp;
    }

    if (actor->IsPlayerRef()) {
        auto dataIt = customSkillsData.find(skillId);
        auto& state = playerCustomSkills[skillId];
        if (dataIt != customSkillsData.end() && state.currentLevel == 15 && state.currentXP == 0.0f && state.bonusLevel == 0) {
            state.currentLevel = dataIt->second.initialLevel;
        }
        state.currentXP = xp;
    }
}

void Manager::SyncLegacyPlayerStateToActorValues() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    for (const auto& [skillId, state] : playerCustomSkills) {
        if (!customSkillsData.contains(skillId)) continue;
        SetCustomSkillLevel(player, skillId, state.currentLevel);
        SetCustomSkillBonus(player, skillId, state.bonusLevel);
        SetActorXP(player, skillId, state.currentXP);
    }
}

void Manager::LoadCustomSkills() {
    nlohmann::json configs = GetLoadedSkillTreeConfigs();

    for (const auto& j : configs) {
        if (j.contains("isVanilla") && !j["isVanilla"].get<bool>()) {
            CustomSkill skill;
            skill.id = j.value("name", "UnknownSkill");
            skill.displayName = j.value("displayName", skill.id);
            skill.initialLevel = j.value("initialLevel", 15);
            skill.isVanilla = false;
            skill.advancesPlayerLevel = j.value("advancesPlayerLevel", false);

            if (j.contains("experienceFormula")) {
                auto& exp = j["experienceFormula"];
                skill.expFormula.useMult = exp.value("useMult", 1.0f);
                skill.expFormula.useOffset = exp.value("useOffset", 0.0f);
                skill.expFormula.improveMult = exp.value("improveMult", 1.0f);
                skill.expFormula.improveOffset = exp.value("improveOffset", 0.0f);
            }

            customSkillsData[skill.id] = skill;

            if (playerCustomSkills.find(skill.id) == playerCustomSkills.end()) {
                playerCustomSkills[skill.id] = { skill.initialLevel, GetActorXP(RE::PlayerCharacter::GetSingleton(), skill.id), 0 };
            }

            logger::info("Custom Skill carregada: {} (Display: {})", skill.id, skill.displayName);
        }
    }

    EnsureActorValueGeneratorConfig();
    SyncLegacyPlayerStateToActorValues();
}

std::vector<std::string> Manager::GetAvailableSkills() const
{
    std::vector<std::string> skills;
    skills.reserve(customSkillsData.size());
    for (const auto& [skillId, skill] : customSkillsData) {
        (void)skill;
        skills.push_back(skillId);
    }
    return skills;
}

int Manager::GetCustomSkillLevel(RE::Actor* actor, const std::string& skillId) {
    auto dataIt = customSkillsData.find(skillId);
    int fallback = dataIt != customSkillsData.end() ? dataIt->second.initialLevel : 1;
    if (!actor || skillId.empty()) return fallback;

    RE::ActorValue av = ResolveCustomSkillActorValue(skillId);
    auto avOwner = actor->AsActorValueOwner();
    if (av != RE::ActorValue::kNone && avOwner) {
        return static_cast<int>(std::round(avOwner->GetBaseActorValue(av)));
    }

    if (actor->IsPlayerRef()) {
        auto it = playerCustomSkills.find(skillId);
        return it != playerCustomSkills.end() ? it->second.currentLevel : fallback;
    }

    return fallback;
}

void Manager::SetCustomSkillLevel(RE::Actor* actor, const std::string& skillId, int level) {
    if (!actor || skillId.empty()) return;

    RE::ActorValue av = ResolveCustomSkillActorValue(skillId);
    auto avOwner = actor->AsActorValueOwner();
    if (av != RE::ActorValue::kNone && avOwner) {
        avOwner->SetBaseActorValue(av, static_cast<float>(level));
    }

    if (actor->IsPlayerRef()) {
        auto& state = playerCustomSkills[skillId];
        state.currentLevel = level;
    }
}

float Manager::GetCustomSkillXP(RE::Actor* actor, const std::string& skillId) {
    return GetActorXP(actor, skillId);
}

void Manager::SetCustomSkillXP(RE::Actor* actor, const std::string& skillId, float xp) {
    SetActorXP(actor, skillId, xp);
}

int Manager::GetCustomSkillBonus(RE::Actor* actor, const std::string& skillId) {
    if (!actor || skillId.empty()) return 0;

    RE::ActorValue av = ResolveCustomSkillActorValue(skillId);
    auto avOwner = actor->AsActorValueOwner();
    if (av != RE::ActorValue::kNone && avOwner) {
        float baseValue = avOwner->GetBaseActorValue(av);
        float permanentValue = avOwner->GetPermanentActorValue(av);
        return static_cast<int>(std::round(permanentValue - baseValue));
    }

    if (actor->IsPlayerRef()) {
        auto it = playerCustomSkills.find(skillId);
        return it != playerCustomSkills.end() ? it->second.bonusLevel : 0;
    }

    return 0;
}

int Manager::GetCustomSkillTotalLevel(RE::Actor* actor, const std::string& skillId) {
    return GetCustomSkillLevel(actor, skillId) + GetCustomSkillBonus(actor, skillId);
}

void Manager::SetCustomSkillBonus(RE::Actor* actor, const std::string& skillId, int amount) {
    if (!actor || skillId.empty()) return;

    RE::ActorValue av = ResolveCustomSkillActorValue(skillId);
    auto avOwner = actor->AsActorValueOwner();
    if (av != RE::ActorValue::kNone && avOwner) {
        int currentBonus = GetCustomSkillBonus(actor, skillId);
        int delta = amount - currentBonus;
        if (delta != 0) {
            avOwner->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, av, static_cast<float>(delta));
        }
    }

    if (actor->IsPlayerRef()) {
        playerCustomSkills[skillId].bonusLevel = amount;
        Prisma::SendUpdateToUI();
    }
}

void Manager::ModCustomSkillBonus(RE::Actor* actor, const std::string& skillId, int amount) {
    SetCustomSkillBonus(actor, skillId, GetCustomSkillBonus(actor, skillId) + amount);
}

bool Manager::HasCustomPerk(RE::Actor* actor, const std::string& perkId) {
    auto perk = ResolvePerkString(perkId);
    return actor && perk && actor->HasPerk(perk);
}

bool Manager::AddCustomPerk(RE::Actor* actor, const std::string& perkId) {
    auto perk = ResolvePerkString(perkId);
    if (!actor || !perk || actor->HasPerk(perk)) return false;
    actor->AddPerk(perk);
    return true;
}

bool Manager::RemoveCustomPerk(RE::Actor* actor, const std::string& perkId) {
    auto perk = ResolvePerkString(perkId);
    if (!actor || !perk || !actor->HasPerk(perk)) return false;
    actor->RemovePerk(perk);
    return true;
}

RE::Actor* Manager::ResolveActor(RE::FormID actorFormID) const {
    return ResolveActorFromFormID(actorFormID);
}

ActorProgressState& Manager::EnsureActorProgress(RE::Actor* actor) {
    static ActorProgressState invalidState;
    if (!actor) return invalidState;

    const auto actorId = actor->GetFormID();
    const auto stableKey = ActorIdentityService::StableKey(actor);

    if (auto direct = actorProgressStates.find(actorId);
        direct != actorProgressStates.end() &&
        !direct->second.actorKey.empty() &&
        !stableKey.empty() &&
        direct->second.actorKey != stableKey) {
        logger::warn(
            "[ActorIdentity] Runtime FormID {:08X} was reused: stored={} current={}",
            actorId,
            direct->second.actorKey,
            stableKey);
        actorProgressStates.erase(direct);
    }

    // A persistent unique actor can receive a different runtime FormID after a
    // framework respawn. Move its state to the currently active reference.
    if (!actorProgressStates.contains(actorId) && !stableKey.empty()) {
        auto stableIt = std::ranges::find_if(
            actorProgressStates,
            [&](const auto& entry) {
                return entry.second.actorKey == stableKey;
            });
        if (stableIt != actorProgressStates.end()) {
            actorProgressStates[actorId] = std::move(stableIt->second);
            actorProgressStates.erase(stableIt);
        }
    }

    auto [it, inserted] = actorProgressStates.try_emplace(actorId);
    auto& state = it->second;
    if (state.actorKey.empty()) {
        state.actorKey = stableKey;
    }
    const int observedLevel = std::max(1, static_cast<int>(actor->GetLevel()));

    if (inserted || state.lastObservedLevel <= 0) {
        state.lastObservedLevel = observedLevel;
        state.highestRewardedLevel = observedLevel;
    }
    else if (!actor->IsPlayerRef() && observedLevel > state.lastObservedLevel) {
        state.pendingLevelUps += observedLevel - state.lastObservedLevel;
        state.lastObservedLevel = observedLevel;
    }

    return state;
}

int Manager::GetActorPerkPoints(RE::Actor* actor) {
    if (!actor) return 0;
    if (actor->IsPlayerRef()) {
        auto player = RE::PlayerCharacter::GetSingleton();
        return player ? static_cast<int>(player->GetPlayerRuntimeData().perkCount) : 0;
    }
    return std::max(0, EnsureActorProgress(actor).perkPoints);
}

bool Manager::SpendActorPerkPoints(RE::Actor* actor, int amount) {
    if (!actor || amount < 0) return false;
    if (GetActorPerkPoints(actor) < amount) return false;
    ModActorPerkPoints(actor, -amount);
    return true;
}

void Manager::ModActorPerkPoints(RE::Actor* actor, int amount, int maximum) {
    if (!actor || amount == 0) return;
    maximum = std::clamp(maximum, 0, 1000000);
    if (actor->IsPlayerRef()) {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        int current = static_cast<int>(player->GetPlayerRuntimeData().perkCount);
        player->GetPlayerRuntimeData().perkCount =
            static_cast<std::uint8_t>(std::clamp(current + amount, 0, std::min(maximum, 255)));
        return;
    }

    auto& state = EnsureActorProgress(actor);
    state.perkPoints = std::clamp(state.perkPoints + amount, 0, maximum);
}

int Manager::GetPendingLevelUps(RE::Actor* actor) {
    return actor ? std::max(0, EnsureActorProgress(actor).pendingLevelUps) : 0;
}

void Manager::QueuePendingLevelUps(RE::Actor* actor, int amount) {
    if (!actor || amount <= 0) return;
    auto& state = EnsureActorProgress(actor);
    state.pendingLevelUps = std::clamp(state.pendingLevelUps + amount, 0, 10000);
    state.lastObservedLevel = std::max(state.lastObservedLevel, static_cast<int>(actor->GetLevel()));
}

void Manager::ConsumePendingLevelUps(RE::Actor* actor, int amount) {
    if (!actor || amount <= 0) return;
    auto& state = EnsureActorProgress(actor);
    const int consumed = std::min(amount, state.pendingLevelUps);
    state.pendingLevelUps -= consumed;
    state.highestRewardedLevel += consumed;
}

void Manager::RecordPurchasedPerk(
    RE::Actor* actor,
    RE::FormID perkFormID,
    int perkPointCost,
    std::vector<PaidResource> resources)
{
    if (!actor || perkFormID == 0) return;
    PerkPurchaseRecord record;
    record.perkPointCost = std::max(0, perkPointCost);
    record.actorLevelAtPurchase = static_cast<int>(actor->GetLevel());
    record.resources = std::move(resources);
    EnsureActorProgress(actor).purchasedPerks[perkFormID] = std::move(record);
}

std::optional<PerkPurchaseRecord> Manager::RemovePurchasedPerkRecord(RE::Actor* actor, RE::FormID perkFormID) {
    if (!actor || perkFormID == 0) return std::nullopt;
    auto actorIt = actorProgressStates.find(actor->GetFormID());
    if (actorIt == actorProgressStates.end()) return std::nullopt;
    auto perkIt = actorIt->second.purchasedPerks.find(perkFormID);
    if (perkIt == actorIt->second.purchasedPerks.end()) return std::nullopt;
    auto paid = std::move(perkIt->second);
    actorIt->second.purchasedPerks.erase(perkIt);
    return paid;
}

bool Manager::WasPerkPurchasedForActor(RE::Actor* actor, RE::FormID perkFormID) const {
    if (!actor || perkFormID == 0) return false;
    auto actorIt = actorProgressStates.find(actor->GetFormID());
    return actorIt != actorProgressStates.end() && actorIt->second.purchasedPerks.contains(perkFormID);
}

std::map<RE::FormID, PerkPurchaseRecord> Manager::GetPurchasedPerks(RE::Actor* actor) {
    if (!actor) return {};
    EnsureActorProgress(actor);
    auto actorIt = actorProgressStates.find(actor->GetFormID());
    return actorIt != actorProgressStates.end() ? actorIt->second.purchasedPerks :
        std::map<RE::FormID, PerkPurchaseRecord>{};
}

void Manager::RehydratePurchasedPerks(RE::Actor* actor) {
    if (!actor) return;
    auto purchases = GetPurchasedPerks(actor);
    for (const auto& [perkId, record] : purchases) {
        (void)record;
        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkId);
        if (perk && !actor->HasPerk(perk)) {
            actor->AddPerk(perk);
            logger::info(
                "[Economy] Reapplied purchased perk {:08X} to {} ({:08X})",
                perkId,
                actor->GetName(),
                actor->GetFormID());
        }
    }
}

int Manager::GetActorResetCount(RE::Actor* actor) {
    return actor ? std::max(0, EnsureActorProgress(actor).resetCount) : 0;
}

void Manager::RecordActorReset(RE::Actor* actor) {
    if (!actor) return;
    auto& state = EnsureActorProgress(actor);
    state.resetCount = std::clamp(state.resetCount + 1, 0, 1000000);
}

void Manager::RemoveCustomSkillState(const std::string& skillId) {
    customSkillsData.erase(skillId);
    playerCustomSkills.erase(skillId);
    for (auto it = actorCustomSkillXP.begin(); it != actorCustomSkillXP.end();) {
        it->second.erase(skillId);
        if (it->second.empty()) {
            it = actorCustomSkillXP.erase(it);
        }
        else {
            ++it;
        }
    }
}

void Manager::AddCustomSkillXPForActorID(RE::FormID actorFormID, const std::string& skillId, float xpAmount) {
    AddCustomSkillXPForActor(ResolveActorFromFormID(actorFormID), skillId, xpAmount);
}

int Manager::GetCustomSkillLevelForActorID(RE::FormID actorFormID, const std::string& skillId) {
    return GetCustomSkillLevel(ResolveActorFromFormID(actorFormID), skillId);
}

float Manager::GetCustomSkillXPForActorID(RE::FormID actorFormID, const std::string& skillId) {
    return GetCustomSkillXP(ResolveActorFromFormID(actorFormID), skillId);
}

int Manager::GetCustomSkillTotalLevelForActorID(RE::FormID actorFormID, const std::string& skillId) {
    return GetCustomSkillTotalLevel(ResolveActorFromFormID(actorFormID), skillId);
}

int Manager::GetCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId) {
    return GetCustomSkillBonus(ResolveActorFromFormID(actorFormID), skillId);
}

void Manager::ModCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId, int amount) {
    ModCustomSkillBonus(ResolveActorFromFormID(actorFormID), skillId, amount);
}

void Manager::SetCustomSkillBonusForActorID(RE::FormID actorFormID, const std::string& skillId, int amount) {
    SetCustomSkillBonus(ResolveActorFromFormID(actorFormID), skillId, amount);
}

bool Manager::HasCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId) {
    return HasCustomPerk(ResolveActorFromFormID(actorFormID), perkId);
}

bool Manager::AddCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId) {
    return AddCustomPerk(ResolveActorFromFormID(actorFormID), perkId);
}

bool Manager::RemoveCustomPerkForActorID(RE::FormID actorFormID, const std::string& perkId) {
    return RemoveCustomPerk(ResolveActorFromFormID(actorFormID), perkId);
}
void Manager::AddCustomSkillXP(const std::string& skillId, float xpAmount) {
    AddCustomSkillXPForActor(RE::PlayerCharacter::GetSingleton(), skillId, xpAmount);
}

void Manager::AddCustomSkillXPForActor(RE::Actor* actor, const std::string& skillId, float xpAmount) {
    if (!actor || skillId.empty() || !std::isfinite(xpAmount) || xpAmount <= 0.0f) return;

    int startLevelSnapshot = 0;
    std::string dispName;
    bool shouldScheduleNotification = false;
    bool playerUpdateAlreadyPending = false;

    {
        std::lock_guard<std::mutex> updateLock(_notificationMutex);

        auto skillDataIt = customSkillsData.find(skillId);
        if (skillDataIt == customSkillsData.end()) return;

        auto& data = skillDataIt->second;
        dispName = data.displayName;

        int currentLevel = GetCustomSkillLevel(actor, skillId);
        float currentXP = GetCustomSkillXP(actor, skillId);
        startLevelSnapshot = currentLevel;

        float finalXp = (xpAmount * data.expFormula.useMult) + data.expFormula.useOffset;
        currentXP += finalXp;

        json settings = GetSettings();
        int maxCap = settings.contains("base") ? settings["base"].value("skillCap", 100) : 100;

        float reqXp = GetRequiredXP(skillId, currentLevel);
        if (!std::isfinite(reqXp) || reqXp <= 0.001f) reqXp = 1.0f;

        while (currentXP >= (reqXp - 0.001f) && currentLevel < maxCap) {
            currentXP -= reqXp;
            if (currentXP < 0.0f) currentXP = 0.0f;

            currentLevel++;
            reqXp = GetRequiredXP(skillId, currentLevel);
            if (!std::isfinite(reqXp) || reqXp <= 0.001f) reqXp = 1.0f;

            if (data.advancesPlayerLevel && actor->IsPlayerRef()) {
                auto player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    auto& rt = player->GetPlayerRuntimeData();
                    if (rt.skills && rt.skills->data) {
                        rt.skills->data->xp += static_cast<float>(currentLevel);
                    }
                }
            }
        }

        if (currentLevel >= maxCap) {
            currentXP = 0.0f;
        }

        SetCustomSkillLevel(actor, skillId, currentLevel);
        SetActorXP(actor, skillId, currentXP);

        if (actor->IsPlayerRef()) {
            playerUpdateAlreadyPending = _pendingNotifications.find(skillId) != _pendingNotifications.end();
            if (!playerUpdateAlreadyPending) {
                _pendingNotifications.insert(skillId);
                shouldScheduleNotification = true;
            }
        }
    }

    if (!actor->IsPlayerRef()) return;

    Prisma::SendUpdateToUI();
    if (playerUpdateAlreadyPending) return;
    if (!shouldScheduleNotification) return;

    SKSE::GetTaskInterface()->AddUITask(
        [this, skillId, dispName, startLevelSnapshot]() {
            int currentRealLevel;
            float currentRealXP;
            float reqXpForCalc;

            {
                std::lock_guard<std::mutex> guard(_notificationMutex);
                _pendingNotifications.erase(skillId);

                auto player = RE::PlayerCharacter::GetSingleton();
                if (!player) return;

                currentRealLevel = GetCustomSkillLevel(player, skillId);
                currentRealXP = GetCustomSkillXP(player, skillId);
                reqXpForCalc = GetRequiredXP(skillId, currentRealLevel);
            }

            if (reqXpForCalc <= 0.001f) reqXpForCalc = 1.0f;

            float endPct = currentRealXP / reqXpForCalc;
            float startPct = currentRealLevel > startLevelSnapshot ? 0.0f : std::max(0.0f, endPct - 0.05f);

            startPct = std::clamp(startPct, 0.0f, 1.0f);
            endPct = std::clamp(endPct, 0.0f, 1.0f);

            if (std::abs(endPct - startPct) < 0.001f && currentRealLevel == startLevelSnapshot) {
                return;
            }

            const auto ui = RE::UI::GetSingleton();
            if (!ui) return;

            const auto menu = ui->GetMenu<RE::HUDMenu>(RE::HUDMenu::MENU_NAME);
            if (!menu || !menu->uiMovie) return;

            auto movie = menu->uiMovie;
            RE::GFxValue questUpdateInstance;

            if (movie->GetVariable(&questUpdateInstance, "_root.HUDMovieBaseInstance.QuestUpdateBaseInstance")) {
                json uiSettings = GetUISettings();
                std::string finalName = (uiSettings.value("hideLockedTreeNames", false) && currentRealLevel <= 0)
                    ? "????" : dispName;

                RE::GFxValue args[8];
                args[0] = finalName.c_str();
                args[1] = "";
                args[2] = "UISkillIncreaseSD";
                args[3] = 0;
                args[4] = 1;
                args[5] = currentRealLevel;
                args[6] = startPct;
                args[7] = endPct;

                questUpdateInstance.Invoke("ShowNotification", nullptr, args, 8);
            }
        });
}
// Calculo do Threshold de XP (Pode ser ajustado para simular 100% a curva vanilla se quiser)
float Manager::GetRequiredXP(const std::string& skillId, int level) {
    if (customSkillsData.find(skillId) != customSkillsData.end()) {
        auto& exp = customSkillsData[skillId].expFormula;

        // FÓRMULA AJUSTADA (Quadrática):
        // Cria uma curva onde níveis altos exigem mais XP.
        // Exemplo (Offset 0, Mult 1.0):
        // Nível 15: ~127 XP
        // Nível 50: ~1300 XP
        // Nível 100: ~5100 XP
        float linearPart = exp.improveMult * level;
        float curvedPart = std::pow(level, 2.0f) * 0.5f; 

        return exp.improveOffset + linearPart + curvedPart;
    }
    // Fallback padrão vanilla-ish
    return 100.0f + (level * 10.0f);
}



// --- LOGICA DE SAVE / LOAD DO SKSE ---
void Manager::Save(SKSE::SerializationInterface* a_intfc) {
    if (!a_intfc->OpenRecord('SKIL', 3)) return;

    uint32_t actorCount = 0;
    for (const auto& [actorId, skills] : actorCustomSkillXP) {
        if (!skills.empty()) actorCount++;
    }

    a_intfc->WriteRecordData(&actorCount, sizeof(actorCount));

    for (const auto& [actorId, skills] : actorCustomSkillXP) {
        if (skills.empty()) continue;

        a_intfc->WriteRecordData(&actorId, sizeof(actorId));

        uint32_t skillCount = 0;
        for (const auto& [skillId, xp] : skills) {
            if (!skillId.empty() && std::isfinite(xp) && xp > 0.001f) skillCount++;
        }
        a_intfc->WriteRecordData(&skillCount, sizeof(skillCount));

        for (const auto& [skillId, xp] : skills) {
            if (skillId.empty() || !std::isfinite(xp) || xp <= 0.001f) continue;

            uint32_t idLen = static_cast<uint32_t>(skillId.length());
            a_intfc->WriteRecordData(&idLen, sizeof(idLen));
            a_intfc->WriteRecordData(skillId.data(), idLen);
            a_intfc->WriteRecordData(&xp, sizeof(xp));
        }
    }

    if (!a_intfc->OpenRecord('APRG', 2)) return;
    const auto progressCount = static_cast<std::uint32_t>(actorProgressStates.size());
    a_intfc->WriteRecordData(&progressCount, sizeof(progressCount));

    for (const auto& [actorId, state] : actorProgressStates) {
        a_intfc->WriteRecordData(&actorId, sizeof(actorId));
        const auto actorKeyLength = static_cast<std::uint32_t>(state.actorKey.size());
        a_intfc->WriteRecordData(&actorKeyLength, sizeof(actorKeyLength));
        if (actorKeyLength > 0) {
            a_intfc->WriteRecordData(state.actorKey.data(), actorKeyLength);
        }
        a_intfc->WriteRecordData(&state.perkPoints, sizeof(state.perkPoints));
        a_intfc->WriteRecordData(&state.lastObservedLevel, sizeof(state.lastObservedLevel));
        a_intfc->WriteRecordData(&state.highestRewardedLevel, sizeof(state.highestRewardedLevel));
        a_intfc->WriteRecordData(&state.pendingLevelUps, sizeof(state.pendingLevelUps));
        a_intfc->WriteRecordData(&state.resetCount, sizeof(state.resetCount));

        const auto perkCount = static_cast<std::uint32_t>(state.purchasedPerks.size());
        a_intfc->WriteRecordData(&perkCount, sizeof(perkCount));
        for (const auto& [perkId, purchase] : state.purchasedPerks) {
            a_intfc->WriteRecordData(&perkId, sizeof(perkId));
            a_intfc->WriteRecordData(&purchase.perkPointCost, sizeof(purchase.perkPointCost));
            a_intfc->WriteRecordData(&purchase.actorLevelAtPurchase, sizeof(purchase.actorLevelAtPurchase));

            const auto resourceCount = static_cast<std::uint32_t>(purchase.resources.size());
            a_intfc->WriteRecordData(&resourceCount, sizeof(resourceCount));
            for (const auto& resource : purchase.resources) {
                const auto writeString = [&](const std::string& value) {
                    const auto stringLength = static_cast<std::uint32_t>(value.size());
                    a_intfc->WriteRecordData(&stringLength, sizeof(stringLength));
                    if (stringLength > 0) {
                        a_intfc->WriteRecordData(value.data(), stringLength);
                    }
                };
                writeString(resource.resourceId);
                writeString(resource.sourceType);
                writeString(resource.sourceLocator);
                a_intfc->WriteRecordData(&resource.amount, sizeof(resource.amount));
                a_intfc->WriteRecordData(&resource.shared, sizeof(resource.shared));
            }
        }
    }
}

void Manager::Load(SKSE::SerializationInterface* a_intfc) {
    uint32_t type;
    uint32_t version;
    uint32_t length;

    actorCustomSkillXP.clear();
    playerCustomSkills.clear();
    actorProgressStates.clear();

    while (a_intfc->GetNextRecordInfo(type, version, length)) {
        if (type == 'APRG') {
            std::uint32_t actorCount = 0;
            if (!a_intfc->ReadRecordData(&actorCount, sizeof(actorCount))) continue;
            if (actorCount > 10000) continue;

            for (std::uint32_t i = 0; i < actorCount; ++i) {
                RE::FormID oldActorId = 0;
                ActorProgressState state;
                if (!a_intfc->ReadRecordData(&oldActorId, sizeof(oldActorId))) break;
                if (version >= 2) {
                    std::uint32_t actorKeyLength = 0;
                    if (!a_intfc->ReadRecordData(&actorKeyLength, sizeof(actorKeyLength))) break;
                    if (actorKeyLength > 1024) break;
                    state.actorKey.resize(actorKeyLength);
                    if (actorKeyLength > 0 &&
                        !a_intfc->ReadRecordData(state.actorKey.data(), actorKeyLength)) break;
                }
                if (!a_intfc->ReadRecordData(&state.perkPoints, sizeof(state.perkPoints))) break;
                if (!a_intfc->ReadRecordData(&state.lastObservedLevel, sizeof(state.lastObservedLevel))) break;
                if (!a_intfc->ReadRecordData(&state.highestRewardedLevel, sizeof(state.highestRewardedLevel))) break;
                if (!a_intfc->ReadRecordData(&state.pendingLevelUps, sizeof(state.pendingLevelUps))) break;
                if (version >= 2 &&
                    !a_intfc->ReadRecordData(&state.resetCount, sizeof(state.resetCount))) break;

                RE::FormID actorId = 0;
                bool actorResolved = a_intfc->ResolveFormID(oldActorId, actorId);
                if (!actorResolved && oldActorId == player_refid) {
                    actorId = player_refid;
                    actorResolved = true;
                }

                std::uint32_t perkCount = 0;
                if (!a_intfc->ReadRecordData(&perkCount, sizeof(perkCount))) break;
                if (perkCount > 100000) break;
                for (std::uint32_t j = 0; j < perkCount; ++j) {
                    RE::FormID oldPerkId = 0;
                    PerkPurchaseRecord purchase;
                    if (!a_intfc->ReadRecordData(&oldPerkId, sizeof(oldPerkId))) break;
                    if (!a_intfc->ReadRecordData(&purchase.perkPointCost, sizeof(purchase.perkPointCost))) break;

                    if (version >= 2) {
                        if (!a_intfc->ReadRecordData(
                            &purchase.actorLevelAtPurchase,
                            sizeof(purchase.actorLevelAtPurchase))) break;

                        std::uint32_t resourceCount = 0;
                        if (!a_intfc->ReadRecordData(&resourceCount, sizeof(resourceCount))) break;
                        if (resourceCount > 10000) break;
                        purchase.resources.reserve(resourceCount);

                        const auto readString = [&](std::string& value) -> bool {
                            std::uint32_t stringLength = 0;
                            if (!a_intfc->ReadRecordData(&stringLength, sizeof(stringLength))) return false;
                            if (stringLength > 4096) return false;
                            value.resize(stringLength);
                            return stringLength == 0 ||
                                a_intfc->ReadRecordData(value.data(), stringLength);
                        };

                        for (std::uint32_t k = 0; k < resourceCount; ++k) {
                            PaidResource resource;
                            if (!readString(resource.resourceId)) break;
                            if (!readString(resource.sourceType)) break;
                            if (!readString(resource.sourceLocator)) break;
                            if (!a_intfc->ReadRecordData(&resource.amount, sizeof(resource.amount))) break;
                            if (!a_intfc->ReadRecordData(&resource.shared, sizeof(resource.shared))) break;
                            if (std::isfinite(resource.amount) && resource.amount > 0.0f) {
                                purchase.resources.push_back(std::move(resource));
                            }
                        }
                    }

                    RE::FormID perkId = 0;
                    if (actorResolved && a_intfc->ResolveFormID(oldPerkId, perkId)) {
                        purchase.perkPointCost = std::max(0, purchase.perkPointCost);
                        state.purchasedPerks[perkId] = std::move(purchase);
                    }
                }

                if (actorResolved || !state.actorKey.empty()) {
                    state.perkPoints = std::clamp(state.perkPoints, 0, 1000000);
                    state.pendingLevelUps = std::clamp(state.pendingLevelUps, 0, 10000);
                    state.lastObservedLevel = std::clamp(state.lastObservedLevel, 0, 10000);
                    state.highestRewardedLevel = std::clamp(state.highestRewardedLevel, 0, 10000);
                    state.resetCount = std::clamp(state.resetCount, 0, 1000000);
                    if (state.actorKey.empty()) {
                        if (auto actor = ResolveActorFromFormID(actorId)) {
                            state.actorKey = ActorIdentityService::StableKey(actor);
                        }
                    }
                    actorProgressStates[
                        actorResolved ? actorId : oldActorId] =
                        std::move(state);
                }
            }
            continue;
        }

        if (type != 'SKIL') continue;

        if (version >= 3) {
            uint32_t actorCount = 0;
            if (!a_intfc->ReadRecordData(&actorCount, sizeof(actorCount))) continue;

            for (uint32_t i = 0; i < actorCount; ++i) {
                RE::FormID oldActorId = 0;
                if (!a_intfc->ReadRecordData(&oldActorId, sizeof(oldActorId))) break;

                RE::FormID actorId = 0;
                bool resolved = a_intfc->ResolveFormID(oldActorId, actorId);
                if (!resolved && oldActorId == player_refid) {
                    actorId = player_refid;
                    resolved = true;
                }

                uint32_t skillCount = 0;
                if (!a_intfc->ReadRecordData(&skillCount, sizeof(skillCount))) break;

                for (uint32_t j = 0; j < skillCount; ++j) {
                    uint32_t idLen = 0;
                    if (!a_intfc->ReadRecordData(&idLen, sizeof(idLen))) break;
                    if (idLen == 0 || idLen > 512) break;

                    std::string id(idLen, '\0');
                    if (!a_intfc->ReadRecordData(id.data(), idLen)) break;

                    float xp = 0.0f;
                    if (!a_intfc->ReadRecordData(&xp, sizeof(xp))) break;

                    if (resolved && std::isfinite(xp) && xp > 0.001f) {
                        actorCustomSkillXP[actorId][id] = xp;
                    }
                }
            }
        }
        else {
            std::size_t count;
            if (!a_intfc->ReadRecordData(&count, sizeof(count))) continue;

            for (std::size_t i = 0; i < count; ++i) {
                std::size_t idLen;
                if (!a_intfc->ReadRecordData(&idLen, sizeof(idLen))) break;
                if (idLen == 0 || idLen > 512) break;

                std::string id(idLen, '\0');
                if (!a_intfc->ReadRecordData(id.data(), idLen)) break;

                CustomSkillState state;
                state.bonusLevel = 0;

                if (!a_intfc->ReadRecordData(&state.currentLevel, sizeof(state.currentLevel))) break;
                if (!a_intfc->ReadRecordData(&state.currentXP, sizeof(state.currentXP))) break;

                if (version >= 2) {
                    if (!a_intfc->ReadRecordData(&state.bonusLevel, sizeof(state.bonusLevel))) break;
                }

                playerCustomSkills[id] = state;
                if (std::isfinite(state.currentXP) && state.currentXP > 0.001f) {
                    actorCustomSkillXP[player_refid][id] = state.currentXP;
                }
            }
        }
    }

    if (auto player = RE::PlayerCharacter::GetSingleton()) {
        RehydratePurchasedPerks(player);
    }
}

// Limpa a memória quando o jogador vai pro menu principal ou da load em outro save
void Manager::Revert(SKSE::SerializationInterface* a_intfc) {
    actorCustomSkillXP.clear();
    actorProgressStates.clear();
    playerCustomSkills.clear();
    for (const auto& [id, data] : customSkillsData) {
        playerCustomSkills[id] = { data.initialLevel, 0.0f, 0 };
    }
}// Adicione esta função no final do arquivo ou junto com os outros métodos públicos

const InternalFormInfo* Manager::GetInfoByID(const std::string& type, RE::FormID id) {
    // Acesso direto ao map _dataStore
    auto it = _dataStore.find(type);
    if (it != _dataStore.end()) {
        const auto& list = it->second;
        // Busca linear para encontrar o FormID correspondente
        for (const auto& item : list) {
            if (item.formID == id) {
                return &item;
            }
        }
    }
    return nullptr;
}
