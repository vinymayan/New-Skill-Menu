#include "Prisma.h"
#include "Manager.h"
#include "ActorIdentityService.h"
#include "PurchaseService.h"
#include "RequirementService.h"
#include "ResetService.h"
#include "ResourceService.h"
#include "RosterService.h"
#include "SnapshotService.h"

using json = nlohmann::json;

PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
static PrismaView view;
static bool isVisible = false;

static json g_mergedLocCache;
static bool g_locLoaded = false;
static bool g_isLevelUpMenuOpen = false;
static RE::FormID g_selectedActorID = player_refid;

json GetLevelRules();
json GetSettings();
json GetUISettings();

static std::string ActorRuntimeKey(RE::Actor* actor) {
    return ActorIdentityService::RuntimeKey(actor);
}

static std::string ActorRuleKey(RE::Actor* actor) {
    return ActorIdentityService::RuleKey(actor);
}

static std::vector<RE::Actor*> GetSelectableActors() {
    return RosterService::GetSelectableActors(GetSettings());
}

static RE::Actor* GetSelectedActor() {
    for (auto actor : GetSelectableActors()) {
        if (actor && actor->GetFormID() == g_selectedActorID) {
            return actor;
        }
    }

    g_selectedActorID = player_refid;
    return RE::PlayerCharacter::GetSingleton();
}

struct CachedTreeData {
    json data;
    std::filesystem::file_time_type lastWriteTime;
};
static std::unordered_map<std::string, CachedTreeData> g_treeCache;

static json g_settingsCache;
static bool g_settingsLoaded = false;

static json g_rulesCache;
static bool g_rulesLoaded = false;

static json g_uiSettingsCache;
static bool g_uiSettingsLoaded = false;

static json g_resourcesCache = json::array();
static bool g_resourcesLoaded = false;

static constexpr const char* VAMPIRE_RESOURCE_ID = "nsm_vampire_perk_points";
static constexpr const char* WEREWOLF_RESOURCE_ID = "nsm_werewolf_perk_points";

static std::filesystem::path GetResourcesDir() {
    return std::filesystem::path("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\Resources");
}

static bool IsDefaultResourceId(const std::string& id) {
    return id == VAMPIRE_RESOURCE_ID || id == WEREWOLF_RESOURCE_ID;
}

static std::filesystem::path GetResourceSuppressPath(const std::string& id) {
    return GetResourcesDir() / (id + ".deleted");
}

static bool IsResourceSuppressed(const std::string& id) {
    return std::filesystem::exists(GetResourceSuppressPath(id));
}

static json GetDefaultCustomResources() {
    return json::array({
        {
            {"id", VAMPIRE_RESOURCE_ID},
            {"name", "Vampire Perk Points"},
            {"glob", ""},
            {"actorValue", "Vampirism"},
            {"isDefault", true}
        },
        {
            {"id", WEREWOLF_RESOURCE_ID},
            {"name", "Werewolf Perk Points"},
            {"glob", ""},
            {"actorValue", "Werewolf"},
            {"isDefault", true}
        }
    });
}

static void EnsureDefaultCustomResources() {
    std::filesystem::path dir = GetResourcesDir();
    std::filesystem::create_directories(dir);

    for (const auto& res : GetDefaultCustomResources()) {
        std::string id = res.value("id", "");
        if (id.empty()) continue;
        if (IsResourceSuppressed(id)) continue;

        std::filesystem::path filePath = dir / (id + ".json");
        if (std::filesystem::exists(filePath)) continue;

        std::ofstream file(filePath);
        if (file.is_open()) {
            file << res.dump(4);
        }
    }
}

static void TriggerMouseModeEvent(bool close = false) {
    auto dispatcher = SKSE::GetModCallbackEventSource();
    if (dispatcher) {
        float actionValue = close ? 0.0f : 1.0f;
        SKSE::ModCallbackEvent modEvent{
            RE::BSFixedString("MouseMode_Trigger"),
            RE::BSFixedString(""), 
            actionValue,
            nullptr
        };
        dispatcher->SendEvent(&modEvent);
        logger::info("Evento MouseMode_Trigger disparado para alternar o MouseMode.");
    }
}
static bool ShouldTriggerMouseMode() {
    auto inputMgr = RE::BSInputDeviceManager::GetSingleton();
    bool isGamepad = inputMgr && inputMgr->IsGamepadEnabled();

    return isGamepad && Prisma::MouseMode;
}

void Prisma::SendKeyPress(const std::string& key) {
    if (PrismaUI && createdView && isVisible) {
        std::string script = fmt::format("window.dispatchEvent(new KeyboardEvent('keydown', {{ key: '{}' }}));", key);
        PrismaUI->Invoke(view, script.c_str());
    }
}

json GetCustomResources() {
    // 1. Verifica se já está no cache
    if (g_resourcesLoaded) {
        ResourceService::SetDefinitions(g_resourcesCache);
        return g_resourcesCache;
    }

    EnsureDefaultCustomResources();

    json resources = json::array();
    std::filesystem::path dir = GetResourcesDir();

    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    try {
                        json res = json::parse(file);
                        resources.push_back(res);
                    }
                    catch (...) {}
                }
            }
        }
    }

    // 2. Salva no cache
    g_resourcesCache = resources;
    g_resourcesLoaded = true;
    ResourceService::SetDefinitions(g_resourcesCache);

    return g_resourcesCache;
}

// --- Salvar recurso ---
static void SaveResourcesFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json incomingResources = json::parse(jsonArgs);
        if (!incomingResources.is_array()) return;

        std::filesystem::path resDir = GetResourcesDir();
        if (!std::filesystem::exists(resDir)) {
            std::filesystem::create_directories(resDir);
        }

        for (auto& res : incomingResources) {
            std::string id = res.value("id", "Unknown");
            if (id == "Unknown") continue;
            std::filesystem::path suppressPath = GetResourceSuppressPath(id);
            if (std::filesystem::exists(suppressPath)) {
                std::filesystem::remove(suppressPath);
            }
            std::filesystem::path filePath = resDir / (id + ".json");
            std::ofstream file(filePath);
            if (file.is_open()) {
                file << res.dump(4);
            }
        }

        // Invalida o cache para forçar a releitura no próximo GetCustomResources
        g_resourcesLoaded = false;
    }
    catch (const std::exception& e) {
        logger::error("Erro em SaveResourcesFromUI: {}", e.what());
    }
}

// --- Deletar recurso ---
static void DeleteResourceFromUI(const char* args) {
    if (!args) return;
    try {
        json j = json::parse(args);
        std::string id = j.value("id", "");
        if (!id.empty()) {
            std::filesystem::path filePath = GetResourcesDir() / (id + ".json");
            if (std::filesystem::exists(filePath)) {
                std::filesystem::remove(filePath);
            }
            if (IsDefaultResourceId(id)) {
                std::filesystem::create_directories(GetResourcesDir());
                std::ofstream suppressFile(GetResourceSuppressPath(id));
                if (suppressFile.is_open()) {
                    suppressFile << "deleted";
                }
            }

            // Invalida o cache após deletar com sucesso
            g_resourcesLoaded = false;
        }
    }
    catch (...) {}
}

static void PlayUISound(const char* soundEditorID) {
    auto audioManager = RE::BSAudioManager::GetSingleton();
    if (audioManager) {
        RE::BSSoundHandle handle;
        audioManager->GetSoundHandleByName(handle, soundEditorID, 16);

        // Verifica se o handle foi criado com sucesso antes de tocar
        if (handle.IsValid()) {
            handle.Play();
        }
    }
}

bool IsLocationDiscovered(RE::BGSLocation* a_location) {
    // 1. Verifica se a localização existe
    if (!a_location) {
        return false;
    }

    // 2. Pega o Handle do marcador de mapa (MNAM) da localização
    RE::ObjectRefHandle markerHandle = a_location->worldLocMarker;
    if (!markerHandle) {
        return false; // Essa location não tem um ícone no mapa global
    }

    // 3. Resolve o Handle para pegar a referência (TESObjectREFR) no mundo
    RE::NiPointer<RE::TESObjectREFR> markerRef = markerHandle.get();
    if (!markerRef) {
        return false;
    }

    // 4. Busca o ExtraMapMarker dentro do objeto
    auto extraMapMarker = markerRef->extraList.GetByType<RE::ExtraMapMarker>();
    if (extraMapMarker && extraMapMarker->mapData) {
        // 5. Checa se a flag kCanTravelTo está ativa (Ícone branco / Fast Travel habilitado)
        return extraMapMarker->mapData->flags.any(RE::MapMarkerData::Flag::kCanTravelTo);
    }

    return false;
}

