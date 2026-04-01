#include "Prisma.h"
#include "Manager.h"

using json = nlohmann::json;

PRISMA_UI_API::IVPrismaUI1* PrismaUI = nullptr;
static PrismaView view;
static bool isVisible = false;

static std::map<std::string, json> localizationCache;
static std::vector<std::string> availableLanguagesCache;
// Variável de controle do menu de level up
static bool g_isLevelUpMenuOpen = false;

struct CachedTreeData {
    nlohmann::json data;
    std::filesystem::file_time_type lastWriteTime;
};
static std::unordered_map<std::string, CachedTreeData> g_treeCache;

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
    std::string illegalChars = "<>:\"/\\|?*";
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
    // Se já escaneamos a pasta antes, retorna a lista da memória
    if (!availableLanguagesCache.empty()) {
        return availableLanguagesCache;
    }

    std::vector<std::string> langs;
    langs.push_back("en"); // Padrão sempre presente

    std::filesystem::path locDir = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Localization";

    if (std::filesystem::exists(locDir) && std::filesystem::is_directory(locDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(locDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string stem = entry.path().stem().string();
                std::string stemLower = stem;
                std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(), ::tolower);

                if (stemLower != "en") {
                    langs.push_back(stem);
                }
            }
        }
    }

    // Salva no cache estático
    availableLanguagesCache = langs;
    return availableLanguagesCache;
}


// 2. Melhoria na leitura do conteúdo (com log de fallback)
json GetLocalizationContent(const std::string& langCode) {
    // 1. Verifica se já está na memória (Cache Hit)
    if (localizationCache.find(langCode) != localizationCache.end()) {
        return localizationCache[langCode];
    }

    // 2. Se não estiver, carrega do disco
    std::string fileName = langCode + ".json";
    std::filesystem::path locDir = "Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Localization";
    std::filesystem::path fullPath = locDir / fileName;

    json content = json::object();

    if (std::filesystem::exists(fullPath)) {
        std::ifstream file(fullPath);
        if (file.is_open()) {
            try {
                content = json::parse(file);
                // 3. Salva no cache para a próxima vez
                localizationCache[langCode] = content;
            }
            catch (const std::exception& e) {
                logger::error("Erro de parse no idioma {}: {}", langCode, e.what());
            }
        }
    }
    else {
        logger::warn("Arquivo de idioma nao encontrado: {}", fullPath.string());
    }

    return content;
}