std::string SanitizeFilename(std::string name) {
    if (name.empty()) return "Unnamed_Tree";
    std::string illegalChars = "<>:\"/\\\\|?*";
    for (char& c : name) {
        if (illegalChars.find(c) != std::string::npos) {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    return name;
}

// Helper para converter caminhos da UI (./Assets/img.png) para caminhos físicos e caminhos dentro do ZIP
struct PathInfo {
    std::string fullSystemPath; // C:\Skyrim\Data\PrismaUI\views\Product\Assets\img.png
    std::string zipInternalPath; // Data/PrismaUI/views/Product/Assets/img.png
    bool valid;
};

PathInfo ResolvePathForExport(const std::string& uiPath) {
    if (uiPath.empty()) return { "", "", false };

    std::string cleanPath = uiPath;
    // Remove o "./" inicial se existir
    if (cleanPath.rfind("./", 0) == 0) {
        cleanPath = cleanPath.substr(2);
    }

    // Caminho base da View
    std::string productPath = "Data/PrismaUI/views/" PRODUCT_NAME "/";

    PathInfo info;
    info.fullSystemPath = productPath + cleanPath;

    // Para ficar instalável, o ZIP deve começar com Data/...
    info.zipInternalPath = productPath + cleanPath;

    // Normaliza separadores para o ZIP (sempre /)
    std::replace(info.zipInternalPath.begin(), info.zipInternalPath.end(), '\\', '/');

    // Verifica se existe no disco
    info.valid = std::filesystem::exists(info.fullSystemPath);

    return info;
}

static void ExportTreeFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;

    try {
        json treeData = json::parse(jsonArgs);
        std::string treeName = treeData.value("name", "Unknown");
        std::string safeName = SanitizeFilename(treeName);

        // 1. Preparar diretório de exportação
        std::filesystem::path exportDir = "Data/PrismaUI/Exports";
        std::filesystem::create_directories(exportDir);
        std::string zipPath = (exportDir / (safeName + ".zip")).string();

        logger::info("Iniciando exportacao da arvore '{}' para '{}'...", treeName, zipPath);

        // 2. Inicializar ZIP
        mz_zip_archive zip_archive;
        memset(&zip_archive, 0, sizeof(zip_archive));

        if (!mz_zip_writer_init_file(&zip_archive, zipPath.c_str(), 0)) {
            logger::error("Falha ao criar arquivo ZIP em {}", zipPath);
            return;
        }

        // 3. Adicionar o arquivo JSON da Árvore
        // Caminho físico atual
        std::string jsonSystemPath = "Data/PrismaUI/views/" PRODUCT_NAME "/Skill Trees/" + treeName + ".json";
        // Caminho dentro do ZIP
        std::string jsonZipPath = "Data/PrismaUI/views/" PRODUCT_NAME "/Skill Trees/" + treeName + ".json";

        // Se o arquivo ainda não foi salvo no disco pelo usuário, salvamos temporariamente o conteúdo do payload
        // Mas o ideal é ler do disco para garantir consistência. Vamos assumir que o usuário salvou antes.
        if (std::filesystem::exists(jsonSystemPath)) {
            mz_zip_writer_add_file(&zip_archive, jsonZipPath.c_str(), jsonSystemPath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION);
        }
        else {
            // Se não existir (novo), grava o conteúdo da string JSON direta no ZIP
            std::string dump = treeData.dump(4);
            mz_zip_writer_add_mem(&zip_archive, jsonZipPath.c_str(), dump.data(), dump.size(), MZ_BEST_COMPRESSION);
        }

        // 4. Identificar e Adicionar Assets (Imagens)
        std::vector<std::string> assetsToProcess;

        // Background da Árvore
        if (treeData.contains("bgPath")) assetsToProcess.push_back(treeData["bgPath"]);
        // Ícone da Árvore
        if (treeData.contains("iconPath")) assetsToProcess.push_back(treeData["iconPath"]);
        // Ícone Genérico de Perk
        if (treeData.contains("iconPerkPath")) assetsToProcess.push_back(treeData["iconPerkPath"]);

        // Ícones dos Nodes (Perks)
        if (treeData.contains("nodes") && treeData["nodes"].is_array()) {
            for (const auto& node : treeData["nodes"]) {
                if (node.contains("icon")) assetsToProcess.push_back(node["icon"]);
            }
        }

        // Processa assets únicos (evitar duplicatas no ZIP)
        std::sort(assetsToProcess.begin(), assetsToProcess.end());
        assetsToProcess.erase(std::unique(assetsToProcess.begin(), assetsToProcess.end()), assetsToProcess.end());

        for (const auto& uiPath : assetsToProcess) {
            PathInfo pathInfo = ResolvePathForExport(uiPath);
            if (pathInfo.valid) {
                // Adiciona ao ZIP
                if (!mz_zip_writer_add_file(&zip_archive, pathInfo.zipInternalPath.c_str(), pathInfo.fullSystemPath.c_str(), nullptr, 0, MZ_BEST_COMPRESSION)) {
                    logger::warn("Falha ao adicionar asset ao ZIP: {}", pathInfo.fullSystemPath);
                }
            }
        }

        // 5. Finalizar
        mz_zip_writer_finalize_archive(&zip_archive);
        mz_zip_writer_end(&zip_archive);

        logger::info("Exportacao concluida com sucesso!");

        // Opcional: Avisar a UI que terminou (via evento)
        if (PrismaUI && view) {
            PrismaUI->Invoke(view, fmt::format("alert('Tree exported to: {}');", "Data/PrismaUI/Exports/" + safeName + ".zip").c_str());
        }

    }
    catch (const std::exception& e) {
        logger::error("Erro critico na exportacao: {}", e.what());
    }
}

std::vector<std::string> GetAvailableLanguages() {
    return { "NSM_Language" };
}


json GetLocalizationContent() {
    // 1. Verifica se já está na memória (Cache Hit)
    if (g_locLoaded) {
        return g_mergedLocCache;
    }

    json merged = json::object();
    std::filesystem::path locDir = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Localization";

    if (std::filesystem::exists(locDir) && std::filesystem::is_directory(locDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(locDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::ifstream file(entry.path());
                if (file.is_open()) {
                    try {
                        json content = json::parse(file);
                        // Deep merge (mescla todos os arquivos em um único dicionário)
                        merged.merge_patch(content);
                    }
                    catch (const std::exception& e) {
                        logger::error("Erro de parse no idioma {}: {}", entry.path().filename().string(), e.what());
                    }
                }
            }
        }
    }
    else {
        logger::warn("Diretorio de idiomas nao encontrado: {}", locDir.string());
    }

    g_mergedLocCache = merged;
    g_locLoaded = true;
    return merged;
}



static void RequestLocalizationFromUI(const char* args) {
    // 1. Lê o JSON mesclado do disco/cache
    json content = GetLocalizationContent();

    // 2. Monta a resposta ignorando o args (agora é unificado)
    json response;
    response["lang"] = "NSM_Language";
    response["data"] = content;

    // 3. Envia para a UI
    if (PrismaUI && Prisma::createdView) {
        std::string script = fmt::format("window.dispatchEvent(new CustomEvent('receiveLocalization', {{ detail: {} }}));", response.dump());
        PrismaUI->Invoke(view, script.c_str());
    }
}

void Prisma::PreloadLocalization() {
    logger::info("Pré-carregando dados de localização unificados...");
    GetLocalizationContent();
}

bool PrismaTreeExists(const std::string& treeId) {
    std::string path = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\" + treeId + ".json";
    return std::filesystem::exists(path);
}

// Helper para converter o ID hexadecimal do config legado (ex: 0x85A) para string formatada (ex: SimpleFist.esp|85A)
std::string FormatLegacyID(const std::string& plugin, uint32_t id) {
    return fmt::format("{}|{:X}", plugin, id & 0xFFFFFF);
}

RE::ActorValue GetActorValueFromName(const std::string& skillName) {
    if (skillName == "One-Handed") return RE::ActorValue::kOneHanded;
    if (skillName == "Two-Handed") return RE::ActorValue::kTwoHanded;
    if (skillName == "Archery") return RE::ActorValue::kArchery;
    if (skillName == "Block") return RE::ActorValue::kBlock;
    if (skillName == "Smithing") return RE::ActorValue::kSmithing;
    if (skillName == "Heavy Armor") return RE::ActorValue::kHeavyArmor;
    if (skillName == "Light Armor") return RE::ActorValue::kLightArmor;
    if (skillName == "Pickpocket") return RE::ActorValue::kPickpocket;
    if (skillName == "Lockpicking") return RE::ActorValue::kLockpicking;
    if (skillName == "Sneak") return RE::ActorValue::kSneak;
    if (skillName == "Alchemy") return RE::ActorValue::kAlchemy;
    if (skillName == "Speech") return RE::ActorValue::kSpeech;
    if (skillName == "Alteration") return RE::ActorValue::kAlteration;
    if (skillName == "Conjuration") return RE::ActorValue::kConjuration;
    if (skillName == "Destruction") return RE::ActorValue::kDestruction;
    if (skillName == "Illusion") return RE::ActorValue::kIllusion;
    if (skillName == "Restoration") return RE::ActorValue::kRestoration;
    if (skillName == "Enchanting") return RE::ActorValue::kEnchanting;
    if (skillName == "Vampirism") return RE::ActorValue::kVampirePerks;
    if (skillName == "Werewolf") return RE::ActorValue::kWerewolfPerks;
    return RE::ActorValue::kNone;
}

// =========================================================================================
// HELPER: Converter ActorValue Enum para String (Necessário para o target do any_skill)
// =========================================================================================
std::string GetNameFromActorValue(RE::ActorValue av) {
    switch (av) {
    case RE::ActorValue::kOneHanded: return "One-Handed";
    case RE::ActorValue::kTwoHanded: return "Two-Handed";
    case RE::ActorValue::kArchery: return "Archery";
    case RE::ActorValue::kBlock: return "Block";
    case RE::ActorValue::kSmithing: return "Smithing";
    case RE::ActorValue::kHeavyArmor: return "Heavy Armor";
    case RE::ActorValue::kLightArmor: return "Light Armor";
    case RE::ActorValue::kPickpocket: return "Pickpocket";
    case RE::ActorValue::kLockpicking: return "Lockpicking";
    case RE::ActorValue::kSneak: return "Sneak";
    case RE::ActorValue::kAlchemy: return "Alchemy";
    case RE::ActorValue::kSpeech: return "Speech";
    case RE::ActorValue::kAlteration: return "Alteration";
    case RE::ActorValue::kConjuration: return "Conjuration";
    case RE::ActorValue::kDestruction: return "Destruction";
    case RE::ActorValue::kIllusion: return "Illusion";
    case RE::ActorValue::kRestoration: return "Restoration";
    case RE::ActorValue::kEnchanting: return "Enchanting";
    case RE::ActorValue::kVampirePerks: return "Vampirism";
    case RE::ActorValue::kWerewolfPerks: return "Werewolf";
    default: return "Unknown";
    }
}

// =========================================================================================
// NOVA FUNÇÃO CENTRALIZADA: Extrai os requisitos de um Perk
// =========================================================================================
json GetPerkRequirements(RE::BGSPerk* perk) {
    json requirements = json::array();
    if (!perk || !perk->perkConditions.head) return requirements;

    RE::TESConditionItem* condItem = perk->perkConditions.head;

    while (condItem) {
        uint16_t funcId = static_cast<uint16_t>(condItem->data.functionData.function.get());

        bool isOr = condItem->data.flags.isOR;
        int opCode = static_cast<int>(condItem->data.flags.opCode);

        std::string perkEditorID = clib_util::editorID::get_editorID(perk);
        logger::debug("DEBUG REQ -> Perk: {} | Encontrou funcId: {}",
            perkEditorID.empty() ? "Unknown" : perkEditorID,
            funcId
        );

        // Valor de comparação (Target Value)
        float compValue = condItem->data.comparisonValue.f;
        if (condItem->data.flags.global && condItem->data.comparisonValue.g) {
            compValue = condItem->data.comparisonValue.g->value;
        }

        json req = json::object();

        // 1. GetGlobalValue (Geralmente usado para Nível de Custom Skills)
        if (funcId == 12) {
            if (condItem->data.functionData.params[0]) {
                auto globalVar = static_cast<RE::TESGlobal*>(condItem->data.functionData.params[0]);
                if (globalVar) {
                    req["type"] = "level";
                    req["value"] = static_cast<int>(compValue);
                }
            }
        }
        // 2. GetActorValue (14) ou GetBaseActorValue (277) -> AGORA USA "any_skill"
        else if (funcId == 14 || funcId == 277) {
            uint64_t paramVal = reinterpret_cast<uint64_t>(condItem->data.functionData.params[0]);
            RE::ActorValue av = static_cast<RE::ActorValue>(paramVal);

            std::string skillTarget = GetNameFromActorValue(av);

            req["type"] = "any_skill";
            req["target"] = skillTarget;
            req["value"] = static_cast<int>(compValue);
        }
        // 3. HasPerk (448) -> Requisito de Perk Anterior
        else if (funcId == 448) {
            auto* reqPerk = static_cast<RE::BGSPerk*>(condItem->data.functionData.params[0]);
            if (reqPerk) {
                auto file = reqPerk->GetFile(0);
                std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
                uint32_t localID = (reqPerk->GetFormID() & 0xFF000000) == 0xFE000000
                    ? (reqPerk->GetFormID() & 0xFFF) : (reqPerk->GetFormID() & 0xFFFFFF);

                req["type"] = "perk";
                req["value"] = fmt::format("{}|{:X}", plugin, localID);
                if ((opCode == 0 && compValue == 0.0f) || (opCode == 1 && compValue != 0.0f)) req["isNot"] = true;
            }
        }
        // 4. GetQuestCompleted (543)
        else if (funcId == 543) {
            auto* reqQuest = static_cast<RE::TESQuest*>(condItem->data.functionData.params[0]);
            if (reqQuest) {
                auto file = reqQuest->GetFile(0);
                std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
                uint32_t localID = (reqQuest->GetFormID() & 0xFF000000) == 0xFE000000
                    ? (reqQuest->GetFormID() & 0xFFF) : (reqQuest->GetFormID() & 0xFFFFFF);

                req["type"] = "quest_completed";
                req["value"] = fmt::format("{}|{:X}", plugin, localID);
                if ((opCode == 0 && compValue == 0.0f) || (opCode == 1 && compValue != 0.0f)) req["isNot"] = true;
            }
        }
        // 5. HasSpell (264)
        else if (funcId == 264) {
            auto* reqSpell = static_cast<RE::SpellItem*>(condItem->data.functionData.params[0]);
            if (reqSpell) {
                auto file = reqSpell->GetFile(0);
                std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
                uint32_t localID = (reqSpell->GetFormID() & 0xFF000000) == 0xFE000000
                    ? (reqSpell->GetFormID() & 0xFFF) : (reqSpell->GetFormID() & 0xFFFFFF);

                req["type"] = "spell";
                req["value"] = fmt::format("{}|{:X}", plugin, localID);
                if ((opCode == 0 && compValue == 0.0f) || (opCode == 1 && compValue != 0.0f)) req["isNot"] = true;
            }
        }
        // 6. HasShout (378)
        else if (funcId == 378) {
            auto* reqShout = static_cast<RE::TESShout*>(condItem->data.functionData.params[0]);
            if (reqShout) {
                auto file = reqShout->GetFile(0);
                std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
                uint32_t localID = (reqShout->GetFormID() & 0xFF000000) == 0xFE000000
                    ? (reqShout->GetFormID() & 0xFFF) : (reqShout->GetFormID() & 0xFFFFFF);

                req["type"] = "shout";
                req["value"] = fmt::format("{}|{:X}", plugin, localID);
                if ((opCode == 0 && compValue == 0.0f) || (opCode == 1 && compValue != 0.0f)) req["isNot"] = true;
            }
        }

        // Único push_back responsável por adicionar o item formatado na lista final
        if (!req.empty()) {
            req["isOr"] = isOr;
            requirements.push_back(req);
        }

        condItem = condItem->next;
    }

    return requirements;
}

// Helper para dividir a string (copiado do Rule.cpp)
std::vector<std::string> split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Função de Parsing adaptada para o Prisma
RE::FormID ParseFormIDString(const std::string& a_formIDStr) {
    if (a_formIDStr.empty()) return 0;

    auto tokens = split(a_formIDStr, '|');
    if (tokens.size() == 2) {
        // Formato: "Plugin.esp|HexID"
        try {
            uint32_t localID = std::stoul(tokens[1], nullptr, 16);
            auto dataHandler = RE::TESDataHandler::GetSingleton();
            if (dataHandler) {
                // Retorna o FormID resolvido com o prefixo do plugin (ex: 0100085A)
                return dataHandler->LookupFormID(localID, tokens[0]);
            }
        }
        catch (...) {
            return 0;
        }
    }
    return 0;
}

void SyncExternalSkillLevel(const std::string& skillId, const std::string& globalIdStr) {
    if (globalIdStr.empty()) return;

    // 1. Resolve o ID da Global (ex: "SimpleFist.esp|87B")
    RE::FormID globalFormID = ParseFormIDString(globalIdStr);
    if (globalFormID == 0) return;

    // 2. Busca o objeto Global na memória
    auto globalVar = RE::TESForm::LookupByID<RE::TESGlobal>(globalFormID);
    if (!globalVar) return;

    // 3. Lê o valor atual (float) e converte para int
    int externalLevel = static_cast<int>(globalVar->value);

    auto mgr = Manager::GetSingleton();
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    int currentLevel = mgr->GetCustomSkillLevel(player, skillId);
    if (externalLevel > currentLevel) {
        logger::info("Sincronizando Nivel '{}': Prisma({}) -> Global({})", skillId, currentLevel, externalLevel);
        mgr->SetCustomSkillLevel(player, skillId, externalLevel);
        mgr->SetCustomSkillXP(player, skillId, 0.0f);
    }
}

// =========================================================================================
// ATUALIZADO: EnrichPerkData (Usa Cache do Manager + Suporte a Ranks)
// =========================================================================================
void EnrichPerkData(RE::BGSPerk* perk, json& nodeData) {
    if (!perk) return;

    auto mgr = Manager::GetSingleton();
    RE::FormID formID = perk->GetFormID();

    // 1. DADOS BÁSICOS (Nome e Descrição)
    // Tenta buscar no cache do Manager primeiro
    const InternalFormInfo* cachedInfo = mgr->GetInfoByID("Perk", formID);

    if (cachedInfo) {
        nodeData["name"] = cachedInfo->name.empty() ? "Unknown Perk" : cachedInfo->name;
        nodeData["description"] = cachedInfo->description;
    }
    else {
        // Fallback: Leitura direta da engine (Atualizado para não passar o '0' no final)
        const char* fn = perk->GetFullName();
        nodeData["name"] = (fn && strlen(fn) > 0) ? fn : "Unknown Perk";

        RE::BSString descStr;
        // FIX: Removemos o ', 0' para garantir que pegue descrições de mods também, se necessário
        static_cast<RE::TESDescription*>(perk)->GetDescription(descStr, perk);
        nodeData["description"] = descStr.empty() ? "" : mgr->ToUTF8(descStr.c_str());
    }

    // 2. REQUISITOS (Sempre via Engine/Helper centralizado)
    nodeData["requirements"] = GetPerkRequirements(perk);

    // 3. LÓGICA DE NEXT RANKS (Recursiva para pegar a cadeia completa)
    // Verifica se tem um próximo perk linkado.
    std::string nextPerkStr = cachedInfo ? cachedInfo->nextPerkId : "";

    // Se não tiver no cache, tenta pegar da engine
    if (nextPerkStr.empty() && perk->nextPerk) {
        auto nextP = perk->nextPerk;
        auto file = nextP->GetFile(0);
        std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
        uint32_t localID = (nextP->GetFormID() & 0xFF000000) == 0xFE000000 ? (nextP->GetFormID() & 0xFFF) : (nextP->GetFormID() & 0xFFFFFF);
        nextPerkStr = fmt::format("{}|{:X}", plugin, localID);
    }

    json ranksArray = json::array();
    int safetyCount = 0;

    while (!nextPerkStr.empty() && safetyCount < 10) {
        RE::FormID nextFormID = ParseFormIDString(nextPerkStr);
        if (nextFormID == 0) break;

        auto nextPerkPtr = RE::TESForm::LookupByID<RE::BGSPerk>(nextFormID);
        if (!nextPerkPtr) break;

        // Busca info deste rank no Manager
        const InternalFormInfo* rankInfo = mgr->GetInfoByID("Perk", nextFormID);

        json rankData;
        rankData["perk"] = nextPerkStr;
        rankData["id"] = nextPerkStr;
        rankData["perkCost"] = 1;

        if (rankInfo) {
            rankData["name"] = rankInfo->name;
            rankData["description"] = rankInfo->description;
            nextPerkStr = rankInfo->nextPerkId; // Avança para o próximo usando o cache
        }
        else {
            // Fallback manual
            const char* rfn = nextPerkPtr->GetFullName();
            rankData["name"] = (rfn) ? rfn : "Rank";

            RE::BSString rDesc;
            static_cast<RE::TESDescription*>(nextPerkPtr)->GetDescription(rDesc, nextPerkPtr);
            rankData["description"] = rDesc.empty() ? "" : mgr->ToUTF8(rDesc.c_str());

            // Tenta achar o próximo pela engine
            if (nextPerkPtr->nextPerk) {
                auto np = nextPerkPtr->nextPerk;
                auto f = np->GetFile(0);
                std::string p = f ? std::string(f->GetFilename()) : "Skyrim.esm";
                uint32_t lid = (np->GetFormID() & 0xFF000000) == 0xFE000000 ? (np->GetFormID() & 0xFFF) : (np->GetFormID() & 0xFFFFFF);
                nextPerkStr = fmt::format("{}|{:X}", p, lid);
            }
            else {
                nextPerkStr = "";
            }
        }

        // Importante: Pegar os requirements deste Rank específico!
        rankData["requirements"] = GetPerkRequirements(nextPerkPtr);

        ranksArray.push_back(rankData);
        safetyCount++;
    }

    nodeData["nextRanks"] = ranksArray;
}

// =========================================================================================
// CONVERSÃO DE CUSTOM SKILLS FRAMEWORK (.JSON)
// =========================================================================================
void ConvertCSFJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        json csfData = json::parse(file);
        json skillsToProcess = json::array();

        // 1. Detecta o formato do arquivo
        if (csfData.contains("skills") && csfData["skills"].is_array()) {
            // Formato de coleção (ex: SKILLS.json antigo ou mesclado)
            skillsToProcess = csfData["skills"];
        }
        else if (csfData.contains("id") && csfData.contains("nodes")) {
            // Formato individual (seu caso atual: Athletics.json, Sorcery.json, etc.)
            skillsToProcess.push_back(csfData);
        }
        else {
            // Não é um arquivo de skill válido
            return;
        }

        for (auto& skill : skillsToProcess) {
            if (skill.is_string()) continue;

            // Extração segura do ID
            std::string skillId = path.stem().string();
            if (skill.contains("id") && skill["id"].is_string()) {
                skillId = skill["id"].get<std::string>();
            }

            // Verifica se a árvore já foi convertida anteriormente
            if (PrismaTreeExists(skillId)) {
                logger::debug("Skill tree '{}' ja existe. Ignorando conversao CSF.", skillId);
                continue; // Pula para a próxima skill sem reconverter
            }


            // Extração segura do Level (evita crash se for "level": null)
            std::string levelGlobalStr = "";
            if (skill.contains("level") && skill["level"].is_string()) {
                levelGlobalStr = skill["level"].get<std::string>();
            }

            SyncExternalSkillLevel(skillId, levelGlobalStr);

            std::map<std::string, std::string> idToPerkMap;
            if (skill.contains("nodes") && skill["nodes"].is_array()) {
                for (auto& node : skill["nodes"]) {
                    std::string oldId = node.value("id", "");
                    std::string perkStr = node.value("perk", "");
                    if (!oldId.empty() && !perkStr.empty()) {
                        idToPerkMap[oldId] = perkStr;
                    }
                }
            }

            logger::info("Convertendo Custom Skill (CSF): {}", skillId);

            json prismaTree;
            prismaTree["name"] = skillId;
            std::string displayName = skill.value("name", skillId);
            if (skillId == "Athletics" || skillId == "HandtoHand" || skillId == "Sorcery" 
                || skillId == "Exploration" || skillId == "Horseman" || skillId == "Philosophy") {
                displayName = skillId; 
            }
            prismaTree["displayName"] = displayName;
            prismaTree["isVanilla"] = false;
            prismaTree["category"] = "Custom";
            prismaTree["color"] = "#FFFFFF";

            if (!levelGlobalStr.empty()) {
                prismaTree["oldLevel"] = levelGlobalStr;
            }

            if (skill.contains("experienceFormula")) {
                prismaTree["experienceFormula"] = skill["experienceFormula"];
            }
            else {
                // Padrão se não existir no arquivo original
                prismaTree["experienceFormula"] = {
                    {"useMult", 1.0},
                    {"useOffset", 0.0},
                    {"improveMult", 1.0},
                    {"improveOffset", 0.0}
                };
            }

            json nodesArray = json::array();
            if (skill.contains("nodes") && skill["nodes"].is_array()) {
                for (auto& node : skill["nodes"]) {
                    json pNode;
                    std::string perkStr = node.value("perk", "");

                    pNode["id"] = perkStr;
                    pNode["perk"] = perkStr;
                    pNode["x"] = node.value("x", 0.0f) * 10.0f + 50.0f;
                    pNode["y"] = 80.0f - (node.value("y", 0.0f) * 10.0f);
                    pNode["perkCost"] = 1;
                    pNode["name"] = (node.contains("name") && node["name"].is_string())
                        ? node["name"].get<std::string>()
                        : "Unknown Perk";
                    pNode["description"] = "";
                    json translatedLinks = json::array();
                    if (node.contains("links") && node["links"].is_array()) {
                        for (auto& link : node["links"]) {
                            std::string linkStr = link.get<std::string>();
                            // Se o nome do link existir no nosso mapa, trocamos pelo Perk ID
                            if (idToPerkMap.count(linkStr)) {
                                translatedLinks.push_back(idToPerkMap[linkStr]);
                            }
                            else {
                                // Se não estiver no mapa (ex: ja é um perk id), mantém o original
                                translatedLinks.push_back(linkStr);
                            }
                        }
                    }
                    pNode["links"] = translatedLinks;
                    // --- ENRIQUECIMENTO ---
                    // Tenta achar o perk na memória para pegar descrição e requerimentos reais
                    RE::FormID formID = ParseFormIDString(perkStr);
                    if (formID != 0) {
                        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(formID);
                        if (perk) {
                            EnrichPerkData(perk, pNode);
                        }
                        else {
                            logger::warn("ConvertCSFJson: Perk nao encontrado na memoria: {}", perkStr);
                        }
                    }
                    // ----------------------

                    nodesArray.push_back(pNode);
                }
            }
            prismaTree["nodes"] = nodesArray;
			logger::info("CSF '{}' convertido com {} nodes.", skillId, nodesArray.size());
            std::string outPath = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\" + skillId + ".json";
            std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees");
            std::ofstream outFile(outPath);
            outFile << prismaTree.dump(4);
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro ao converter JSON CSF {}: {}", path.string(), e.what());
    }
}

// =========================================================================================
// CONVERSÃO DE LEGACY CONFIG (.TXT)
// =========================================================================================
void ConvertLegacyConfig(const std::filesystem::path& path) {
    std::string filename = path.filename().string();
    size_t start = filename.find("CustomSkill.") + 12;
    size_t end = filename.find(".config");
    if (start == std::string::npos || end == std::string::npos) return;
    std::string skillId = filename.substr(start, end - start);

    if (PrismaTreeExists(skillId)) {
        logger::debug("Skill tree '{}' ja existe. Ignorando.", skillId);
        return;
    }

    logger::info("Convertendo Skill Tree Legada (.config): {}", skillId);

    std::ifstream file(path);
    std::string line, displayName = skillId;
    std::string levelFile = "";
    uint32_t levelId = 0;

    float useMult = 1.0f;
    float useOffset = 0.0f;
    float improveMult = 1.0f;
    float improveOffset = 0.0f;

    // Estrutura temporária para guardar os dados crus antes de processar links
    struct RawNode {
        uint32_t perkIdInt = 0;
        std::string perkFile = "";
        float x = 0.0f;
        float y = 0.0f;
        float gridX = 0.0f; // NOVO: Capturar GridX
        float gridY = 0.0f; // NOVO: Capturar GridY
        std::string rawLinks = ""; // String "2,3" ou "2 3"
    };
    std::map<int, RawNode> rawNodes;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Limpeza básica de CR (carriage return) se houver
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.find("Name =") != std::string::npos) {
            size_t firstQuote = line.find("\"");
            size_t lastQuote = line.rfind("\"");
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && lastQuote > firstQuote) {
                displayName = line.substr(firstQuote + 1, lastQuote - firstQuote - 1);
            }
        }

        if (line.find("LevelFile") != std::string::npos) {
            size_t eqPos = line.find("=");
            if (eqPos != std::string::npos) {
                std::string val = line.substr(eqPos + 1);
                // Trim e remove aspas
                val.erase(0, val.find_first_not_of(" \t"));
                val.erase(val.find_last_not_of(" \t") + 1);
                if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                    val = val.substr(1, val.size() - 2);
                }
                levelFile = val;
            }
        }
        if (line.find("LevelId") != std::string::npos) {
            size_t eqPos = line.find("=");
            if (eqPos != std::string::npos) {
                std::string val = line.substr(eqPos + 1);
                try { levelId = std::stoul(val, nullptr, 16); }
                catch (...) {}
            }
        }

        // --- PARSING OPCIONAL DA FÓRMULA NO TXT ---
        auto ParseFloatVal = [&](const std::string& keyStr, float& targetVar) {
            if (line.find(keyStr) != std::string::npos) {
                size_t eqPos = line.find("=");
                if (eqPos != std::string::npos) {
                    try { targetVar = std::stof(line.substr(eqPos + 1)); }
                    catch (...) {}
                }
            }
            };

        ParseFloatVal("UseMult", useMult);
        ParseFloatVal("UseOffset", useOffset);
        ParseFloatVal("ImproveMult", improveMult);
        ParseFloatVal("ImproveOffset", improveOffset);

        // Parsing de Node
        if (line.find("Node") == 0 && line.find(".") != std::string::npos) {
            size_t dotPos = line.find(".");
            try {
                int nodeIdx = std::stoi(line.substr(4, dotPos - 4));

                size_t eqPos = line.find("=");
                if (eqPos != std::string::npos) {
                    std::string key = line.substr(dotPos + 1, eqPos - dotPos - 1);
                    // Trim na key
                    key.erase(0, key.find_first_not_of(" \t"));
                    key.erase(key.find_last_not_of(" \t") + 1);

                    std::string val = line.substr(eqPos + 1);
                    // Trim no val
                    val.erase(0, val.find_first_not_of(" \t"));
                    val.erase(val.find_last_not_of(" \t") + 1);

                    if (key == "PerkId") rawNodes[nodeIdx].perkIdInt = std::stoul(val, nullptr, 16);
                    if (key == "PerkFile") {
                        // Remove as aspas
                        if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
                            val = val.substr(1, val.size() - 2);
                        }
                        rawNodes[nodeIdx].perkFile = val;
                    }
                    if (key == "X") rawNodes[nodeIdx].x = std::stof(val);
                    if (key == "Y") rawNodes[nodeIdx].y = std::stof(val);
                    if (key == "GridX") rawNodes[nodeIdx].gridX = std::stof(val);
                    if (key == "GridY") rawNodes[nodeIdx].gridY = std::stof(val);
                    if (key == "Links") rawNodes[nodeIdx].rawLinks = val;
                }
            }
            catch (...) { continue; }
        }
    }

    json prismaTree;
    prismaTree["name"] = skillId;
    prismaTree["displayName"] = displayName;
    prismaTree["isVanilla"] = false;
    prismaTree["category"] = "Custom";
    prismaTree["color"] = "#FFFFFF";

    if (!levelFile.empty() && levelId != 0) {
        std::string formattedLevelGlobal = FormatLegacyID(levelFile, levelId);
        SyncExternalSkillLevel(skillId, formattedLevelGlobal);
        prismaTree["oldLevel"] = formattedLevelGlobal;
    }

    prismaTree["experienceFormula"] = {
        {"useMult", useMult},
        {"useOffset", useOffset},
        {"improveMult", improveMult},
        {"improveOffset", improveOffset}
    };

    json nodesArray = json::array();

    // Map auxiliar: Index do Node -> ID Formatado (para resolver os links depois)
    std::map<int, std::string> nodeIndexToID;

    // Variáveis para encontrar os limites (Bounding Box) da árvore
    float minX = 10000.0f, maxX = -10000.0f;
    float minY = 10000.0f, maxY = -10000.0f;

    // Passada 1: Criar os IDs formatados e descobrir o tamanho real da árvore
    for (auto const& [idx, data] : rawNodes) {
        if (idx == 0 || data.perkIdInt == 0) continue;

        std::string fID = FormatLegacyID(data.perkFile, data.perkIdInt);
        nodeIndexToID[idx] = fID;

        // Calcula a posição real do nó no espaço do Skyrim
        float trueX = data.gridX + data.x;
        float trueY = data.gridY + data.y;

        // Atualiza os limites
        if (trueX < minX) minX = trueX;
        if (trueX > maxX) maxX = trueX;
        if (trueY < minY) minY = trueY;
        if (trueY > maxY) maxY = trueY;
    }

    // Calcula a área total que a árvore ocupa
    float rangeX = (maxX - minX);
    float rangeY = (maxY - minY);
    if (rangeX <= 0) rangeX = 1.0f; // Previne divisão por zero caso a árvore tenha só 1 perk
    if (rangeY <= 0) rangeY = 1.0f;

    // Passada 2: Montar o JSON com as coordenadas normalizadas
    for (auto const& [idx, data] : rawNodes) {
        if (idx == 0 || data.perkIdInt == 0) continue;

        json n;
        std::string fID = nodeIndexToID[idx];

        float trueX = data.gridX + data.x;
        float trueY = data.gridY + data.y;

        // NORMALIZAÇÃO PARA A UI (Garante que caberá na tela, independente do arquivo original)
        // Espreme o X para ficar entre 15% e 85% da tela
        float normalizedX = ((trueX - minX) / rangeX) * 70.0f + 15.0f;

        // Espreme o Y para ficar entre 20% e 80% da altura da tela (Invertendo o Y, pois a Web desenha de cima pra baixo)
        float normalizedY = 80.0f - (((trueY - minY) / rangeY) * 60.0f);

        n["id"] = fID;
        n["perk"] = fID;
        n["x"] = normalizedX;
        n["y"] = normalizedY;
        n["perkCost"] = 1;
        n["description"] = "";

        json links = json::array();
        if (!data.rawLinks.empty()) {
            // CORREÇÃO DOS LINKS: Substituir espaços por vírgulas
            std::string safeLinks = data.rawLinks;
            std::replace(safeLinks.begin(), safeLinks.end(), ' ', ',');

            std::vector<std::string> linkIndexes = split(safeLinks, ',');
            for (const auto& sIdx : linkIndexes) {
                if (sIdx.empty()) continue; // Ignora espaços em branco vazios " "

                try {
                    std::string cleanIdx = sIdx;
                    cleanIdx.erase(std::remove(cleanIdx.begin(), cleanIdx.end(), '"'), cleanIdx.end());
                    int targetIdx = std::stoi(cleanIdx);
                    if (nodeIndexToID.count(targetIdx)) {
                        links.push_back(nodeIndexToID[targetIdx]);
                    }
                }
                catch (...) {}
            }
        }
        n["links"] = links;

        RE::FormID formID = ParseFormIDString(fID);
        if (formID != 0) {
            auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(formID);
            if (perk) {
                EnrichPerkData(perk, n);
            }
            else {
                n["name"] = "Perk Not Found";
                n["description"] = "Could not lookup ID in memory.";
                n["requirements"] = json::array();
            }
        }
        else {
            n["name"] = "Unknown Perk";
            n["description"] = "Perk data not loaded (ID 0).";
            n["requirements"] = json::array();
        }

        nodesArray.push_back(n);
    }

    prismaTree["nodes"] = nodesArray;

    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees");
    std::ofstream outFile("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\" + skillId + ".json");
    outFile << prismaTree.dump(4);
}

void ScanAndConvertExternalSkills() {
    logger::info("Iniciando varredura de skills externas...");

    // 1. Escanear Custom Skills Framework (JSON)
    std::filesystem::path csfDir("Data\\SKSE\\Plugins\\CustomSkills");
    if (std::filesystem::exists(csfDir)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(csfDir)) {
            // Verifica se é um arquivo e se a extensão é .json
            if (entry.is_regular_file() && entry.path().extension() == ".json") {

                std::string filename = entry.path().filename().string();

                // Ignora especificamente o arquivo SKILLS.json
                if (filename == "SKILLS.json") {
                    logger::debug("Ignorando arquivo de config global: {}", filename);
                    continue;
                }

                ConvertCSFJson(entry.path());
            }
        }
    }

    // 2. Escanear NetScriptFramework (Legacy .config)
    std::filesystem::path legacyDir("Data\\NetScriptFramework\\Plugins");
    if (std::filesystem::exists(legacyDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(legacyDir)) {
            std::string fname = entry.path().filename().string();
            if (fname.find("CustomSkill.") != std::string::npos && fname.find(".config.txt") != std::string::npos) {
                ConvertLegacyConfig(entry.path());
            }
        }
    }
}

static void ApplyDefaultSpecialResourceCost(json& nodeData, const std::string& resourceId) {
    if (resourceId.empty()) return;
    if (IsResourceSuppressed(resourceId)) return;

    json customCost = json::array({
        {
            {"resourceId", resourceId},
            {"amount", 1}
        }
    });

    nodeData["perkCost"] = 0;
    nodeData["customCosts"] = customCost;

    if (nodeData.contains("nextRanks") && nodeData["nextRanks"].is_array()) {
        for (auto& rank : nodeData["nextRanks"]) {
            rank["perkCost"] = 0;
            rank["customCosts"] = customCost;
        }
    }
}

static void MigrateDefaultSpecialResourceCosts(const std::string& filePath, const std::string& resourceId) {
    if (resourceId.empty() || !std::filesystem::exists(filePath)) return;
    if (IsResourceSuppressed(resourceId)) return;

    std::ifstream inFile(filePath);
    if (!inFile.is_open()) return;

    try {
        json treeData = json::parse(inFile);
        inFile.close();

        if (!treeData.contains("nodes") || !treeData["nodes"].is_array()) return;

        bool changed = false;
        auto hasPayableCustomCost = [](const json& item) {
            if (!item.contains("customCosts") || !item["customCosts"].is_array()) return false;
            for (const auto& cost : item["customCosts"]) {
                if (!cost.value("resourceId", "").empty() && cost.value("amount", 0.0f) >= 1.0f) {
                    return true;
                }
            }
            return false;
        };

        for (auto& node : treeData["nodes"]) {
            if (!hasPayableCustomCost(node)) {
                ApplyDefaultSpecialResourceCost(node, resourceId);
                changed = true;
            }
            else if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                for (auto& rank : node["nextRanks"]) {
                    if (!hasPayableCustomCost(rank)) {
                        ApplyDefaultSpecialResourceCost(rank, resourceId);
                        changed = true;
                    }
                }
            }
        }

        if (changed) {
            std::ofstream outFile(filePath);
            if (outFile.is_open()) {
                outFile << treeData.dump(4);
            }
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro ao migrar custos especiais em {}: {}", filePath, e.what());
    }
}