// Callback chamado pela UI
static void RequestLocalizationFromUI(const char* args) {
    if (!args) return;
    std::string langCode = args; // O argumento é apenas a string "pt", "es", etc.

    // 1. Lê o JSON do disco
    json content = GetLocalizationContent(langCode);

    // 2. Monta a resposta
    json response;
    response["lang"] = langCode;
    response["data"] = content;

    // 3. Envia para a UI
    if (PrismaUI && Prisma::createdView) {
        std::string script = fmt::format("window.dispatchEvent(new CustomEvent('receiveLocalization', {{ detail: {} }}));", response.dump());
        PrismaUI->Invoke(view, script.c_str());
    }
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

    // 4. Acessa o Manager e atualiza se necessário
    auto mgr = Manager::GetSingleton();

    // Se a skill ainda não existe no map do manager, cria a entrada
    if (mgr->playerCustomSkills.find(skillId) == mgr->playerCustomSkills.end()) {
        mgr->playerCustomSkills[skillId].currentLevel = externalLevel;
        mgr->playerCustomSkills[skillId].currentXP = 0.0f;
        logger::info("Skill '{}' inicializada via Global Externa com Nivel: {}", skillId, externalLevel);
    }
    else {
        // Se já existe, atualiza apenas se o nível externo for maior (proteção contra regressão)
        if (externalLevel > mgr->playerCustomSkills[skillId].currentLevel) {
            logger::info("Sincronizando Nivel '{}': Prisma({}) -> Global({})",
                skillId, mgr->playerCustomSkills[skillId].currentLevel, externalLevel);

            mgr->playerCustomSkills[skillId].currentLevel = externalLevel;
        }
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

void ExportVanillaPerkTree(RE::ActorValue actorValue, const std::string& skillName, const std::string& categoryName, const std::string& iconName, const std::string& bgName) {
    std::string dirPath = "Data\\PrismaUI\\views\\SkillMenu\\Skill Trees";
    std::filesystem::create_directories(dirPath);
    std::string filePath = dirPath + "\\" + skillName + ".json";

    if (std::filesystem::exists(filePath)) return;

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
    std::filesystem::path rulesPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Rules.json");

    if (std::filesystem::exists(rulesPath)) {
        std::ifstream file(rulesPath);
        if (file.is_open()) {
            try {
                return json::parse(file);
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler rules.json: {}", e.what());
            }
        }
    }

    // Padrão se não existir: Regra nível 1
    // skillCap 100 é o padrão do Skyrim, mas você pode mudar aqui
    json defaultRules = json::array({
        });

    // Cria o arquivo se não existir
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(rulesPath);
    if (file.is_open()) file << defaultRules.dump(4);

    return defaultRules;
}

void SaveLevelRulesToFile(const json& rulesArr) {
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Rules.json");
    if (file.is_open()) {
        file << rulesArr.dump(4);
        logger::info("rules.json salvo com sucesso.");
    }
}

json GetSettings() {
    std::filesystem::path settingsPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Settings.json");

    // Nova estrutura padrão
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
            {"carryWeightLinkedAttributes", json::array({"Stamina"})}
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

                if (loadedSettings.contains("levelRules")) {
                    loadedSettings.erase("levelRules");
                }

                // Mescla os padrões
                for (auto& [key, value] : defaultSettings["base"].items()) {
                    if (!loadedSettings["base"].contains(key)) {
                        loadedSettings["base"][key] = value;
                    }
                }

                if (!loadedSettings.contains("categories")) loadedSettings["categories"] = defaultSettings["categories"];
                return loadedSettings;
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler Settings.json: {}", e.what());
            }
        }
    }

    // Se não existir ou deu erro, cria a pasta e o arquivo com os padrões
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(settingsPath);
    if (file.is_open()) file << defaultSettings.dump(4);

    return defaultSettings;
}