void ExportVanillaPerkTree(RE::ActorValue actorValue, const std::string& skillName, const std::string& categoryName, const std::string& iconName, const std::string& bgName) {
    std::string dirPath = "Data\\PrismaUI\\views\\SkillMenu\\Skill Trees";
    std::filesystem::create_directories(dirPath);
    std::string filePath = dirPath + "\\" + skillName + ".json";

    std::string existingSpecialResourceId;
    if (actorValue == RE::ActorValue::kVampirePerks) {
        existingSpecialResourceId = VAMPIRE_RESOURCE_ID;
    }
    else if (actorValue == RE::ActorValue::kWerewolfPerks) {
        existingSpecialResourceId = WEREWOLF_RESOURCE_ID;
    }

    if (std::filesystem::exists(filePath)) {
        MigrateDefaultSpecialResourceCosts(filePath, existingSpecialResourceId);
        return;
    }

    auto avInfo = RE::ActorValueList::GetSingleton()->GetActorValueInfo(actorValue);
    if (!avInfo || !avInfo->perkTree) return;

    logger::info("--- Exportando Arvore Vanilla: {} ---", skillName);

    struct TempNode {
        RE::BGSSkillPerkTreeNode* original;
        float rawX;
        float rawY;
    };
    std::vector<TempNode> allNodes;
    std::set<RE::BGSSkillPerkTreeNode*> visited;
    std::queue<RE::BGSSkillPerkTreeNode*> queue;

    queue.push(avInfo->perkTree);
    visited.insert(avInfo->perkTree);

    float minX = 10000.0f, maxX = -10000.0f;
    float minY = 10000.0f, maxY = -10000.0f;

    // 1. PRIMEIRA PASSADA: Coleta dados e calcula limites (Bfounds)
    while (!queue.empty()) {
        auto node = queue.front();
        queue.pop();

        if (node->perk) {
            // CÁLCULO CORRETO: Grid (inteiro) + Position (float offset)
            float curX = static_cast<float>(node->perkGridX) + node->horizontalPosition;
            float curY = static_cast<float>(node->perkGridY) + node->verticalPosition;

            allNodes.push_back({ node, curX, curY });

            if (curX < minX) minX = curX;
            if (curX > maxX) maxX = curX;
            if (curY < minY) minY = curY;
            if (curY > maxY) maxY = curY;
        }

        for (auto child : node->children) {
            if (child && visited.find(child) == visited.end()) {
                visited.insert(child);
                queue.push(child);
            }
        }
    }

    float rangeX = (maxX - minX);
    float rangeY = (maxY - minY);
    if (rangeX <= 0) rangeX = 1.0f;
    if (rangeY <= 0) rangeY = 1.0f;

    // 2. SEGUNDA PASSADA: Normaliza para 0-100 (React)
    json treeData;
    treeData["name"] = skillName;
    treeData["displayName"] = avInfo->GetFullName() ? avInfo->GetFullName() : skillName;
    treeData["isVanilla"] = true;
    treeData["color"] = "#FFFFFF";
    treeData["category"] = categoryName;
    treeData["iconPath"] = iconName.empty() ? "" : "./Assets/" + iconName;
    treeData["bgPath"] = bgName.empty() ? "" : "./Assets/" + bgName;
    treeData["iconPerkPath"] = "./Assets/Perk.svg";
    json treeReqs = json::array();
    if (actorValue == RE::ActorValue::kWerewolfPerks) {
        treeReqs.push_back({ {"type", "is_werewolf"}, {"value", 1} });
    }
    else if (actorValue == RE::ActorValue::kVampirePerks) {
        treeReqs.push_back({ {"type", "is_vampire"}, {"value", 1} });
    }

    if (!treeReqs.empty()) {
        treeData["treeRequirements"] = treeReqs;
    }

    json nodesArray = json::array();
    auto mgr = Manager::GetSingleton();
    std::string defaultSpecialResourceId;
    if (actorValue == RE::ActorValue::kVampirePerks) {
        defaultSpecialResourceId = VAMPIRE_RESOURCE_ID;
    }
    else if (actorValue == RE::ActorValue::kWerewolfPerks) {
        defaultSpecialResourceId = WEREWOLF_RESOURCE_ID;
    }

    auto getFormattedID = [](RE::TESForm* f) -> std::string {
        if (!f) return "";
        auto file = f->GetFile(0);
        std::string plugin = file ? std::string(file->GetFilename()) : "Skyrim.esm";
        uint32_t localID = (f->GetFormID() & 0xFF000000) == 0xFE000000 ? (f->GetFormID() & 0xFFF) : (f->GetFormID() & 0xFFFFFF);
        return fmt::format("{}|{:X}", plugin, localID);
        };

    for (auto& tNode : allNodes) {
        RE::BGSPerk* perk = tNode.original->perk;
        json nodeData;
        std::string fID = getFormattedID(perk);

        nodeData["id"] = fID;
        nodeData["perk"] = fID;
        nodeData["name"] = perk->GetFullName() ? perk->GetFullName() : "Unknown";

        // --- NORMALIZAÇÃO PARA A UI ---
        // Mirror X: Inverte o X para bater com o visual do Skyrim (Max - Atual)
        // Deixamos margem de 15% nas laterais para centralizar melhor
        float normalizedX = ((maxX - tNode.rawX) / rangeX) * 70.0f + 15.0f;

        // Inverter Y: Skyrim 0 é a base, UI 0 é o topo.
        // Colocamos entre 20% e 80% da altura da tela
        float normalizedY = 80.0f - ((tNode.rawY - minY) / rangeY) * 60.0f;

        nodeData["x"] = normalizedX;
        nodeData["y"] = normalizedY;

        // Metadados básicos
        RE::BSString descStr;
        perk->TESDescription::GetDescription(descStr, perk, 0);
        nodeData["description"] = mgr->ToUTF8(descStr.c_str());
        nodeData["perkCost"] = 1;

        EnrichPerkData(perk, nodeData);
        ApplyDefaultSpecialResourceCost(nodeData, defaultSpecialResourceId);
        // Conexões (Links)
        json connections = json::array();
        for (auto child : tNode.original->children) {
            if (child && child->perk) {
                connections.push_back(getFormattedID(child->perk));
            }
        }
        nodeData["links"] = connections;

        nodesArray.push_back(nodeData);
    }

    treeData["nodes"] = nodesArray;
    std::ofstream file(filePath);
    if (file.is_open()) {
        file << treeData.dump(4);
        logger::info("Arvore '{}' gerada com sucesso.", skillName);
    }
}

// Função para varrer todas as skills Vanilla (Categorizadas)
void GenerateAllVanillaTrees() {
    logger::info("Verificando a existencia de perk trees vanilla...");

    GetSettings();
    GetLevelRules();
    GetUISettings();
    GetCustomResources();

    struct SkillMapInfo {
        RE::ActorValue av;
        std::string name;
        std::string category;
        std::string icon;
        std::string bg;
    };

    std::vector<SkillMapInfo> vanillaSkills = {
        // Combat
        {RE::ActorValue::kOneHanded, "One-Handed", "Combat", "Skrymbols_oneHand_solid.svg", "Skrymbols_oneHand_threadSm.webp"},
        {RE::ActorValue::kTwoHanded, "Two-Handed", "Combat", "Skrymbols_twoHand_solid.svg", "Skrymbols_twoHand_threadSm.webp"},
        {RE::ActorValue::kArchery, "Archery", "Combat", "Skrymbols_archery_solid.svg", "Skrymbols_archery_threadSm.webp"},
        {RE::ActorValue::kBlock, "Block", "Combat", "Skrymbols_block_solid.svg", "Skrymbols_block_threadSm.webp"},
        {RE::ActorValue::kSmithing, "Smithing", "Combat", "Skrymbols_smithing_solid.svg", "Skrymbols_smithing_threadSm.webp"},
        {RE::ActorValue::kHeavyArmor, "Heavy Armor", "Combat", "Skrymbols_heavyArmor_solid.svg", "Skrymbols_heavyArmor_threadSm.webp"},

        // Stealth
        {RE::ActorValue::kLightArmor, "Light Armor", "Stealth", "Skrymbols_lightArmor_solid.svg", "Skrymbols_lightArmor_threadSm.webp"},
        {RE::ActorValue::kPickpocket, "Pickpocket", "Stealth", "Skrymbols_pickpocket_solid.svg", "Skrymbols_pickpocket_threadSm.webp"},
        {RE::ActorValue::kLockpicking, "Lockpicking", "Stealth", "Skrymbols_lockpicking_solid.svg", "Skrymbols_pickpocket_threadSm.webp"},
        {RE::ActorValue::kSneak, "Sneak", "Stealth", "Skrymbols_sneak_solid.svg", "Skrymbols_sneak_threadSm.webp"},
        {RE::ActorValue::kAlchemy, "Alchemy", "Stealth", "Skrymbols_alchemy_solid.svg", "Skrymbols_alchemy_threadSm.webp"},
        {RE::ActorValue::kSpeech, "Speech", "Stealth", "Skrymbols_speech_solid.svg", "Skrymbols_speech_threadSm.webp"},

        // Magic
        {RE::ActorValue::kAlteration, "Alteration", "Magic", "Skrymbols_alteration_Solid.svg", "Skrymbols_alteration_threadSm.webp"},
        {RE::ActorValue::kConjuration, "Conjuration", "Magic", "Skrymbols_conjuration_solid.svg", "Skrymbols_conjuration_threadSm.webp"},
        {RE::ActorValue::kDestruction, "Destruction", "Magic", "Skrymbols_destruction_solid.svg", "Skrymbols_destruction_threadSm.webp"},
        {RE::ActorValue::kIllusion, "Illusion", "Magic", "Skrymbols_illusion_solid.svg", "Skrymbols_illusion_threadSm.webp"},
        {RE::ActorValue::kRestoration, "Restoration", "Magic", "Skrymbols_restoration_solid.svg", "Skrymbols_restoration_threadSm.webp"},
        {RE::ActorValue::kEnchanting, "Enchanting", "Magic", "Skrymbols_enchanting_solid.svg", "Skrymbols_enchanting_threadSm.webp"},

        // Special (Lobisomem/Vampiro)
        {RE::ActorValue::kVampirePerks, "Vampirism", "Special", "VampIcon.svg", "Skrymbols_vamp_threadSm.webp"},
        {RE::ActorValue::kWerewolfPerks, "Werewolf", "Special", "Skrymbols_werewolf_solid.svg", "Skrymbols_werewolf_threadSm.webp"}
    };

    for (const auto& skill : vanillaSkills) {
        ExportVanillaPerkTree(skill.av, skill.name, skill.category, skill.icon, skill.bg);
    }

    ScanAndConvertExternalSkills();

    logger::info("Verificacao de perk trees vanilla finalizada.");
}

json GetLevelRules() {
    if (g_rulesLoaded) {
        return g_rulesCache;
    }

    std::filesystem::path rulesPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Rules.json");

    if (std::filesystem::exists(rulesPath)) {
        std::ifstream file(rulesPath);
        if (file.is_open()) {
            try {
                g_rulesCache = json::parse(file);
                g_rulesLoaded = true;
                return g_rulesCache;
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler rules.json: {}", e.what());
            }
        }
    }

    json defaultRules = json::array({});
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(rulesPath);
    if (file.is_open()) file << defaultRules.dump(4);

    g_rulesCache = defaultRules;
    g_rulesLoaded = true;
    return g_rulesCache;
}

void SaveLevelRulesToFile(const json& rulesArr) {
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Rules.json");
    if (file.is_open()) {
        file << rulesArr.dump(4);
        logger::info("rules.json salvo com sucesso.");
    }
    g_rulesCache = rulesArr;
    g_rulesLoaded = true;
}

json GetSettings() {
    if (g_settingsLoaded) {
        return g_settingsCache;
    }

    std::filesystem::path settingsPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Settings.json");

    json defaultSettings = {
        {"base", {
            {"perksPerLevel", 1},
            {"healthIncrease", 10.0f},
            {"staminaIncrease", 10.0f},
            {"magickaIncrease", 10.0f},
            {"skillPointsPerLevel", 1},
            {"maxSkillPointsSpendablePerLevel", 10},
            {"skillCap", 100},
            {"useDynamicSkillCap", true},
            {"skillCapPerLevelMult", 2.0f},
            {"applyRacialBonusToCap", true},
            {"enableLegendary", true},
            {"refillAttributesOnLevelUp", false},
            {"useBaseSkillLevel", true},
            {"applyVanillaInitialLevels", true},
            {"carryWeightIncrease", 0.0f},
            {"carryWeightMethod", "none"},
            {"carryWeightLinkedAttributes", json::array({"Stamina"})},
            {"maxPerkPoints", 255},
            {"maxResetsPerActor", -1},
            {"resourceRewards", json::array()}
        }},
        {"followerDetection", {
            {"currentFollowerFactions", json::array()},
            {"potentialFollowerFactions", json::array()},
            {"allowHumanoidTeammates", true},
            {"allowSummoned", false}
        }},
        {"categories", {"Combat", "Magic", "Stealth", "Special", "Custom"}},
        {"codes", json::array({
            {
                {"code", "DEVMODE"},
                {"maxUses", -1},
                {"currentUses", 0},
                {"rewards", json::object()},
                {"isEditorCode", true}
            }
        })}
    };

    if (std::filesystem::exists(settingsPath)) {
        std::ifstream file(settingsPath);
        if (file.is_open()) {
            try {
                json loadedSettings = json::parse(file);
                if (loadedSettings.contains("levelRules")) loadedSettings.erase("levelRules");

                for (auto& [key, value] : defaultSettings["base"].items()) {
                    if (!loadedSettings["base"].contains(key)) loadedSettings["base"][key] = value;
                }
                if (!loadedSettings.contains("categories")) loadedSettings["categories"] = defaultSettings["categories"];
                if (!loadedSettings.contains("followerDetection") ||
                    !loadedSettings["followerDetection"].is_object()) {
                    loadedSettings["followerDetection"] = defaultSettings["followerDetection"];
                }
                else {
                    for (auto& [key, value] : defaultSettings["followerDetection"].items()) {
                        if (!loadedSettings["followerDetection"].contains(key)) {
                            loadedSettings["followerDetection"][key] = value;
                        }
                    }
                }

                g_settingsCache = loadedSettings;
                g_settingsLoaded = true;
                return g_settingsCache;
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler Settings.json: {}", e.what());
            }
        }
    }

    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(settingsPath);
    if (file.is_open()) file << defaultSettings.dump(4);

    g_settingsCache = defaultSettings;
    g_settingsLoaded = true;
    return g_settingsCache;
}

static int GetRuleSpecificity(const json& rule, RE::Actor* actor) {
    const std::string scope = rule.value("scope", "");
    if (scope.empty()) return actor && actor->IsPlayerRef() ? 20 : -1;
    if (scope == "all") return 10;
    if (scope == "player") return actor && actor->IsPlayerRef() ? 20 : -1;
    if (scope == "followers") return actor && !actor->IsPlayerRef() ? 20 : -1;
    if (scope == "actor") {
        return actor && rule.value("actorKey", "") == ActorRuleKey(actor) ? 30 : -1;
    }
    return -1;
}

// Calcula os valores efetivos para um ator e nível, respeitando a precedência
// base < global < player/followers < ator específico.
json GetEffectiveSettings(int targetLevel, RE::Actor* actor) {
    json fullSettings = GetSettings();
    json eff = fullSettings["base"];
    int hardCap = eff.value("skillCap", 100);
    json rules = GetLevelRules();

    if (rules.is_array()) {
        std::vector<std::pair<int, json>> sortedRules;
        for (auto& r : rules) {
            const int specificity = GetRuleSpecificity(r, actor);
            if (specificity >= 0 && r.value("level", 0) <= targetLevel) {
                sortedRules.emplace_back(specificity, r);
            }
        }

        std::sort(sortedRules.begin(), sortedRules.end(), [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second.value("level", 0) < b.second.value("level", 0);
            });

        for (auto& [specificity, rule] : sortedRules) {
            for (auto& [key, val] : rule.items()) {
                if (key != "level" && key != "scope" && key != "actorKey" && key != "actorName") {
                    eff[key] = val;
                }
            }
        }
    }

    int calculatedCap = eff.value("skillCap", 100);
    if (calculatedCap > hardCap) {
        eff["skillCap"] = hardCap;
    }

    return eff;
}

json GetEffectiveSettings(int targetLevel) {
    return GetEffectiveSettings(targetLevel, RE::PlayerCharacter::GetSingleton());
}

static void SaveRulesFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json newRules = json::parse(jsonArgs);
        const auto errors = RequirementService::ValidateRules(newRules);
        if (!errors.empty()) {
            logger::error(
                "Rules.json rejeitado: {} erro(s): {}",
                errors.size(),
                errors.dump());
            return;
        }
        SaveLevelRulesToFile(newRules);
    }
    catch (const std::exception& e) {
        logger::error("Erro ao salvar rules da UI: {}", e.what());
    }
}

// Salva as configurações passando o JSON objeto
void SaveSettingsToFile(const json& settingsObj) {
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Settings.json");
    if (file.is_open()) {
        file << settingsObj.dump(4);
    }

    g_settingsCache = settingsObj;
    g_settingsLoaded = true;
}


json GetLoadedSkillTreeConfigs() {
    json trees = json::array();
    std::filesystem::path dir("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees");

    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
        // Vare a pasta raiz e subpastas buscando arquivos .json
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filePath = entry.path().string();
                if (filePath.find("\\Resources\\") != std::string::npos || filePath.find("/Resources/") != std::string::npos) {
                    continue;
                }
                std::error_code ec;
                auto currentWriteTime = std::filesystem::last_write_time(entry.path(), ec);

                json tree;
                bool useCache = false;

                // 1. Verifica se está no cache e se o arquivo no disco NÃO foi alterado
                if (g_treeCache.find(filePath) != g_treeCache.end() &&
                    g_treeCache[filePath].lastWriteTime == currentWriteTime) {

                    tree = g_treeCache[filePath].data; // Pega da memória!
                    useCache = true;
                }
                else {
                    // 2. Cache Miss ou arquivo desatualizado: Lemos do disco
                    std::ifstream file(entry.path());
                    if (file.is_open()) {
                        try {
                            tree = json::parse(file);
                            if (!tree.contains("nodes") && !tree.contains("isVanilla")) {
                                continue;
                            }
                            bool isVanilla = tree.value("isVanilla", false);
                            std::string treeName = tree.value("name", entry.path().stem().string());

                            tree["name"] = treeName;
                            tree["isVanilla"] = isVanilla;
                            if (!tree.contains("color")) tree["color"] = "#ffffff";
                            if (!tree.contains("initialLevel")) tree["initialLevel"] = 15;
                            if (!tree.contains("bgPath")) tree["bgPath"] = "";
                            if (!tree.contains("iconPath")) tree["iconPath"] = "";
                            if (!tree.contains("selectionIconPath")) tree["selectionIconPath"] = "";
                            if (!tree.contains("iconPerkPath")) tree["iconPerkPath"] = "";

                            // NOVO: SISTEMA DE CATEGORIAS
                            std::string category = "Custom";
                            if (tree.contains("category")) {
                                category = tree["category"];
                            }
                            tree["category"] = category;

                            bool isHidden = tree.value("isHidden", false);
                            tree["isHidden"] = isHidden;

                            // Fallbacks para os nodes (perks)
                            if (!tree.contains("nodes") || !tree["nodes"].is_array() || tree["nodes"].empty()) {
                                tree["nodes"] = json::array();
                            }
                            else {
                                for (auto& node : tree["nodes"]) {
                                    if (!node.contains("icon")) node["icon"] = "";
                                    if (!node.contains("name")) node["name"] = "Unknown Perk";
                                    if (!node.contains("description")) node["description"] = "";
                                    if (!node.contains("perk")) node["perk"] = "";
                                    if (!node.contains("perkCost")) node["perkCost"] = 1;
                                    if (!node.contains("requirements")) node["requirements"] = json::array();
                                    if (!node.contains("links")) node["links"] = json::array();
                                }
                            }
                            tree["_originalFilePath"] = filePath;

                            // Salva no cache a base crua para uso futuro
                            g_treeCache[filePath] = { tree, currentWriteTime };
                        }
                        catch (const std::exception& e) {
                            logger::error("Erro ao ler/processar o JSON {}: {}", filePath, e.what());
                            continue;
                        }
                    }
                }

                if (tree.contains("oldLevel")) {
                    std::string oldLevelStr = tree.value("oldLevel", "");
                    SyncExternalSkillLevel(tree.value("name", ""), oldLevelStr);
                }

                trees.push_back(tree);
            }
        }
    }
    else {
        logger::warn("Pasta Data\\PrismaUI\\views\\{}\\Skill Trees nao encontrada. Nenhuma skill tree carregada.", PRODUCT_NAME);
    }

    return trees;
}

json GetUISettings() {
    if (g_uiSettingsLoaded) {
        return g_uiSettingsCache;
    }

    std::filesystem::path settingsPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\uisettings.json");

    json defaultUISettings = {
        {"language", "NSM_Language"},
        {"hideLockedTreeNames", true},
        {"hideLockedTreeBG", false},
        {"performanceMode", false},
        {"columnPreviewMode", "full"},
        {"enableEditorMode", false},
        {"hidePerkNames", false}
    };

    if (std::filesystem::exists(settingsPath)) {
        std::ifstream file(settingsPath);
        if (file.is_open()) {
            try {
                json loadedSettings = json::parse(file);
                for (auto& [key, value] : defaultUISettings.items()) {
                    if (!loadedSettings.contains(key)) {
                        loadedSettings[key] = value;
                    }
                }

                g_uiSettingsCache = loadedSettings;
                g_uiSettingsLoaded = true;
                return g_uiSettingsCache;
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler uisettings.json: {}", e.what());
            }
        }
    }

    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(settingsPath);
    if (file.is_open()) file << defaultUISettings.dump(4);

    g_uiSettingsCache = defaultUISettings;
    g_uiSettingsLoaded = true;
    return g_uiSettingsCache;
}

// Salvar configurações da UI vindas do React
static void SaveUISettingsFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json newSettings = json::parse(jsonArgs);
        std::filesystem::path dir("Data\\PrismaUI\\views\\" PRODUCT_NAME);
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);

        std::ofstream file("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\uisettings.json");
        if (file.is_open()) {
            file << newSettings.dump(4);
            file.close();
            logger::info("uisettings.json salvo no disco.");
        }

        // ATUALIZA O CACHE DIRETAMENTE
        g_uiSettingsCache = newSettings;
        g_uiSettingsLoaded = true;
    }
    catch (const std::exception& e) {
        logger::error("Erro ao salvar uisettings.json: {}", e.what());
    }
}