// Função que calcula os valores efetivos para um determinado nível
json GetEffectiveSettings(int targetLevel) {
    json fullSettings = GetSettings();
    json eff = fullSettings["base"];
    int hardCap = eff.value("skillCap", 100);
    // Carrega as regras do arquivo separado
    json rules = GetLevelRules();

    if (rules.is_array()) {
        std::vector<json> sortedRules;
        for (auto& r : rules) sortedRules.push_back(r);

        // Ordena por nível
        std::sort(sortedRules.begin(), sortedRules.end(), [](const json& a, const json& b) {
            return a.value("level", 0) < b.value("level", 0);
            });

        for (auto& r : sortedRules) {
            if (r.value("level", 0) <= targetLevel) {
                // Sobrescreve os valores da base
                for (auto& [key, val] : r.items()) {
                    if (key != "level") eff[key] = val;
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

static void SaveRulesFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json newRules = json::parse(jsonArgs);
        if (newRules.is_array()) {
            SaveLevelRulesToFile(newRules);
        }
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
}


json GetLoadedSkillTreeConfigs() {
    json trees = json::array();
    std::filesystem::path dir("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Skill Trees");

    if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
        // Vare a pasta raiz e subpastas buscando arquivos .json
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                std::string filePath = entry.path().string();
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
                            json tree = json::parse(file);

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
    std::filesystem::path settingsPath("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\uisettings.json");

    // Configurações padrão da UI
    json defaultUISettings = {
        {"language", "en"},
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
                // Mescla com os padrões para garantir que novas chaves existam
                for (auto& [key, value] : defaultUISettings.items()) {
                    if (!loadedSettings.contains(key)) {
                        loadedSettings[key] = value;
                    }
                }
                return loadedSettings;
            }
            catch (const std::exception& e) {
                logger::error("Erro ao ler uisettings.json: {}", e.what());
            }
        }
    }

    // Se não existir, cria
    std::filesystem::create_directories("Data\\PrismaUI\\views\\" PRODUCT_NAME);
    std::ofstream file(settingsPath);
    if (file.is_open()) file << defaultUISettings.dump(4);

    return defaultUISettings;
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
            logger::info("uisettings.json atualizado com sucesso.");
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro ao salvar uisettings.json: {}", e.what());
    }
}

std::string GetPlayerSkillsJSON() {
    try {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player || !player->Is3DLoaded()) return "{\"player\":null, \"trees\":[]}";

        // --- 1. DADOS BÁSICOS DO JOGADOR (HEADER) ---
        std::string playerName = player->GetName();
        auto avOwner = player->AsActorValueOwner();

        float hpCur = avOwner->GetActorValue(RE::ActorValue::kHealth);
        float hpMax = avOwner->GetBaseActorValue(RE::ActorValue::kHealth);
        float mpCur = avOwner->GetActorValue(RE::ActorValue::kMagicka);
        float mpMax = avOwner->GetBaseActorValue(RE::ActorValue::kMagicka);
        float spCur = avOwner->GetActorValue(RE::ActorValue::kStamina);
        float spMax = avOwner->GetBaseActorValue(RE::ActorValue::kStamina);
        uint8_t perkPoints = player->GetPlayerRuntimeData().perkCount;
        int playerLevel = player->GetLevel();
        int dragonSouls = static_cast<int>(avOwner->GetActorValue(RE::ActorValue::kDragonSouls));
        bool hasPendingLevelUp = false;
        auto playerSkills = player->GetPlayerRuntimeData().skills;
        float playerLevelProgress = 0.0f;

        json currentEffSettings = GetEffectiveSettings(playerLevel);
        int globalSkillCap = currentEffSettings.value("skillCap", 100);
        bool useDynamicCap = currentEffSettings.value("useDynamicSkillCap", true);
        int baseSkillCap = currentEffSettings.value("baseSkillCap", 18);
        float capMult = currentEffSettings.value("skillCapPerLevelMult", 2.0f);
        bool applyRacial = currentEffSettings.value("applyRacialBonusToCap", true);
        bool useBaseSkill = currentEffSettings.value("useBaseSkillLevel", true);

        if (playerSkills) {
            hasPendingLevelUp = playerSkills->CanLevelUp();
            if (playerSkills->data && playerSkills->data->levelThreshold > 0) {
                float currentXP = playerSkills->data->xp;
                float reqXP = playerSkills->data->levelThreshold;
                playerLevelProgress = (currentXP / reqXP) * 100.0f;
                playerLevelProgress = std::clamp(playerLevelProgress, 0.0f, 100.0f);
            }
        }
        std::string raceName = "Unknown";
        if (auto race = player->GetRace()) {
            raceName = race->GetFullName();
        }

        json playerData = {
            {"name", playerName},
            {"health", {{"current", hpCur}, {"max", hpMax}}},
            {"magicka", {{"current", mpCur}, {"max", mpMax}}},
            {"stamina", {{"current", spCur}, {"max", spMax}}},
            {"perkPoints", perkPoints},
            {"level", playerLevel},
            {"levelProgress", playerLevelProgress},
            {"race", raceName},
            {"dragonSouls", dragonSouls},
            {"title", "Dragonborn"},
            {"pendingLevelUp", hasPendingLevelUp},
            {"isLevelUpMenuOpen", Prisma::IsLevelUpMenuOpen()}
        };

        // --- 2. COLETA DE NOVAS INFORMAÇÕES GLOBAIS PARA OS REQUISITOS ---

        // A. Vampiro e Lobisomem (Usando as Keywords nativas da Engine)
        bool isVampire = player->HasKeywordString("Vampire") || player->HasKeywordString("VampireActive");
        bool isWerewolf = player->HasKeywordString("Werewolf") || player->HasKeywordString("ActorTypeCreature");

        // B. Magias Conhecidas (Separação por Escola)
        std::unordered_map<std::string, int> spellsKnownBySchool;
        for (auto spell : player->GetActorRuntimeData().addedSpells) {
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
                        if (av != RE::ActorValue::kNone && player->GetRace()) {
                            for (uint32_t i = 0; i < 7; ++i) {
                                if (player->GetRace()->data.skillBoosts[i].skill == av) {
                                    racialBonus = player->GetRace()->data.skillBoosts[i].bonus;
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
                        currentLevel = static_cast<int>(player->AsActorValueOwner()->GetBaseActorValue(av));
                    }
                    else {
                        currentLevel = static_cast<int>(player->AsActorValueOwner()->GetActorValue(av));
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
                auto mgr = Manager::GetSingleton();
                if (mgr->playerCustomSkills.find(skillName) != mgr->playerCustomSkills.end()) {
                    int baseLevel = mgr->playerCustomSkills[skillName].currentLevel;
                    int bonusLevel = mgr->playerCustomSkills[skillName].bonusLevel;

                    // --- INTEGRAÇÃO DA SETTING USE_BASE_SKILL_LEVEL ---
                    if (useBaseSkill) {
                        currentLevel = baseLevel; // Ignora o bônus
                    }
                    else {
                        currentLevel = baseLevel + bonusLevel; // Aplica o bônus
                    }
                    float currentXP = mgr->playerCustomSkills[skillName].currentXP;
                    float reqXP = mgr->GetRequiredXP(skillName, baseLevel);

                    if (reqXP > 0.0f) {
                        float calcProgress = (currentXP / reqXP) * 100.0f;
                        if (std::isfinite(calcProgress)) {
                            progressPercent = std::clamp(calcProgress, 0.0f, 100.0f);
                        }
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

                    if (!perkStr.empty()) {
                        RE::FormID fullID = ParseFormIDString(perkStr);
                        if (fullID != 0) {
                            auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(fullID);
                            if (perk && player->HasPerk(perk)) hasPerk = true;
                        }
                    }

                    node["isUnlocked"] = hasPerk;
                    if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
                        for (auto& rank : node["nextRanks"]) {
                            std::string rankPerk = rank.value("perk", "");
                            bool rankHas = false;
                            if (!rankPerk.empty()) {
                                RE::FormID fullID = ParseFormIDString(rankPerk);
                                if (fullID != 0) {
                                    auto rPerk = RE::TESForm::LookupByID<RE::BGSPerk>(fullID);
                                    if (rPerk && player->HasPerk(rPerk)) rankHas = true;
                                }
                            }
                            rank["isUnlocked"] = rankHas;
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
                    else if (reqType == "player_level") isMet = (playerLevel >= req.value("value", 0));
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
                            if (spell) isMet = player->HasSpell(spell); 
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
                            if (fact) isMet = player->IsInFaction(fact);
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
                            else if (reqType == "player_level") isMet = (playerLevel >= req.value("value", 0));
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
                                    if (spell) isMet = player->HasSpell(spell); 
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
                                    if (fact) isMet = player->IsInFaction(fact);
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
                                    else if (reqType == "player_level") isMet = (playerLevel >= req.value("value", 0));
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
                                            if (spell) isMet = player->HasSpell(spell); 
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
                                            if (fact) isMet = player->IsInFaction(fact);
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
        std::string currentLangCode = uiSettingsData.value("language", "en");
        json currentLangData = GetLocalizationContent(currentLangCode);
        json fallbackLangData = json::object();
        if (currentLangCode != "en") {
            fallbackLangData = GetLocalizationContent("en");
        }

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

        json availableReqs = json::array({
            //{{"id", "level"}, {"name", "Skill Level (Atual)"}},
            {{"id", "player_level"}, {"name", "Player Level"}},
            {{"id", "perk"}, {"name", "Has Perk"}, {"isForm", true}},
            {{"id", "quest_completed"}, {"name", "Quest Completed"}, {"isForm", true}}, 
            {{"id", "location_discovered"}, {"name", "Location Discovered"}, {"isForm", true}},
            {{"id", "location_cleared"}, {"name", "Location Cleared"}, {"isForm", true}},
            {{"id", "faction"}, {"name", "In Faction"}, {"isForm", true}},
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

        json finalResponse = {
            {"player", playerData},
            {"trees", allTrees},
            {"settings", settingsData},
            {"rules", rulesData},
            {"uiSettings", uiSettingsData},
            {"formLists", formLists},
            {"availableRequirements", availableReqs},
            {"availableLanguages", langs},
            {"activeTranslation", currentLangData},
            {"fallbackTranslation", fallbackLangData}
            
        };

        return finalResponse.dump(-1, ' ', true, json::error_handler_t::replace);

    }
    catch (const std::exception& e) {
        logger::error("ERRO CRÍTICO em GetPlayerSkillsJSON: {}", e.what());
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
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        for (auto& codeObj : settings["codes"]) {
            if (codeObj.value("code", "") == inputCode) {
                int maxUses = codeObj.value("maxUses", -1);
                int currentUses = codeObj.value("currentUses", 0);

                if (maxUses == -1 || currentUses < maxUses) {
                    codeObj["currentUses"] = currentUses + 1;
                    updated = true;

                    if (codeObj.contains("rewards")) {
                        auto rw = codeObj["rewards"];
                        if (rw.contains("perkPoints")) player->GetPlayerRuntimeData().perkCount += rw.value("perkPoints", 0);
                        if (rw.contains("health")) player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kHealth, rw.value("health", 0.0f));
                        if (rw.contains("magicka")) player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kMagicka, rw.value("magicka", 0.0f));
                        if (rw.contains("stamina")) player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kStamina, rw.value("stamina", 0.0f));
                    }
                    logger::info("Codigo '{}' resgatado com sucesso!", inputCode);
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
// Função para lidar com a compra do Perk vinda da UI
static void UnlockPerkFromUI(const char* args) {
    if (!args) return;
    try {
        json payload = json::parse(args);
        std::string perkIDStr = payload.value("id", "");
        int cost = payload.value("cost", 1);

        RE::FormID perkID = ParseFormIDString(perkIDStr);
        if (perkID == 0) return;

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(perkID);
        if (perk) {
            auto playerSkills = player->GetPlayerRuntimeData().skills;
            auto& playerRuntime = player->GetPlayerRuntimeData();

            if (playerRuntime.perkCount >= cost) {
                player->AddPerk(perk);
                playerRuntime.perkCount -= cost;

                logger::info("Perk {} desbloqueado com sucesso! Custo: {}", perkIDStr, cost);

                Prisma::SendUpdateToUI();
            }
            else {
                logger::warn("Tentativa de desbloqueio de perk sem pontos suficientes. Necessario: {}", cost);
            }
        }
    }
    catch (const std::exception& e) {
        logger::error("Erro ao desbloquear perk: {}", e.what());
    }
}


// Função para lidar com o Level Up (Escolha de Atributo)
static void ChooseAttributeFromUI(const char* args) {
    if (!args) return;
    try {
        // Agora recebemos um JSON complexo da UI
        json payload = json::parse(args);
        std::string attribute = payload.value("attribute", "");
        json skillsMap = payload.value("skills", json::object());

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;
        
        int currentLevel = player->GetLevel();
        int targetLevel = currentLevel + 1;
        logger::info("[Prisma] Iniciando processo de Level Up para o level {}", targetLevel);
        // Pega as configurações EFETIVAS para esse nível alcançado
        json effSettings = GetEffectiveSettings(targetLevel);
        bool refillAttributes = effSettings.value("refillAttributesOnLevelUp", false);
        float healthInc = effSettings.value("healthIncrease", 10.0f);
        float magickaInc = effSettings.value("magickaIncrease", 10.0f);
        float staminaInc = effSettings.value("staminaIncrease", 10.0f);
        int perksPerLevel = effSettings.value("perksPerLevel", 1);

        // 1. Aplica o Atributo
        if (attribute == "Health") player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kHealth, healthInc);
        else if (attribute == "Magicka") player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kMagicka, magickaInc);
        else if (attribute == "Stamina") player->AsActorValueOwner()->ModBaseActorValue(RE::ActorValue::kStamina, staminaInc);

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
            player->AsActorValueOwner()->ModActorValue(RE::ACTOR_VALUE_MODIFIER::kPermanent,RE::ActorValue::kCarryWeight, cwInc);
            logger::info("Carry Weight incrementado em {}", cwInc);
        }

        if (refillAttributes) {
            // kDamage é o modificador que o Skyrim usa para "dano recebido". 
            // Restaurar 99999 remove todo o dano, enchendo a barra.
            player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kHealth, 999999.0f);
            player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kMagicka, 999999.0f);
            player->AsActorValueOwner()->RestoreActorValue(RE::ActorValue::kStamina, 999999.0f);
        }

        // 2. Aplica as Skills Escolhidas (Sobe de nível!)
        for (auto& [skillName, amountVal] : skillsMap.items()) {
            int amount = amountVal.get<int>();
            if (amount > 0) {
                RE::ActorValue av = GetActorValueFromName(skillName);
                if (av != RE::ActorValue::kNone) {
                    // É Vanilla: Sobe o nível via Engine nativa
                    player->AsActorValueOwner()->ModBaseActorValue(av, static_cast<float>(amount));
                }
                else {
                    // É Custom Skill: Modifica pelo nosso Manager
                    auto mgr = Manager::GetSingleton();
                    if (mgr->playerCustomSkills.find(skillName) != mgr->playerCustomSkills.end()) {
                        mgr->playerCustomSkills[skillName].currentLevel += amount;
                        // Opcional: Zera a barra de XP atual para essa skill se subir pelo UI de stats?
                        // mgr->playerCustomSkills[skillName].currentXP = 0.0f;
                    }
                }
            }
        }

        // 3. Processa o Level Up e Perks Extras
        auto playerSkills = player->GetPlayerRuntimeData().skills;
        if (playerSkills) {
            playerSkills->AdvanceLevel(false);
        }

        if (perksPerLevel != 1) {
            // Matemática segura e cast explícito para uint8_t (limite 0-255)
            int extraPerks = perksPerLevel - 1;
            int currentPerks = static_cast<int>(player->GetPlayerRuntimeData().perkCount);
            int newPerkCount = currentPerks + extraPerks;
            
            // Trava o valor entre 0 e 255 para evitar underflow/overflow da engine do Skyrim
            newPerkCount = std::clamp(newPerkCount, 0, 255);
            
            player->GetPlayerRuntimeData().perkCount = static_cast<uint8_t>(newPerkCount);
        }

        /*auto eventSource = RE::LevelIncrease::GetEventSource();
        if (eventSource) {
            RE::LevelIncrease::Event e{ player, (uint16_t)player->GetLevel() };
            eventSource->SendEvent(&e);
        }*/

        logger::info("Level Up processado para: {}. Skills incrementadas: {}", attribute, skillsMap.dump());
        auto msgQueue = RE::UIMessageQueue::GetSingleton();
        if (msgQueue) {
            msgQueue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
        }

                Prisma::SendUpdateToUI();

        logger::info("[Prisma] ChooseAttributeFromUI finalizado com sucesso e UI atualizada.");
    }
    catch (const std::exception& e) {
        logger::error("Erro ao aplicar level up complexo: {}", e.what());
    }
}

static void SaveSettingsFromUI(const char* jsonArgs) {
    if (!jsonArgs) return;
    try {
        json newSettings = json::parse(jsonArgs);
        std::filesystem::path dir("Data\\PrismaUI\\views\\" PRODUCT_NAME);
        if (!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);

        std::ofstream file("Data\\PrismaUI\\views\\" PRODUCT_NAME "\\Settings.json");
        if (file.is_open()) {
            file << newSettings.dump(4);
            file.close();
            logger::info("Settings.json atualizado com sucesso pela UI.");
        }
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
            if (tree.contains("nodes") && tree["nodes"].is_array()) {
                for (auto& node : tree["nodes"]) {
                    node.erase("isUnlocked");
                    node.erase("canUnlock");
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

// =========================================================================================
// HELPER: REMOVER PERKS DE UMA ÁRVORE E CALCULAR REEMBOLSO
// =========================================================================================
int RemovePerksFromTree(const json& treeData, RE::PlayerCharacter* player) {
    int pointsRefunded = 0;

    if (!treeData.contains("nodes") || !treeData["nodes"].is_array()) return 0;

    for (const auto& node : treeData["nodes"]) {
        // 1. Verifica e remove Ranks Superiores primeiro (de trás para frente)
        if (node.contains("nextRanks") && node["nextRanks"].is_array()) {
            const auto& ranks = node["nextRanks"];
            // Itera reverso para remover do maior rank para o menor
            for (auto it = ranks.rbegin(); it != ranks.rend(); ++it) {
                std::string rankIDStr = it->value("perk", "");
                int cost = it->value("perkCost", 1);

                RE::FormID rankID = ParseFormIDString(rankIDStr);
                if (rankID != 0) {
                    auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(rankID);
                    if (perk && player->HasPerk(perk)) {
                        logger::debug("[RemovePerks] Removendo Rank Perk: {:08X} ({})", rankID, rankIDStr);
                        player->RemovePerk(perk);
                        pointsRefunded += cost;
                    }
                }
            }
        }

        // 2. Verifica e remove o Perk Base
        std::string baseIDStr = node.value("perk", "");
        int baseCost = node.value("perkCost", 1);
        RE::FormID baseID = ParseFormIDString(baseIDStr);

        if (baseID != 0) {
            auto perk = RE::TESForm::LookupByID<RE::BGSPerk>(baseID);
            if (perk && player->HasPerk(perk)) {
                logger::debug("[RemovePerks] Removendo Base Perk: {:08X} ({})", baseID, baseIDStr);
                player->RemovePerk(perk);
                pointsRefunded += baseCost;
            }
        }
    }
    return pointsRefunded;
}

// =========================================================================================
// CALLBACK: LEGENDARY SKILL (Reseta Nivel + Perks)
// =========================================================================================
static void LegendarySkillFromUI(const char* args) {
    if (!args) return;
    try {
        json payload = json::parse(args);
        std::string treeName = payload.value("treeName", "");

        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

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

        // 1. Remover Perks e Calcular Pontos
        int refunded = RemovePerksFromTree(targetTree, player);

        // 2. Devolver Pontos
        if (refunded > 0) {
            // Cast seguro para evitar overflow se o count for uint8_t
            int currentPoints = static_cast<int>(player->GetPlayerRuntimeData().perkCount);
            int newTotal = currentPoints + refunded;
            player->GetPlayerRuntimeData().perkCount = static_cast<uint8_t>(std::min(newTotal, 255));

            logger::info("[Legendary] Skill '{}': {} pontos devolvidos.", treeName, refunded);
        }
        else {
            logger::debug("[Legendary] Skill '{}' nao teve perks a reembolsar.", treeName);
        }

        // 3. Resetar Nível da Skill
        int initialLevel = targetTree.value("initialLevel", 15);
        bool isVanilla = targetTree.value("isVanilla", false);

        if (isVanilla) {
            RE::ActorValue av = GetActorValueFromName(treeName);
            if (av != RE::ActorValue::kNone) {
                // Define o valor base para o inicial
                player->AsActorValueOwner()->SetBaseActorValue(av, static_cast<float>(initialLevel));
                logger::debug("[Legendary] Nivel da skill vanilla {} resetado para {}", treeName, initialLevel);
            }
        }
        else {
            // Custom Skill
            auto mgr = Manager::GetSingleton();
            if (mgr->playerCustomSkills.find(treeName) != mgr->playerCustomSkills.end()) {
                mgr->playerCustomSkills[treeName].currentLevel = initialLevel;
                mgr->playerCustomSkills[treeName].currentXP = 0.0f;
                logger::debug("[Legendary] Nivel da custom skill {} resetado para {}", treeName, initialLevel);
            }
        }

        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            SKSE::GetTaskInterface()->AddUITask([]() {
                logger::debug("[Legendary] Disparando atualizacao da UI apos delay...");
                Prisma::SendUpdateToUI();
                });
            }).detach();
    }
    catch (const std::exception& e) {
        logger::error("Erro em LegendarySkillFromUI: {}", e.what());
    }
}

// =========================================================================================
// CALLBACK: RESET ALL PERKS (Apenas Perks, mantem niveis)
// =========================================================================================
static void ResetAllPerksFromUI(const char*) {
    try {
        auto player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        logger::debug("[ResetAll] Iniciando remocao de TODOS os perks...");

        json allTrees = GetLoadedSkillTreeConfigs();
        int totalRefunded = 0;

        for (const auto& tree : allTrees) {
            int refunded = RemovePerksFromTree(tree, player);
            if (refunded > 0) {
                logger::debug("[ResetAll] {} pontos reembolsados da arvore '{}'.", refunded, tree.value("name", "Unknown"));
            }
            totalRefunded += refunded;
        }

        if (totalRefunded > 0) {
            int currentPoints = static_cast<int>(player->GetPlayerRuntimeData().perkCount);
            int newTotal = currentPoints + totalRefunded;
            player->GetPlayerRuntimeData().perkCount = static_cast<uint8_t>(std::min(newTotal, 255));

            logger::info("[ResetAll] {} pontos totais devolvidos ao jogador.", totalRefunded);
        }
        else {
            logger::debug("[ResetAll] Nenhum perk foi removido (Nenhum ponto reembolsado).");
        }

        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            SKSE::GetTaskInterface()->AddUITask([]() {
                logger::debug("[ResetAll] Disparando atualizacao da UI apos delay...");
                Prisma::SendUpdateToUI();
                });
            }).detach();
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
                auto msgQueue = RE::UIMessageQueue::GetSingleton();
                if (msgQueue) {
                    msgQueue->AddMessage(RE::StatsMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
                    msgQueue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr); // Garante o fechamento de ambos
                }
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
                            mgr->playerCustomSkills.erase(treeName);
                            mgr->customSkillsData.erase(treeName);

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
            PrismaUI->RegisterJSListener(currentView, "saveSettings", [](const char* args) { SaveSettingsFromUI(args); });
            PrismaUI->RegisterJSListener(currentView, "saveUISettings", [](const char* args) { SaveUISettingsFromUI(args); });
            SendUpdateToUI();
            PrismaUI->Focus(currentView, true);
            /*auto msgQueue = RE::UIMessageQueue::GetSingleton();
            if (msgQueue) {
                msgQueue->AddMessage(RE::LevelUpMenu::MENU_NAME, RE::UI_MESSAGE_TYPE::kHide, nullptr);
            }*/
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

void Prisma::PreloadLocalization() {
    logger::info("Pré-carregando dados de localização...");

    // A. Escaneia a pasta e cacheia os nomes dos arquivos
    GetAvailableLanguages();

    // B. Carrega sempre o inglês (Fallback) para a memória
    GetLocalizationContent("en");

    // C. Descobre qual idioma o usuário usou por último e já carrega ele também
    try {
        json settings = GetUISettings();
        std::string currentLang = settings.value("language", "en");

        if (currentLang != "en") {
            logger::info("Pré-carregando idioma do usuario: {}", currentLang);
            GetLocalizationContent(currentLang);
        }
    }
    catch (...) {
        logger::warn("Erro ao tentar pré-carregar settings de idioma.");
    }
}



void Prisma::SetLevelUpMenuOpen(bool isOpen) {
    g_isLevelUpMenuOpen = isOpen;
}

bool Prisma::IsLevelUpMenuOpen() {
    return g_isLevelUpMenuOpen;
}