std::string GetPlayerSkillsJSON() {
    try {
        auto player = RE::PlayerCharacter::GetSingleton();
        auto actor = GetSelectedActor();
        if (!player || !actor || !actor->Is3DLoaded()) return "{\"player\":null, \"trees\":[]}";

        auto playerSkills = player->GetPlayerRuntimeData().skills;
        if (!actor->IsPlayerRef()) playerSkills = nullptr;
        auto mgr = Manager::GetSingleton();
        mgr->EnsureActorProgress(actor);
        if (actor->IsPlayerRef() && playerSkills) {
            while (playerSkills->CanLevelUp()) {
                playerSkills->AdvanceLevel(false);
                mgr->QueuePendingLevelUps(actor, 1);
            }
        }

        json customResources = GetCustomResources();
        json resourceValuesMap = ResourceService::BuildValues(actor, customResources);

        // --- 1. DADOS BÁSICOS DO JOGADOR (HEADER) ---
        std::string playerName = actor->GetName();
        auto avOwner = actor->AsActorValueOwner();

        float hpCur = avOwner->GetActorValue(RE::ActorValue::kHealth);
        float hpMax = avOwner->GetBaseActorValue(RE::ActorValue::kHealth);
        float mpCur = avOwner->GetActorValue(RE::ActorValue::kMagicka);
        float mpMax = avOwner->GetBaseActorValue(RE::ActorValue::kMagicka);
        float spCur = avOwner->GetActorValue(RE::ActorValue::kStamina);
        float spMax = avOwner->GetBaseActorValue(RE::ActorValue::kStamina);
        int perkPoints = mgr->GetActorPerkPoints(actor);
        int playerLevel = actor->GetLevel();
        int realPlayerLevel = player->GetLevel();
        int dragonSouls = actor->IsPlayerRef() ?
            static_cast<int>(avOwner->GetActorValue(RE::ActorValue::kDragonSouls)) : 0;
        bool hasPendingLevelUp = false;
        float playerLevelProgress = 0.0f;

        json currentEffSettings = GetEffectiveSettings(playerLevel, actor);
        int globalSkillCap = currentEffSettings.value("skillCap", 100);
        bool useDynamicCap = currentEffSettings.value("useDynamicSkillCap", true);
        int baseSkillCap = currentEffSettings.value("baseSkillCap", 18);
        float capMult = currentEffSettings.value("skillCapPerLevelMult", 2.0f);
        bool applyRacial = currentEffSettings.value("applyRacialBonusToCap", true);
        bool useBaseSkill = currentEffSettings.value("useBaseSkillLevel", true);

        if (actor->IsPlayerRef() && playerSkills) {
            hasPendingLevelUp = playerSkills->CanLevelUp();
            if (playerSkills->data && playerSkills->data->levelThreshold > 0) {
                float currentXP = playerSkills->data->xp;
                float reqXP = playerSkills->data->levelThreshold;
                playerLevelProgress = (currentXP / reqXP) * 100.0f;
                playerLevelProgress = std::clamp(playerLevelProgress, 0.0f, 100.0f);
            }
        }
        std::string raceName = "Unknown";
        if (auto race = actor->GetRace()) {
            raceName = race->GetFullName();
        }

        json playerData = {
            {"id", ActorRuntimeKey(actor)},
            {"ruleKey", ActorRuleKey(actor)},
            {"kind", actor->IsPlayerRef() ? "player" : "follower"},
            {"name", playerName},
            {"health", {{"current", hpCur}, {"max", hpMax}}},
            {"magicka", {{"current", mpCur}, {"max", mpMax}}},
            {"stamina", {{"current", spCur}, {"max", spMax}}},
            {"perkPoints", perkPoints},
            {"level", playerLevel},
            {"levelProgress", playerLevelProgress},
            {"race", raceName},
            {"dragonSouls", dragonSouls},
            {"title", actor->IsPlayerRef() ? "Dragonborn" : "Companion"},
            {"pendingLevelUps", mgr->GetPendingLevelUps(actor)},
            {"isLevelUpMenuOpen", actor->IsPlayerRef() && Prisma::IsLevelUpMenuOpen()},
            {"resourceValues", resourceValuesMap},
            {"resetPreview", ResetService::Preview(
                actor,
                {},
                currentEffSettings.value("maxResetsPerActor", -1))}
        };

        // --- 2. COLETA DE NOVAS INFORMAÇÕES GLOBAIS PARA OS REQUISITOS ---

        // A. Vampiro e Lobisomem (Usando as Keywords nativas da Engine)
        bool isVampire = actor->HasKeywordString("Vampire") || actor->HasKeywordString("VampireActive");
        bool isWerewolf = actor->HasKeywordString("Werewolf") || actor->HasKeywordString("ActorTypeCreature");

        // B. Magias Conhecidas (Separação por Escola)
        std::unordered_map<std::string, int> spellsKnownBySchool;
        for (auto spell : actor->GetActorRuntimeData().addedSpells) {
            if (spell && spell->Is(RE::FormType::Spell)) {
                auto sp = spell->As<RE::SpellItem>();
                if (sp && sp->GetSpellType() == RE::MagicSystem::SpellType::kSpell) {
                    auto school = sp->GetAssociatedSkill();
                    if (school == RE::ActorValue::kAlteration) spellsKnownBySchool["Alteration"]++;
                    else if (school == RE::ActorValue::kConjuration) spellsKnownBySchool["Conjuration"]++;
                    else if (school == RE::ActorValue::kDestruction) spellsKnownBySchool["Destruction"]++;
                    else if (school == RE::ActorValue::kIllusion) spellsKnownBySchool["Illusion"]++;
                    else if (school == RE::ActorValue::kRestoration) spellsKnownBySchool["Restoration"]++;
                }
            }
        }

        // C. Kills (Abates Totais)
        // O MiscStatManager não é acessível tão facilmente, mas se você usa Mods que guardam kills numa
        // variável global ou num ActorValue não utilizado, modifique aqui. (Fallback setado para 0).
        int totalKills = 0; // Ex: player->GetActorValue(RE::ActorValue::kVariable01); se estiver salvo em AV.

        // Obtém as configurações carregadas
        json allTrees = GetLoadedSkillTreeConfigs();

        // D. Mapa Global de Levels de TODAS as Skills (necessário para o requisito de Any Skill)
        std::unordered_map<std::string, int> allSkillLevelsMap;
        std::unordered_map<std::string, bool> unlockedNodesMap;

        // --- PRIMEIRA VARREDURA: COLETAR NÍVEIS E PERKS ---
        for (auto& tree : allTrees) {
            bool isVanilla = tree.value("isVanilla", false);
            std::string skillName = tree.value("name", "Unknown");

            // 1. LÊ O NÍVEL INICIAL IMUTÁVEL DO ARQUIVO JSON
            // Para Vanilla é 15. Para Custom pode ser qualquer valor (ex: 1).
            int staticInitialLevel = tree.value("initialLevel", 15);

            // --- INÍCIO: NOVO CÁLCULO DE DYNAMIC SKILL CAP ---
            int treeCap = globalSkillCap;
            if (useDynamicCap) {
                // 2. CÁLCULO ORGÂNICO DO CAP
                // O Cap baseia-se puramente no nível inicial da skill + (Nível do Player * Multiplicador)
                // Ex: Vanilla Lvl 1: 15 + (1 * 2) = 17
                // Ex: Custom Lvl 1:   5 + (1 * 2) = 7
                treeCap = staticInitialLevel + static_cast<int>(playerLevel * capMult);

                // Aplica bônus raciais se ativado nas configurações
                if (applyRacial) {
                    int racialBonus = 0;
                    if (isVanilla) {
                        RE::ActorValue av = GetActorValueFromName(skillName);
                        if (av != RE::ActorValue::kNone && actor->GetRace()) {
                            for (uint32_t i = 0; i < 7; ++i) {
                                if (actor->GetRace()->data.skillBoosts[i].skill == av) {
                                    racialBonus = actor->GetRace()->data.skillBoosts[i].bonus;
                                    break;
                                }
                            }
                        }
                    }
                    treeCap += racialBonus;
                }

                // Impede que o cap dinâmico ultrapasse o limite global/da regra (Hard Cap)
                if (treeCap > globalSkillCap) {
                    treeCap = globalSkillCap;
                }
            }

            tree["cap"] = treeCap;
            // --- FIM: CÁLCULO DE DYNAMIC SKILL CAP ---

            // A partir daqui usamos a leitura normal da Engine (mutável) para saber o progresso da barra
            int currentLevel = staticInitialLevel;
            float progressPercent = 0.0f;

            if (isVanilla) {
                RE::ActorValue av = GetActorValueFromName(skillName);
                if (av != RE::ActorValue::kNone) {
                    // Aqui sim pegamos o nível atual que o jogador upou!
                    if (useBaseSkill) {
                        currentLevel = static_cast<int>(actor->AsActorValueOwner()->GetBaseActorValue(av));
                    }
                    else {
                        currentLevel = static_cast<int>(actor->AsActorValueOwner()->GetActorValue(av));
                    }
                    if (playerSkills && playerSkills->data) {
                        uint32_t avInt = static_cast<uint32_t>(av);
                        if (avInt >= 6 && avInt <= 23) {
                            auto& skillData = playerSkills->data->skills[avInt - 6];
                            if (skillData.levelThreshold > 0) {
                                float calcProgress = (skillData.xp / skillData.levelThreshold) * 100.0f;
                                if (std::isfinite(calcProgress)) {
                                    progressPercent = std::clamp(calcProgress, 0.0f, 100.0f);
                                }
                            }
                        }
                    }
                }
            }
            else {
                int baseLevel = mgr->GetCustomSkillLevel(actor, skillName);
                int bonusLevel = mgr->GetCustomSkillBonus(actor, skillName);

                if (useBaseSkill) {
                    currentLevel = baseLevel;
                }
                else {
                    currentLevel = baseLevel + bonusLevel;
                }

                float currentXP = mgr->GetCustomSkillXP(actor, skillName);
                float reqXP = mgr->GetRequiredXP(skillName, baseLevel);

                if (reqXP > 0.0f) {
                    float calcProgress = (currentXP / reqXP) * 100.0f;
                    if (std::isfinite(calcProgress)) {
                        progressPercent = std::clamp(calcProgress, 0.0f, 100.0f);
                    }
                }
            }

            tree["currentLevel"] = currentLevel;
            tree["currentProgress"] = progressPercent;
            allSkillLevelsMap[skillName] = currentLevel; // Registra na memória global

            // Varre Perks
            if (tree.contains("nodes") && tree["nodes"].is_array()) {
                for (auto& node : tree["nodes"]) {
                    std::string perkStr = node.value("perk", "");
                    bool hasPerk = false;
                    std::string ownershipSource = "none";

                    if (!perkStr.empty()) {
                        RE::FormID fullID = ParseFormIDString(perkStr);
                        if (fullID != 0) {
                            auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(fullID);
                            if (perk) {
                                auto ownership = SnapshotService::GetPerkOwnership(actor, perk);
                                hasPerk = ownership.owned;
                                ownershipSource = ownership.source;
                            }
                        }
                    }

                    node["isUnlocked"] = hasPerk;
                    node["ownershipSource"] = ownershipSource;
                    if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                        for (auto& rank : node["nextRanks"]) {
                            std::string rankPerk = rank.value("perk", "");
                            bool rankHas = false;
                            std::string rankOwnershipSource = "none";
                            if (!rankPerk.empty()) {
                                RE::FormID fullID = ParseFormIDString(rankPerk);
                                if (fullID != 0) {
                                    auto rPerk = RE::TESForm::LookupByID<RE::BGSPerk>(fullID);
                                    if (rPerk) {
                                        auto ownership = SnapshotService::GetPerkOwnership(actor, rPerk);
                                        rankHas = ownership.owned;
                                        rankOwnershipSource = ownership.source;
                                    }
                                }
                            }
                            rank["isUnlocked"] = rankHas;
                            rank["ownershipSource"] = rankOwnershipSource;
                            if (!rankPerk.empty()) unlockedNodesMap[rankPerk] = rankHas;
                            std::string rankId = rank.value("id", "");
                            if (!rankId.empty()) unlockedNodesMap[rankId] = rankHas;
                        }
                    }
                    std::string nodeId = node.value("id", "");
                    if (!nodeId.empty()) {
                        unlockedNodesMap[nodeId] = hasPerk;
                    }
                }
            }
        }

        // --- SEGUNDA VARREDURA: AVALIAR REQUISITOS (Nodes e Trees) ---
        for (auto& tree : allTrees) {
            int currentTreeLevel = tree.value("currentLevel", 15);

            // AVALIA REQUISITOS DA ÁRVORE (Se existirem)
            if (tree.contains("treeRequirements") && tree["treeRequirements"].is_array()) {
                for (auto& req : tree["treeRequirements"]) {
                    bool isMet = false;
                    std::string reqType = req.value("type", "");

                    if (reqType == "level") isMet = (currentTreeLevel >= req.value("value", 0));
                    else if (reqType == "player_level") isMet = (realPlayerLevel >= req.value("value", 0));
                    else if (reqType == "actor_level") isMet = (playerLevel >= req.value("value", 0));
                    else if (reqType == "perk") isMet = unlockedNodesMap[req.value("value", "")];
                    else if (reqType == "is_vampire") isMet = isVampire;
                    else if (reqType == "is_werewolf") isMet = isWerewolf;
                    else if (reqType == "any_skill") isMet = (allSkillLevelsMap[req.value("target", "")] >= req.value("value", 0));
                    else if (reqType == "spells_known") isMet = (spellsKnownBySchool[req.value("target", "")] >= req.value("value", 0));
                    else if (reqType == "kills") isMet = (totalKills >= req.value("value", 0));
                    else if (reqType == "quest_completed") {
                        RE::FormID questID = ParseFormIDString(req.value("value", ""));
                        if (questID != 0) {
                            auto quest = RE::TESForm::LookupByID<RE::TESQuest>(questID);
                            if (quest) isMet = quest->IsCompleted();
                        }
                    }
                    else if (reqType == "spell") {
                        RE::FormID spellID = ParseFormIDString(req.value("value", ""));
                        if (spellID != 0) {
                            auto spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
                            if (spell) isMet = actor->HasSpell(spell);
                        }
                    }
                    else if (reqType == "location_discovered") {
                        RE::FormID locID = ParseFormIDString(req.value("value", ""));
                        if (locID != 0) {
                            auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                            if (loc) isMet = IsLocationDiscovered(loc);
                        }
                    }
                    else if (reqType == "location_cleared") {
                        RE::FormID locID = ParseFormIDString(req.value("value", ""));
                        if (locID != 0) {
                            auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                            if (loc) isMet = loc->IsCleared();
                        }
                    }
                    else if (reqType == "faction") {
                        RE::FormID factID = ParseFormIDString(req.value("value", ""));
                        if (factID != 0) {
                            auto fact = RE::TESForm::LookupByID<RE::TESFaction>(factID);
                            if (fact) isMet = actor->IsInFaction(fact);
                        }
                    }
                    else if (reqType == "book_read") {
                        RE::FormID bookID = ParseFormIDString(req.value("value", ""));
                        if (bookID != 0) {
                            auto book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookID);
                            if (book) isMet = book->IsRead();
                        }
                    }
                    else if (reqType == "shout") {
                        RE::FormID shoutID = ParseFormIDString(req.value("value", ""));
                        if (shoutID != 0) {
                            auto shout = RE::TESForm::LookupByID<RE::TESShout>(shoutID);
                            if (shout) isMet = actor->HasShout(shout);
                        }
                    }
                    else isMet = true; // Fallback
                    if (req.value("isNot", false)) isMet = !isMet;
                    req["isMet"] = isMet;
                }
            }

            // AVALIA REQUISITOS DE PERKS E RANKS
            if (tree.contains("nodes") && tree["nodes"].is_array()) {
                for (auto& node : tree["nodes"]) {
                    bool canUnlock = true;

                    if (node.contains("requirements") && node["requirements"].is_array()) {
                        bool currentChainResult = false; // Resultado do grupo OR atual
                        bool insideOrChain = false;      // Estamos dentro de uma sequência de ORs?
                        bool hasProcessedAny = false;    // Para evitar validar arrays vazios como true sem checar
                        for (auto& req : node["requirements"]) {
                            bool isMet = false;
                            std::string reqType = req.value("type", "");

                            if (reqType == "level") isMet = (currentTreeLevel >= req.value("value", 0));
                            else if (reqType == "player_level") isMet = (realPlayerLevel >= req.value("value", 0));
                            else if (reqType == "actor_level") isMet = (playerLevel >= req.value("value", 0));
                            else if (reqType == "perk") isMet = unlockedNodesMap[req.value("value", "")];
                            else if (reqType == "is_vampire") isMet = isVampire;
                            else if (reqType == "is_werewolf") isMet = isWerewolf;
                            else if (reqType == "any_skill") isMet = (allSkillLevelsMap[req.value("target", "")] >= req.value("value", 0));
                            else if (reqType == "spells_known") isMet = (spellsKnownBySchool[req.value("target", "")] >= req.value("value", 0));
                            else if (reqType == "kills") isMet = (totalKills >= req.value("value", 0));
                            else if (reqType == "quest_completed") {
                                RE::FormID questID = ParseFormIDString(req.value("value", ""));
                                if (questID != 0) {
                                    auto quest = RE::TESForm::LookupByID<RE::TESQuest>(questID);
                                    if (quest) isMet = quest->IsCompleted();
                                }
                            }
                            else if (reqType == "spell") {
                                RE::FormID spellID = ParseFormIDString(req.value("value", ""));
                                if (spellID != 0) {
                                    auto spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
                                    if (spell) isMet = actor->HasSpell(spell);
                                }
                            }
                            else if (reqType == "location_discovered") {
                                RE::FormID locID = ParseFormIDString(req.value("value", ""));
                                if (locID != 0) {
                                    auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                                    if (loc) isMet = IsLocationDiscovered(loc);
                                }
                            }
                            else if (reqType == "location_cleared") {
                                RE::FormID locID = ParseFormIDString(req.value("value", ""));
                                if (locID != 0) {
                                    auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                                    if (loc) isMet = loc->IsCleared();
                                }
                            }
                            else if (reqType == "faction") {
                                RE::FormID factID = ParseFormIDString(req.value("value", ""));
                                if (factID != 0) {
                                    auto fact = RE::TESForm::LookupByID<RE::TESFaction>(factID);
                                    if (fact) isMet = actor->IsInFaction(fact);
                                }
                            }
                            else if (reqType == "book_read") {
                                RE::FormID bookID = ParseFormIDString(req.value("value", ""));
                                if (bookID != 0) {
                                    auto book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookID);
                                    if (book) isMet = book->IsRead();
                                }
                            }
                            else if (reqType == "shout") {
                                RE::FormID shoutID = ParseFormIDString(req.value("value", ""));
                                if (shoutID != 0) {
                                    auto shout = RE::TESForm::LookupByID<RE::TESShout>(shoutID);
                                    if (shout) isMet = actor->HasShout(shout);
                                }
                            }
                            else isMet = true;

                            req["isMet"] = isMet;
                            hasProcessedAny = true;
                            // 2. Lógica Combinatória (AND/OR)
                            bool isOrLink = req.value("isOr", false);

                            if (isOrLink) {
                                // Este item conecta com o PRÓXIMO via OR.
                                // Se este item for verdadeiro, o grupo OR todo vira verdadeiro.
                                if (isMet) currentChainResult = true;
                                insideOrChain = true;
                            }
                            else {
                                // Este item NÃO tem OR, então ele é o fim de uma cadeia (ou um AND isolado)
                                if (insideOrChain) {
                                    // Fim da cadeia OR. Verificamos o último elemento.
                                    if (isMet) currentChainResult = true;

                                    // Aplica o resultado da cadeia no total
                                    if (!currentChainResult) canUnlock = false;

                                    // Reset
                                    insideOrChain = false;
                                    currentChainResult = false;
                                }
                                else {
                                    // AND Padrão
                                    if (!isMet) canUnlock = false;
                                }
                            }
                        }

                        // Segurança: Se terminou o loop e ainda estávamos numa cadeia OR (último item tinha flag OR erroneamente)
                        if (insideOrChain) {
                            if (!currentChainResult) canUnlock = false;
                        }
                    }
                    node["canUnlock"] = canUnlock;

                    bool prevUnlocked = node.value("isUnlocked", false);
                    if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                        for (auto& rank : node["nextRanks"]) {
                            bool canUnlockRank = prevUnlocked;
                            if (canUnlockRank && rank.contains("requirements") && rank["requirements"].is_array()) {
                                bool currentChainResultRank = false;
                                bool insideOrChainRank = false;

                                for (auto& req : rank["requirements"]) {
                                    bool isMet = false;
                                    std::string reqType = req.value("type", "");

                                    if (reqType == "level") isMet = (currentTreeLevel >= req.value("value", 0));
                                    else if (reqType == "player_level") isMet = (realPlayerLevel >= req.value("value", 0));
                                    else if (reqType == "actor_level") isMet = (playerLevel >= req.value("value", 0));
                                    else if (reqType == "perk") isMet = unlockedNodesMap[req.value("value", "")];
                                    else if (reqType == "is_vampire") isMet = isVampire;
                                    else if (reqType == "is_werewolf") isMet = isWerewolf;
                                    else if (reqType == "any_skill") isMet = (allSkillLevelsMap[req.value("target", "")] >= req.value("value", 0));
                                    else if (reqType == "spells_known") isMet = (spellsKnownBySchool[req.value("target", "")] >= req.value("value", 0));
                                    else if (reqType == "kills") isMet = (totalKills >= req.value("value", 0));
                                    else if (reqType == "quest_completed") {
                                        RE::FormID questID = ParseFormIDString(req.value("value", ""));
                                        if (questID != 0) {
                                            auto quest = RE::TESForm::LookupByID<RE::TESQuest>(questID);
                                            if (quest) isMet = quest->IsCompleted();
                                        }
                                    }
                                    else if (reqType == "spell") {
                                        RE::FormID spellID = ParseFormIDString(req.value("value", ""));
                                        if (spellID != 0) {
                                            auto spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
                                            if (spell) isMet = actor->HasSpell(spell);
                                        }
                                    }
                                    else if (reqType == "location_discovered") {
                                        RE::FormID locID = ParseFormIDString(req.value("value", ""));
                                        if (locID != 0) {
                                            auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                                            if (loc) isMet = IsLocationDiscovered(loc);
                                        }
                                    }
                                    else if (reqType == "location_cleared") {
                                        RE::FormID locID = ParseFormIDString(req.value("value", ""));
                                        if (locID != 0) {
                                            auto loc = RE::TESForm::LookupByID<RE::BGSLocation>(locID);
                                            if (loc) isMet = loc->IsCleared();
                                        }
                                    }
                                    else if (reqType == "faction") {
                                        RE::FormID factID = ParseFormIDString(req.value("value", ""));
                                        if (factID != 0) {
                                            auto fact = RE::TESForm::LookupByID<RE::TESFaction>(factID);
                                            if (fact) isMet = actor->IsInFaction(fact);
                                        }
                                    }
                                    else if (reqType == "book_read") {
                                        RE::FormID bookID = ParseFormIDString(req.value("value", ""));
                                        if (bookID != 0) {
                                            auto book = RE::TESForm::LookupByID<RE::TESObjectBOOK>(bookID);
                                            if (book) isMet = book->IsRead();
                                        }
                                    }
                                    else if (reqType == "shout") {
                                        RE::FormID shoutID = ParseFormIDString(req.value("value", ""));
                                        if (shoutID != 0) {
                                            auto shout = RE::TESForm::LookupByID<RE::TESShout>(shoutID);
                                            if (shout) isMet = actor->HasShout(shout);
                                        }
                                    }
                                    else isMet = true;

                                    req["isMet"] = isMet;

                                    // [CORREÇÃO AQUI] Avaliando efetivamente se tem flag de "isOr"
                                    bool isOrLink = req.value("isOr", false);

                                    if (isOrLink) {
                                        if (isMet) currentChainResultRank = true;
                                        insideOrChainRank = true;
                                    }
                                    else {
                                        if (insideOrChainRank) {
                                            if (isMet) currentChainResultRank = true;
                                            if (!currentChainResultRank) canUnlockRank = false;
                                            insideOrChainRank = false;
                                            currentChainResultRank = false;
                                        }
                                        else {
                                            if (!isMet) canUnlockRank = false;
                                        }
                                    }
                                }

                                // Fechamento de uma possível cadeia OR não processada
                                if (insideOrChainRank) {
                                    if (!currentChainResultRank) canUnlockRank = false;
                                }
                            }
                            rank["canUnlock"] = canUnlockRank;
                            prevUnlocked = rank.value("isUnlocked", false);
                        }
                    }
                }
            }
        }

        json settingsData = GetSettings();
        json rulesData = GetLevelRules();
        json uiSettingsData = GetUISettings();
        std::string currentLangCode = uiSettingsData.value("language", "NSM_Language");
        json currentLangData = GetLocalizationContent();
        json fallbackLangData = json::object();


        json availablePerks = json::array();
        for (const auto& perk : Manager::GetSingleton()->GetList("Perk")) {
            uint32_t localID = (perk.formID & 0xFF000000) == 0xFE000000 ? (perk.formID & 0xFFF) : (perk.formID & 0xFFFFFF);


            json reqs = json::array();
            auto perkPtr = RE::TESForm::LookupByID<RE::BGSPerk>(perk.formID);
            if (perkPtr) {
                reqs = GetPerkRequirements(perkPtr);
            }

            availablePerks.push_back({
                {"id", fmt::format("{}|{:X}", perk.pluginName, localID)},
                {"name", perk.GetDisplayName()},
                {"description", perk.description},
                {"nextPerk", perk.nextPerkId},
                {"requirements", reqs}
                });
        }

        json availableQuests = json::array();
        for (const auto& quest : Manager::GetSingleton()->GetList("Quest")) {
            uint32_t localID = (quest.formID & 0xFF000000) == 0xFE000000 ? (quest.formID & 0xFFF) : (quest.formID & 0xFFFFFF);

            // Prioridade: FullName -> EditorID -> FormID
            std::string questName = quest.name;
            if (questName.empty()) questName = quest.editorID;
            if (questName.empty()) questName = fmt::format("{:X}", quest.formID);

            availableQuests.push_back({
                {"id", fmt::format("{}|{:X}", quest.pluginName, localID)},
                {"name", questName},
                {"editorId", quest.editorID}
                });
        }

        json availableSpells = json::array();
        for (const auto& spell : Manager::GetSingleton()->GetList("Spell")) {
            uint32_t localID = (spell.formID & 0xFF000000) == 0xFE000000 ? (spell.formID & 0xFFF) : (spell.formID & 0xFFFFFF);

            // Prioridade: Nome -> EditorID -> FormID
            std::string spellName = spell.name;
            if (spellName.empty()) spellName = spell.editorID;
            if (spellName.empty()) spellName = fmt::format("{:X}", spell.formID);

            availableSpells.push_back({
                {"id", fmt::format("{}|{:X}", spell.pluginName, localID)},
                {"name", spellName},
                {"editorId", spell.editorID}
                });
        }

        json availableLocations = json::array();
        for (const auto& loc : Manager::GetSingleton()->GetList("Location")) {
            uint32_t localID = (loc.formID & 0xFF000000) == 0xFE000000 ? (loc.formID & 0xFFF) : (loc.formID & 0xFFFFFF);

            // Prioridade: Nome -> EditorID -> FormID
            std::string locName = loc.name;
            if (locName.empty()) locName = loc.editorID;
            if (locName.empty()) locName = fmt::format("{:X}", loc.formID);

            availableLocations.push_back({
                {"id", fmt::format("{}|{:X}", loc.pluginName, localID)},
                {"name", locName},
                {"editorId", loc.editorID}
                });
        }

        json availableFactions = json::array();
        for (const auto& faction : Manager::GetSingleton()->GetList("Faction")) {
            uint32_t localID = (faction.formID & 0xFF000000) == 0xFE000000 ? (faction.formID & 0xFFF) : (faction.formID & 0xFFFFFF);

            // Prioridade: Nome -> EditorID -> FormID
            std::string factionName = faction.name;
            if (factionName.empty()) factionName = faction.editorID;
            if (factionName.empty()) factionName = fmt::format("{:X}", faction.formID);

            availableFactions.push_back({
                {"id", fmt::format("{}|{:X}", faction.pluginName, localID)},
                {"name", factionName},
                {"editorId", faction.editorID}
                });
        }

        json availableBooks = json::array();
        for (const auto& book : Manager::GetSingleton()->GetList("Book")) {
            uint32_t localID = (book.formID & 0xFF000000) == 0xFE000000 ? (book.formID & 0xFFF) : (book.formID & 0xFFFFFF);

            // Prioridade: Nome -> EditorID -> FormID
            std::string bookName = book.name;
            if (bookName.empty()) bookName = book.editorID;
            if (bookName.empty()) bookName = fmt::format("{:X}", book.formID);

            availableBooks.push_back({
                {"id", fmt::format("{}|{:X}", book.pluginName, localID)},
                {"name", bookName},
                {"editorId", book.editorID}
                });
        }

        json availableShouts = json::array();
        for (const auto& shout : Manager::GetSingleton()->GetList("Shout")) {
            uint32_t localID = (shout.formID & 0xFF000000) == 0xFE000000 ? (shout.formID & 0xFFF) : (shout.formID & 0xFFFFFF);

            std::string shoutName = shout.name;
            if (shoutName.empty()) shoutName = shout.editorID;
            if (shoutName.empty()) shoutName = fmt::format("{:X}", shout.formID);

            availableShouts.push_back({
                {"id", fmt::format("{}|{:X}", shout.pluginName, localID)},
                {"name", shoutName},
                {"editorId", shout.editorID}
                });
        }

        json availableGlobals = json::array();
        for (const auto& glob : Manager::GetSingleton()->GetList("Global")) {
            uint32_t localID = (glob.formID & 0xFF000000) == 0xFE000000 ? (glob.formID & 0xFFF) : (glob.formID & 0xFFFFFF);

            std::string globName = glob.name;
            if (globName.empty()) globName = glob.editorID;
            if (globName.empty()) globName = fmt::format("{:X}", glob.formID);

            availableGlobals.push_back({
                {"id", fmt::format("{}|{:X}", glob.pluginName, localID)},
                {"name", globName},
                {"editorId", glob.editorID}
                });
        }

        json availableReqs = json::array({
            //{{"id", "level"}, {"name", "Skill Level (Atual)"}},
            {{"id", "player_level"}, {"name", "Player Level"}},
            {{"id", "actor_level"}, {"name", "Selected Actor Level"}},
            {{"id", "perk"}, {"name", "Has Perk"}, {"isForm", true}},
            {{"id", "quest_completed"}, {"name", "Quest Completed"}, {"isForm", true}}, 
            {{"id", "location_discovered"}, {"name", "Location Discovered"}, {"isForm", true}},
            {{"id", "location_cleared"}, {"name", "Location Cleared"}, {"isForm", true}},
            {{"id", "faction"}, {"name", "In Faction"}, {"isForm", true}},
            {{"id", "book_read"}, {"name", "Book Read"}, {"isForm", true}},
            {{"id", "shout"}, {"name", "Has Shout"}, {"isForm", true}},
            {{"id", "spell"}, {"name", "Has Spell"}, {"isForm", true}},
            {{"id", "is_vampire"}, {"name", "Must be Vampire"}},
            {{"id", "is_werewolf"}, {"name", "Must be Werewolf"}},
            {{"id", "any_skill"}, {"name", "Target Skill Level"}},
            {{"id", "spells_known"}, {"name", "Spells Known (School)"}},
            //{{"id", "kills"}, {"name", "Total Kills"}},
            //{{"id", "item"}, {"name", "Has Item (FormID)"}},
            //{{"id", "global"}, {"name", "Global Variable"}}
            });

        std::vector<std::string> langs = GetAvailableLanguages();
        json formLists = json::object();
        formLists["perk"] = availablePerks;
        formLists["quest_completed"] = availableQuests;
        formLists["spell"] = availableSpells;
        formLists["location_discovered"] = availableLocations;
        formLists["location_cleared"] = availableLocations;
        formLists["faction"] = availableFactions;
        formLists["book_read"] = availableBooks;
        formLists["shout"] = availableShouts;
        formLists["global"] = availableGlobals;

        json availableActors = json::array();
        for (auto selectableActor : GetSelectableActors()) {
            if (!selectableActor) continue;
            mgr->EnsureActorProgress(selectableActor);
            std::string selectableRace = "Unknown";
            if (auto race = selectableActor->GetRace()) {
                selectableRace = race->GetFullName();
            }
            auto summary = SnapshotService::BuildActorSummary(selectableActor);
            summary["race"] = selectableRace;
            summary["pendingLevelUps"] =
                mgr->GetPendingLevelUps(selectableActor);
            availableActors.push_back(std::move(summary));
        }

        const int maxResets =
            currentEffSettings.value("maxResetsPerActor", -1);
        for (auto& tree : allTrees) {
            tree["resetPreview"] = ResetService::Preview(
                actor,
                ResetService::PerksInTree(tree),
                maxResets);
        }

        json finalResponse = {
            {"player", playerData},
            {"actors", availableActors},
            {"selectedActorId", ActorRuntimeKey(actor)},
            {"trees", allTrees},
            {"settings", settingsData},
            {"rules", rulesData},
            {"ruleValidation", RequirementService::ValidateRules(rulesData)},
            {"uiSettings", uiSettingsData},
            {"formLists", formLists},
            {"availableRequirements", availableReqs},
            {"availableLanguages", langs},
            {"activeTranslation", currentLangData},
            {"fallbackTranslation", fallbackLangData},
            {"customResources", customResources}
            
        };

        return finalResponse.dump(-1, ' ', true, json::error_handler_t::replace);

    }
    catch (const std::exception& e) {
        logger::error("ERRO CRÍTICO aqui o GetPlayerSkillsJSON: {}", e.what());
        return "{\"player\":null, \"trees\":[]}";
    }
}




void Prisma::Install() {
    logger::debug("Tentando instalar API do PrismaUI...");
    PrismaUI = reinterpret_cast<PRISMA_UI_API::IVPrismaUI1*>(PRISMA_UI_API::RequestPluginAPI());

    if (PrismaUI) {
        logger::debug("API do PrismaUI carregada com sucesso.");
    }
    else {
        logger::error("FALHA ao carregar API do PrismaUI. O arquivo PrismaUI.dll esta instalado?");
    }
}

void Prisma::SendUpdateToUI() {
    if (!PrismaUI || !Prisma::createdView) return;
	logger::debug("Enviando atualização de dados para a UI...");
    std::string jsonStr = GetPlayerSkillsJSON();

    // Se o json vier vazio ou sem player, enviamos mesmo assim para a UI resetar
    std::string script = "window.dispatchEvent(new CustomEvent('updateSkills', { detail: " + jsonStr + " }));";
    PrismaUI->Invoke(view, script.c_str());

    if (jsonStr == "{\"player\":null, \"trees\":[]}") {
        logger::debug("Dados de reset enviados para a UI (Player Null).");
    }
    else {
        logger::debug("Dados atualizados enviados para a UI.");
    }
}
// Resgata o código (Chamado pela UI)
static void RedeemCodeFromUI(const char* args) {
    if (!args) return;
    try {
        std::string inputCode(args);
        json settings = GetSettings();
        bool updated = false;
        auto actor = GetSelectedActor();
        if (!actor) return;

        for (auto& codeObj : settings["codes"]) {
            if (codeObj.value("code", "") == inputCode) {
                int maxUses = codeObj.value("maxUses", -1);
                int currentUses = codeObj.value("currentUses", 0);

                if (maxUses == -1 || currentUses < maxUses) {
                    codeObj["currentUses"] = currentUses + 1;
                    updated = true;

                    if (codeObj.contains("rewards")) {
                        auto rw = codeObj["rewards"];
                        const auto effective =
                            GetEffectiveSettings(actor->GetLevel(), actor);
                        if (rw.contains("perkPoints")) {
                            Manager::GetSingleton()->ModActorPerkPoints(
                                actor,
                                rw.value("perkPoints", 0),
                                effective.value("maxPerkPoints", 255));
                        }
                        auto owner = actor->AsActorValueOwner();
                        if (owner && rw.contains("health")) {
                            owner->ModBaseActorValue(
                                RE::ActorValue::kHealth,
                                rw.value("health", 0.0f));
                        }
                        if (owner && rw.contains("magicka")) {
                            owner->ModBaseActorValue(
                                RE::ActorValue::kMagicka,
                                rw.value("magicka", 0.0f));
                        }
                        if (owner && rw.contains("stamina")) {
                            owner->ModBaseActorValue(
                                RE::ActorValue::kStamina,
                                rw.value("stamina", 0.0f));
                        }
                    }
                    logger::info(
                        "Codigo '{}' resgatado para {} ({:08X}).",
                        inputCode,
                        actor->GetName(),
                        actor->GetFormID());
                }
                break;
            }
        }

        if (updated) {
            logger::debug("[DEBUG] Chamando SendUpdateToUI via RedeemCodeFromUI");
            SaveSettingsToFile(settings);
            Prisma::SendUpdateToUI();
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro no RedeemCode: {}", e.what());
    }
}



static RE::Actor* ResolveSelectableActorFromPayload(const json& payload) {
    std::string actorIdStr = payload.value("actorId", "");
    if (actorIdStr.empty()) return GetSelectedActor();

    RE::FormID actorId = 0;
    try {
        actorId = static_cast<RE::FormID>(std::stoul(actorIdStr, nullptr, 16));
    }
    catch (...) {
        return nullptr;
    }

    for (auto actor : GetSelectableActors()) {
        if (actor && actor->GetFormID() == actorId) return actor;
    }
    return nullptr;
}

static void SelectActorFromUI(const char* args) {
    if (!args) return;
    try {
        json payload = json::parse(args);
        auto actor = ResolveSelectableActorFromPayload(payload);
        if (!actor) return;
        g_selectedActorID = actor->GetFormID();
        Manager::GetSingleton()->EnsureActorProgress(actor);
        Prisma::SendUpdateToUI();
    }
    catch (const std::exception& e) {
        logger::warn("Falha ao selecionar ator: {}", e.what());
    }
}

static bool ResolvePurchaseDefinition(
    const std::string& perkIDStr,
    int& cost,
    json& customCosts)
{
    for (const auto& tree : GetLoadedSkillTreeConfigs()) {
        if (!tree.contains("nodes") || !tree["nodes"].is_array()) continue;
        for (const auto& node : tree["nodes"]) {
            if (node.value("perk", "") == perkIDStr) {
                cost = std::max(0, node.value("perkCost", 1));
                customCosts = node.value("customCosts", json::array());
                return true;
            }
            if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                for (const auto& rank : node["nextRanks"]) {
                    if (rank.value("perk", "") == perkIDStr) {
                        cost = std::max(0, rank.value("perkCost", 1));
                        customCosts = rank.value("customCosts", json::array());
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// Desbloqueio actor-aware com custos revalidados no backend.
static void UnlockPerkFromUI(const char* args)
{
    if (!args) return;
    try {
        const json payload = json::parse(args);
        const std::string perkIDStr = payload.value("id", "");
        int cost = 0;
        json customCosts = json::array();
        if (!ResolvePurchaseDefinition(perkIDStr, cost, customCosts)) return;

        const RE::FormID perkID = ParseFormIDString(perkIDStr);
        auto actor = ResolveSelectableActorFromPayload(payload);
        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
        if (!actor || !perk) return;

        const auto snapshot = json::parse(GetPlayerSkillsJSON());
        const auto validation =
            RequirementService::ValidatePurchaseSnapshot(snapshot, perkIDStr);
        if (!validation.allowed) {
            logger::warn(
                "[Economy] Purchase rejected actor={:08X} perk={} reason={}",
                actor->GetFormID(),
                perkIDStr,
                validation.reason);
            return;
        }

        const auto result = PurchaseService::Purchase(
            actor,
            perk,
            cost,
            customCosts,
            GetCustomResources());
        if (!result.success) {
            logger::warn(
                "[Economy] Purchase failed actor={:08X} perk={} reason={}",
                actor->GetFormID(),
                perkIDStr,
                result.reason);
            return;
        }

        if (auto dispatcher = SKSE::GetModCallbackEventSource()) {
            SKSE::ModCallbackEvent modEvent{
                RE::BSFixedString("NSM_PerkAquired"),
                RE::BSFixedString(perkIDStr),
                static_cast<float>(perkID),
                actor
            };
            dispatcher->SendEvent(&modEvent);
        }
        Prisma::SendUpdateToUI();
    }
    catch (const std::exception& e) {
        logger::error("Erro ao desbloquear perk: {}", e.what());
    }
}

static void ChooseAttributeFromUI(const char* args) {
    if (!args) return;
    try {
        json payload = json::parse(args);
        json levelUpsArray = payload.value("levelUps", json::array());
        json skillsMap = payload.value("skills", json::object());

        auto actor = ResolveSelectableActorFromPayload(payload);
        if (!actor) return;
        auto mgr = Manager::GetSingleton();
        const int requestedLevelUps = static_cast<int>(levelUpsArray.size());
        const int pendingBefore = mgr->GetPendingLevelUps(actor);
        if (requestedLevelUps <= 0 || requestedLevelUps > pendingBefore) return;

        const int firstRewardLevel = actor->IsPlayerRef() ?
            static_cast<int>(actor->GetLevel()) + 1 :
            std::max(1, static_cast<int>(actor->GetLevel()) - pendingBefore + 1);
        int allowedSkillPoints = 0;
        int allowedSpend = 0;
        for (int i = 0; i < requestedLevelUps; ++i) {
            auto effective = GetEffectiveSettings(firstRewardLevel + i, actor);
            allowedSkillPoints += std::max(0, effective.value("skillPointsPerLevel", 0));
            allowedSpend += std::max(0, effective.value("maxSkillPointsSpendablePerLevel", 0));
        }

        int requestedSkillPoints = 0;
        for (auto& [skillName, amountVal] : skillsMap.items()) {
            if (!amountVal.is_number_integer()) return;
            const int amount = amountVal.get<int>();
            if (amount < 0) return;
            requestedSkillPoints += amount;
        }
        if (requestedSkillPoints > std::min(allowedSkillPoints, allowedSpend)) return;

        int totalExtraPerks = 0;
        int maxPerkPoints = GetEffectiveSettings(
            firstRewardLevel + requestedLevelUps - 1,
            actor).value("maxPerkPoints", 255);
        logger::info(
            "[Prisma] Processando {} level ups para {} ({:08X}).",
            levelUpsArray.size(),
            actor->GetName(),
            actor->GetFormID());

        // 1. Processa Atributos para cada nível pendente escolhido
        int levelIndex = 0;
        for (auto& lvlUp : levelUpsArray) {
            int lvl = firstRewardLevel + levelIndex++;
            std::string attribute = lvlUp.value("attribute", "");
            if (attribute != "Health" && attribute != "Magicka" && attribute != "Stamina") return;
            auto playerBase = actor->GetActorBase();
            if (actor->IsPlayerRef() && playerBase) {
                playerBase->actorData.level += 1;
                playerBase->AddChange(RE::TESNPC::ChangeFlags::kBaseData);
                logger::info("Player subiu de nivel! Novo nivel: {}", playerBase->actorData.level);
            }
            // Pega as configurações EFETIVAS baseadas na regra daquele respectivo nível
            json effSettings = GetEffectiveSettings(lvl, actor);
            float healthInc = effSettings.value("healthIncrease", 10.0f);
            float magickaInc = effSettings.value("magickaIncrease", 10.0f);
            float staminaInc = effSettings.value("staminaIncrease", 10.0f);
            int perksPerLevel = effSettings.value("perksPerLevel", 1);
            bool refillAttributes = effSettings.value("refillAttributesOnLevelUp", false);
            maxPerkPoints = effSettings.value("maxPerkPoints", maxPerkPoints);

            if (attribute == "Health") actor->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kHealth, healthInc);
            else if (attribute == "Magicka") actor->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kMagicka, magickaInc);
            else if (attribute == "Stamina") actor->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kStamina, staminaInc);

            float cwInc = effSettings.value("carryWeightIncrease", 0.0f);
            std::string cwMethod = effSettings.value("carryWeightMethod", "none");
            bool giveCW = false;

            if (cwMethod == "auto") {
                giveCW = true;
            }
            else if (cwMethod == "linked") {
                auto linkedAttrs = effSettings.value("carryWeightLinkedAttributes", json::array());
                for (auto& attr : linkedAttrs) {
                    if (attr == attribute) {
                        giveCW = true;
                        break;
                    }
                }
            }

            if (giveCW && cwInc > 0.0f) {
                actor->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent, RE::ActorValue::kCarryWeight, cwInc);
                logger::info("Carry Weight incrementado em {} para o Nivel {}", cwInc, lvl);
            }

            if (refillAttributes) {
                actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, 999999.0f);
                actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, 999999.0f);
                actor->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kStamina, 999999.0f);
            }

            for (const auto& reward :
                effSettings.value("resourceRewards", json::array())) {
                std::string error;
                if (!ResourceService::Credit(
                    actor,
                    reward.value("resourceId", ""),
                    reward.value("amount", 0.0f),
                    GetCustomResources(),
                    error)) {
                    logger::warn(
                        "[Economy] Level resource reward failed actor={:08X} "
                        "level={} reason={}",
                        actor->GetFormID(),
                        lvl,
                        error);
                }
            }

            totalExtraPerks += perksPerLevel;
        }

        // 2. Aplica as Skills Escolhidas (Agrupadas)
        for (auto& [skillName, amountVal] : skillsMap.items()) {
            int amount = amountVal.get<int>();
            if (amount > 0) {
                RE::ActorValue av = GetActorValueFromName(skillName);
                if (av != RE::ActorValue::kNone) {
                    actor->AsActorValueOwner()->ModBaseActorValue(av, static_cast<float>(amount));
                }
                else {
                    int currentCustomLevel = mgr->GetCustomSkillLevel(actor, skillName);
                    mgr->SetCustomSkillLevel(actor, skillName, currentCustomLevel + amount);
                }
            }
        }

        // 3. Compensa os Perk Points finais (soma/subtrai as regras efetivas dos níveis agrupados)
        if (totalExtraPerks != 0) {
            mgr->ModActorPerkPoints(actor, totalExtraPerks, maxPerkPoints);
        }

        // Limpa as pendências já processadas
        mgr->ConsumePendingLevelUps(actor, requestedLevelUps);

        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (actor->IsPlayerRef() && msgQueue) {
            msgQueue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }

        Prisma::SendUpdateToUI();
        logger::info(
            "[Prisma] Level Up processado. Restam {} pendências para o ator.",
            mgr->GetPendingLevelUps(actor));
    }
    catch (const std::exception& e) {
        logger::error("Erro ao aplicar level up em lote: {}", e.what());
    }
}

static void SaveSettingsFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json newSettings = json::parse(jsonArgs);
        SaveSettingsToFile(newSettings);
        logger::info("Settings.json atualizado com sucesso pela UI (Disco e Cache).");
    }
    catch (const std::exception& e) {
        logger::error("Erro critico ao processar e salvar Settings da UI: {}", e.what());
    }
}

// Função para receber o JSON da UI, limpar os dados do jogador e salvar nos arquivos
static void SaveSkillTreesFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;

    try {
        json incomingTrees = json::parse(jsonArgs);

        if (!incomingTrees.is_array()) {
            logger::error("SaveSkillTreesFromUI: O JSON recebido nao e um array.");
            return;
        }

        std::string skillTreesDir = std::string("Data\\PrismaUI\\views\\") + PRODUCT_NAME + "\\Skill Trees";
        if (!std::filesystem::exists(skillTreesDir)) {
            std::filesystem::create_directories(skillTreesDir);
        }

        for (auto& tree : incomingTrees) {
            std::string treeName = tree.value("name", "Unknown");
            if (treeName == "Unknown") continue;

            tree.erase("currentLevel");
            tree.erase("currentProgress");
            tree.erase("cap");
            tree.erase("resetPreview");
            if (tree.contains("nodes") && tree["nodes"].is_array()) {
                for (auto& node : tree["nodes"]) {
                    node.erase("isUnlocked");
                    node.erase("canUnlock");
                    node.erase("ownershipSource");
                    if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                        for (auto& rank : node["nextRanks"]) {
                            rank.erase("isUnlocked");
                            rank.erase("canUnlock");
                            rank.erase("ownershipSource");
                            if (rank.contains("requirements") &&
                                rank["requirements"].is_array()) {
                                for (auto& requirement : rank["requirements"]) {
                                    requirement.erase("isMet");
                                }
                            }
                        }
                    }
                    if (node.contains("requirements") && node["requirements"].is_array()) {
                        for (auto& req : node["requirements"]) {
                            req.erase("isMet");
                        }
                    }
                }
            }

            std::string defaultPath = skillTreesDir + "\\" + treeName + ".json";
            std::string filePath = tree.value("_originalFilePath", defaultPath);
            tree.erase("_originalFilePath");
            std::ofstream file(filePath);

            if (file.is_open()) {
                file << tree.dump(4);
                file.close();

                // Atualiza o cache de imediato usando o novo estado que acabou de ser salvo
                std::error_code ec;
                auto writeTime = std::filesystem::last_write_time(filePath, ec);
                tree["_originalFilePath"] = filePath; // Devolve o path para garantir coerência no cache
                g_treeCache[filePath] = { tree, writeTime };

                logger::info("Skill tree '{}' atualizada e salva com sucesso em {}", treeName, filePath);
            }
            else {
                logger::error("Falha ao abrir o arquivo para salvar: {}", filePath);
            }
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro critico ao processar e salvar skill trees da UI: {}", e.what());
    }
}

static void LegendarySkillFromUI(const char* args) {
    if (!args) return;
    try {
        json payload = json::parse(args);
        std::string treeName = payload.value("treeName", "");

        auto actor = ResolveSelectableActorFromPayload(payload);
        if (!actor) return;

        logger::debug("[Legendary] Iniciando reset lendario para a arvore: {}", treeName);

        // Carrega a configuração da árvore específica
        json allTrees = GetLoadedSkillTreeConfigs();
        json targetTree;
        bool found = false;

        for (const auto& t : allTrees) {
            if (t.value("name", "") == treeName) {
                targetTree = t;
                found = true;
                break;
            }
        }

        if (!found) {
            logger::warn("[Legendary] Arvore {} nao encontrada.", treeName);
            return;
        }

        const auto effective = GetEffectiveSettings(actor->GetLevel(), actor);
        const auto resetResult = ResetService::Execute(
            actor,
            ResetService::PerksInTree(targetTree),
            GetCustomResources(),
            effective.value("maxPerkPoints", 255),
            effective.value("maxResetsPerActor", -1),
            true);
        if (!resetResult.value("success", false)) {
            logger::warn(
                "[Legendary] Reset rejeitado para '{}' reason={}",
                treeName,
                resetResult.value("reason", "unknown"));
            return;
        }
        logger::info(
            "[Legendary] Skill '{}': {} perks removidos e {} pontos devolvidos.",
            treeName,
            resetResult.value("removed", 0),
            resetResult.value("refundedPerkPoints", 0));

        // 3. Resetar Nível da Skill
        int initialLevel = targetTree.value("initialLevel", 15);
        bool isVanilla = targetTree.value("isVanilla", false);

        if (isVanilla) {
            RE::ActorValue av = GetActorValueFromName(treeName);
            if (av != RE::ActorValue::kNone) {
                // Define o valor base para o inicial
                actor->AsActorValueOwner()->SetBaseActorValue(av, static_cast<float>(initialLevel));
                logger::debug("[Legendary] Nivel da skill vanilla {} resetado para {}", treeName, initialLevel);
            }
        }
        else {
            auto mgr = Manager::GetSingleton();
            mgr->SetCustomSkillLevel(actor, treeName, initialLevel);
            mgr->SetCustomSkillXP(actor, treeName, 0.0f);
            logger::debug("[Legendary] Nivel da custom skill {} resetado para {}", treeName, initialLevel);
        }

        SKSE::GetTaskInterface()->AddUITask([]() {
            Prisma::SendUpdateToUI();
        });
    }
    catch (const std::exception& e) {
        logger::error("Erro em LegendarySkillFromUI: {}", e.what());
    }
}

// =========================================================================================
// CALLBACK: RESET ALL PERKS (Apenas Perks, mantem niveis)
// =========================================================================================
static void ResetAllPerksFromUI(const char* args) {
    try {
        json payload = args && *args ? json::parse(args) : json::object();
        auto actor = ResolveSelectableActorFromPayload(payload);
        if (!actor) return;

        logger::debug("[ResetAll] Iniciando remocao de TODOS os perks...");

        const auto effective = GetEffectiveSettings(actor->GetLevel(), actor);
        const auto resetResult = ResetService::Execute(
            actor,
            {},
            GetCustomResources(),
            effective.value("maxPerkPoints", 255),
            effective.value("maxResetsPerActor", -1),
            true);
        if (!resetResult.value("success", false)) {
            logger::warn(
                "[ResetAll] Reset rejeitado reason={}",
                resetResult.value("reason", "unknown"));
            return;
        }

        logger::info(
            "[ResetAll] {} perks removidos, {} pontos devolvidos.",
            resetResult.value("removed", 0),
            resetResult.value("refundedPerkPoints", 0));
        SKSE::GetTaskInterface()->AddUITask([]() {
            Prisma::SendUpdateToUI();
        });
    }
    catch (const std::exception& e) {
        logger::error("Erro em ResetAllPerksFromUI: {}", e.what());
    }
}
static bool isInspectorVisible = false;
static bool hasInspectorInitialized = false;
void Prisma::Show() {
    if (!PrismaUI) {
        logger::error("Impossivel executar Show(): PrismaUI e nulo!");
        return;
    }

    if (isVisible) return;

    if (!createdView) {
        logger::debug("Criando nova View para o Prisma...");
        createdView = true;

#ifdef DEV_SERVER
        constexpr const char* path = "http://localhost:5173";
#else
        constexpr const char* path = PRODUCT_NAME "/index.html"; // Verifique se o caminho esta correto
#endif
        logger::debug("Caminho da UI: {}", path);

        view = PrismaUI->CreateView(path, [](PrismaView currentView) -> void {
            logger::debug("DOM Pronto. Configurando interface...");
            PrismaUI->RegisterJSListener(currentView, "toggleInspector", [](const char*) {
                // 1. Se ainda não foi criado, cria o Inspector View
                if (!hasInspectorInitialized) {

                    // CORREÇÃO: Removemos o callback. A função só aceita a 'view'.
                    PrismaUI->CreateInspectorView(view);

                    logger::info("Inspector View criado.");

                    // Executamos a configuração de limites imediatamente após criar
                    // Nota: Verifique se sua API espera Pixels ou Porcentagem.
                    // O cabeçalho pede 'unsigned int' para largura/altura, o que geralmente indica Pixels.
                    // Se a janela ficar muito pequena, mude 50/100 para valores de pixel (ex: 960, 1080).
                    PrismaUI->SetInspectorBounds(view, 50, 0, 800, 800);

                    hasInspectorInitialized = true;
                }

                // 2. Alterna a visibilidade
                isInspectorVisible = !isInspectorVisible;
                // A função da API para visibilidade é SetInspectorVisibility(view, bool)
                PrismaUI->SetInspectorVisibility(view, isInspectorVisible);

                logger::debug("Inspector visibility set to: {}", isInspectorVisible);
                });
            // Registramos os listeners primeiro
            PrismaUI->RegisterJSListener(currentView, "hideWindow", [](const char*) {
                logger::debug("Recebida requisicao para fechar o menu Prisma.");
				Prisma::Hide();
                });
            PrismaUI->RegisterJSListener(currentView, "exportTree", [](const char* args) { ExportTreeFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "requestLocalization", [](const char* args) {
                RequestLocalizationFromUI(args);
                });
            PrismaUI->RegisterJSListener(currentView, "playUISound", [](const char* args) {
                if (args) PlayUISound(args);
                });
            PrismaUI->RegisterJSListener(currentView, "legendarySkill", [](const char* args) { LegendarySkillFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "resetAllPerks", [](const char* args) { ResetAllPerksFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "selectActor", [](const char* args) { SelectActorFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "requestSkills", [](const char*) {
                logger::debug("[DEBUG] Chamando SendUpdateToUI via JSListener (requestSkills)");
                SendUpdateToUI();
                });
            PrismaUI->RegisterJSListener(currentView, "saveSkillTrees", [](const char* args) {
                SaveSkillTreesFromUI(args);
                logger::debug("[DEBUG] Chamando SendUpdateToUI via JSListener (saveSkillTrees)");
                SendUpdateToUI();
                });
            PrismaUI->RegisterJSListener(currentView, "saveRules", [](const char* args) { SaveRulesFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "createTree", [](const char* args) {
                try {
                    auto j = json::parse(args);
                    std::string newName = j["name"];

                    std::filesystem::path baseDir = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\" + newName;
                    std::filesystem::create_directories(baseDir);

                    json newTree = {
                        {"name", newName},
                        {"displayName", newName},
                        {"isVanilla", false},
                        {"initialLevel", 15},
                        {"advancesPlayerLevel", true},
                        {"category", "Custom"},
                        {"color", "#ffffff"},
                        {"bgPath", ""},
                        {"iconPath", ""},
                        {"selectionIconPath", ""},
                        {"experienceFormula", {
                            {"useMult", 1.0},
                            {"useOffset", 0.0},
                            {"improveMult", 1.0},
                            {"improveOffset", 0.0}
                        }},
                        {"treeRequirements", json::array()},
                        {"nodes", json::array()}
                    };

                    std::ofstream file(baseDir / (newName + ".json"));
                    file << newTree.dump(4);
                    file.close();

                    // Força o C++ a reler a pasta e enviar pro UI
                    Manager::GetSingleton()->LoadCustomSkills();
                    logger::debug("[DEBUG] Chamando SendUpdateToUI via JSListener (createTree)");
                    SendUpdateToUI();
                }
                catch (const std::exception& e) {
                    logger::error("Erro ao criar nova árvore: {}", e.what());
                }
                });
            PrismaUI->RegisterJSListener(currentView, "deleteTree", [](const char* args) {
                try {
                    auto j = json::parse(args);
                    std::string treeName = j.value("name", "");
                    if (!treeName.empty()) {
                        std::string treePath = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees\\" + treeName + ".json";
                        if (std::filesystem::exists(treePath)) {
                            std::filesystem::remove(treePath);
                            logger::info("Arvore deletada com sucesso: {}", treeName);

                            auto mgr = Manager::GetSingleton();
                            mgr->RemoveCustomSkillState(treeName);

                            g_treeCache.erase(treePath);

                            SendUpdateToUI();
                        }
                    }
                }
                catch (const std::exception& e) {
                    logger::error("Erro ao deletar arvore: {}", e.what());
                }
                });
            PrismaUI->RegisterJSListener(currentView, "requestFileList", [](const char* args) {
                try {
                    auto j = json::parse(args);
                    std::string reqPath = j.value("path", "");
                    std::string field = j.value("field", "");

                    // Resolve a pasta base do mod e concatena com o caminho requisitado
                    std::filesystem::path baseDir = "Data\\PrismaUI\\views\\" PRODUCT_NAME;
                    std::filesystem::path targetDir = baseDir / reqPath;

                    json folders = json::array();
                    json files = json::array();

                    if (std::filesystem::exists(targetDir) && std::filesystem::is_directory(targetDir)) {
                        for (const auto& entry : std::filesystem::directory_iterator(targetDir)) {
                            std::string name = entry.path().filename().string();
                            if (entry.is_directory()) {
                                folders.push_back(name);
                            }
                            else {
                                // Filtra apenas imagens para o navegador UI
                                std::string ext = entry.path().extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".svg" || ext == ".webp") {
                                    files.push_back(name);
                                }
                            }
                        }
                    }

                    json res = {
                        {"currentPath", reqPath},
                        {"field", field},
                        {"folders", folders},
                        {"files", files}
                    };

                    std::string script = fmt::format("window.dispatchEvent(new CustomEvent('fileListResponse', {{detail: {}}}));", res.dump());
                    PrismaUI->Invoke(view, script.c_str());

                }
                catch (const std::exception& e) {
                    logger::error("Erro em requestFileList: {}", e.what());
                }
                });
            PrismaUI->RegisterJSListener(currentView, "unlockPerk", [](const char* args) { UnlockPerkFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "chooseAttribute", [](const char* args) { ChooseAttributeFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "redeemCode", [](const char* args) { RedeemCodeFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "saveResources", [](const char* args) { SaveResourcesFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "deleteResource", [](const char* args) { DeleteResourceFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "saveSettings", [](const char* args) { SaveSettingsFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "saveUISettings", [](const char* args) { SaveUISettingsFromUI(args); });
            SendUpdateToUI();
            PrismaUI->Focus(currentView, true);
            });
    }
    else {
        logger::debug("Reexibindo View existente.");
        PrismaUI->Show(view);
        SendUpdateToUI();
        PrismaUI->Focus(view, true);
    }

    //RE::UIBlurManager::GetSingleton()->IncrementBlurCount();
    isVisible = true;
    if (ShouldTriggerMouseMode()) {
        TriggerMouseModeEvent();
    }
   
}

void Prisma::TriggerExitAnimation() {
    if (PrismaUI && createdView && isVisible) {
        // Envia um evento para o frontend React executar a animação de saída
        PrismaUI->Invoke(view, "window.dispatchEvent(new CustomEvent('triggerExitAnimation'));");
    }
}

void Prisma::TriggerBack() {
    // Usamos as variáveis internas do seu Prisma.cpp para ter certeza que a view é válida
    if (PrismaUI && createdView && isVisible) {
        PrismaUI->Invoke(view, "window.dispatchEvent(new CustomEvent('HardwareBack'));");
    }
}

void Prisma::Hide() {
    if (!PrismaUI) return;

    if (createdView && isVisible) {
        logger::debug("Escondendo menu Prisma...");
        PrismaUI->Unfocus(view);
        PrismaUI->Hide(view);
       //RE::UIBlurManager::GetSingleton()->DecrementBlurCount();
        isVisible = false;
        auto ui = RE::UI::GetSingleton();
        if (ui) {
            auto focusMenu = ui->GetMenu("PrismaUI_FocusMenu");
            if (focusMenu) {
                focusMenu->menuFlags.reset(RE::UI_MENU_FLAGS::kFreezeFrameBackground);
            }
        }
        if (ShouldTriggerMouseMode()) {
            TriggerMouseModeEvent(true);
        }
        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (msgQueue) {
            msgQueue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            msgQueue->AddMessage(RE::StatsMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }
    }
}

bool Prisma::IsHidden() {
    return !isVisible;
}

void ApplyVanillaInitialLevels() {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    json settings = GetSettings();
    if (!settings["base"].value("applyVanillaInitialLevels", true)) {
        logger::info("ApplyVanillaInitialLevels esta desabilitado nas configuracoes. Ignorando.");
        return;
    }

    logger::info("Aplicando Niveis Iniciais para Skills Vanilla (New Game)...");

    // Carrega todas as configurações de árvores (Vanilla e Custom)
    json allTrees = GetLoadedSkillTreeConfigs();

    for (const auto& tree : allTrees) {
        // Verifica se é Vanilla
        if (tree.value("isVanilla", false)) {
            std::string name = tree.value("name", "");
            // Pega o initialLevel do JSON (Padrão 15 se não existir)
            int initialLevel = tree.value("initialLevel", 15);

            RE::ActorValue av = GetActorValueFromName(name);
            if (av != RE::ActorValue::kNone) {
                // Define o valor base do ActorValue para o nível configurado
                player->AsActorValueOwner()->SetBaseActorValue(av, static_cast<float>(initialLevel));
                logger::info("Skill Vanilla '{}' definida para o nivel inicial: {}", name, initialLevel);
            }
        }
    }
}





void Prisma::SetLevelUpMenuOpen(bool isOpen) {
    g_isLevelUpMenuOpen = isOpen;
}

bool Prisma::IsLevelUpMenuOpen() {
    return g_isLevelUpMenuOpen;
}
