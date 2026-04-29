import React, { useState, useEffect, useRef, memo, useCallback, useMemo } from 'react';
import { createPortal } from 'react-dom';
import useEmblaCarousel from 'embla-carousel-react';
import { SketchPicker } from 'react-color';
import './App.css';
import { t, setLanguage, addTranslation, hasTranslation, type Language } from './lib/locales';
import { Stage, Layer, Line, Circle, Group, Image as KonvaImage, Text as KonvaText, Rect } from 'react-konva';
import Konva from 'konva';
// --- Interfaces ---
interface Requirement {
    type: string;
    value: any;
    target?: string;
    isMet?: boolean;
    isOr?: boolean; 
    isNot?: boolean;
}
interface UISettings {
    language: Language;
    hideLockedTreeNames: boolean;
    hideLockedTreeBG: boolean;
    performanceMode: boolean;
    columnPreviewMode?: 'full' | 'bg' | 'tree' | 'none';
    enableEditorMode: boolean;
    hidePerkNames: boolean;
}
interface AvailablePerk {
    id: string;
    name: string;
    editorId?: string;
    description?: string;
    nextPerk?: string;
    requirements?: Requirement[];
}
interface CustomResource {
    id: string;
    name: string;
    glob: string;
}

interface CustomCost {
    resourceId: string;
    amount: number;
}
interface PerkRank {
    perk: string; name: string; description: string;
    perkCost: number; requirements: Requirement[];
    isUnlocked?: boolean; canUnlock?: boolean;
    customCosts?: CustomCost[];
}
interface PerkNode {
    id: string; perk: string; name: string; description: string;
    icon: string; x: number; y: number; requirements: Requirement[];
    links: string[]; isUnlocked: boolean; canUnlock?: boolean;
    perkCost: number;
    nextRanks?: PerkRank[];
    customCosts?: CustomCost[];
}
interface ExperienceFormula {
    useMult: number;
    useOffset: number;
    improveMult: number;
    improveOffset: number;
}
interface SkillTreeData {
    name: string;
    displayName?: string;
    isVanilla: boolean;
    advancesPlayerLevel?: boolean;
    color: string;
    initialLevel: number;
    bgPath: string;
    iconPath: string;
    iconPerkPath: string;
    selectionIconPath: string;
    category: string;
    treeRequirements: Requirement[];
    experienceFormula?: ExperienceFormula;
    nodes: PerkNode[];
    currentLevel: number;
    currentProgress: number;
    cap?: number;
    isHidden?: boolean;
}
interface PlayerData {
    name: string;
    health: { current: number; max: number };
    magicka: { current: number; max: number };
    stamina: { current: number; max: number };
    perkPoints: number;
    level?: number;
    levelProgress?: number;
    title?: string;
    race?: string;
    dragonSouls?: number;
    pendingLevelUps?: number;
    isLevelUpMenuOpen?: boolean;
    resourceValues?: Record<string, number>;
}
interface LevelRule {
    level: number;
    perksPerLevel?: number;
    healthIncrease?: number;
    staminaIncrease?: number;
    magickaIncrease?: number;
    skillPointsPerLevel?: number;
    maxSkillPointsSpendablePerLevel?: number;
    skillCap?: number;
    useDynamicSkillCap?: boolean;
    carryWeightIncrease?: number;
}
interface Reward {
    perkPoints?: number;
    health?: number;
    magicka?: number;
    stamina?: number;
}
interface CodeData {
    code: string;
    maxUses: number;
    currentUses: number;
    rewards: Reward;
    isEditorCode?: boolean;
}
interface RequirementDef {
    id: string;
    name: string;
    isForm?: boolean;
}
interface SettingsData {
    base: {
        perksPerLevel: number;
        healthIncrease: number;
        staminaIncrease: number;
        magickaIncrease: number;
        skillPointsPerLevel: number;
        maxSkillPointsSpendablePerLevel: number;
        enableLegendary: boolean;
        refillAttributesOnLevelUp?: boolean;
        useBaseSkillLevel?: boolean;
        skillCap?: number;
        useDynamicSkillCap?: boolean;
        skillCapPerLevelMult?: number;
        applyRacialBonusToCap?: boolean;
        applyVanillaInitialLevels?: boolean;
        carryWeightIncrease?: number;
        carryWeightMethod?: 'none' | 'auto' | 'linked';
        carryWeightLinkedAttributes?: string[];
    };
    categories: string[];
    codes: CodeData[];
}

function resolveText(text: string | undefined, isEditor: boolean): string {
    if (!text) return "";
    if (isEditor) return text;

    // Procura por todas as instâncias de {{$...}}
    return text.replace(/\{\{\$([a-zA-Z0-9_.]+)\}\}/g, (match, key) => {
        const translated = t(key);
        // Se houver tradução retorna ela, caso contrário retorna a string bruta
        return translated !== key ? translated : match;
    });
}

// === DROPDOWN CUSTOMIZADO ===
const CustomSelect = ({
    options,
    value,
    onChange,
    placeholder = t('common.search_short'),
    width = "auto",
    disableSearch = false // Mantido na assinatura para evitar erros de tipagem, mas o efeito foi removido
}: {
    options: { value: string | number, label: string }[],
    value: string | number,
    onChange: (val: any) => void,
    placeholder?: string,
    width?: string,
    disableSearch?: boolean
}) => {
    const [isOpen, setIsOpen] = useState(false);
    const [searchTerm, setSearchTerm] = useState("");
    const dropdownRef = useRef<HTMLDivElement>(null);

    // Fecha o dropdown se clicar fora dele
    useEffect(() => {
        const handleClickOutside = (event: MouseEvent) => {
            if (dropdownRef.current && !dropdownRef.current.contains(event.target as Node)) {
                setIsOpen(false);
            }
        };
        if (isOpen) {
            document.addEventListener('mousedown', handleClickOutside);
        }
        return () => {
            document.removeEventListener('mousedown', handleClickOutside);
        };
    }, [isOpen]);

    const selectedOption = options.find(opt => String(opt.value) === String(value));
    const displayLabel = selectedOption ? selectedOption.label : value;

    const filteredOptions = options.filter(opt =>
        opt.label.toLowerCase().includes(searchTerm.toLowerCase())
    );

    return (
        <div ref={dropdownRef} className="skyrim-select-container" style={{ width: width, position: 'relative' }}>
            <button
                type="button"
                className="skyrim-dropdown form-selector-trigger-btn"
                style={{ width: '100%', textAlign: 'left', padding: '5px', margin: 0, display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}
                onClick={(e) => {
                    e.preventDefault();
                    setIsOpen(!isOpen);
                    if (!isOpen) setSearchTerm(""); // Limpa a busca ao abrir
                }}
            >
                <span style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{displayLabel}</span>
                <span style={{ fontSize: '0.8rem', marginLeft: '5px', opacity: 0.7 }}>▼</span>
            </button>

            {isOpen && (
                <div style={{
                    position: "absolute", top: "100%", left: 0, width: "100%", minWidth: "150px",
                    background: "rgba(0,0,0,0.95)", border: "1px solid #777",
                    zIndex: 9999, maxHeight: "200px", overflowY: "auto",
                    display: "flex", flexDirection: "column", padding: "5px",
                    boxShadow: "0 4px 6px rgba(0,0,0,0.5)"
                }}>
                    {/* O input de pesquisa agora aparece sempre, sem checar options.length ou disableSearch */}
                    <input
                        type="text"
                        className="skyrim-search-input"
                        placeholder={placeholder}
                        value={searchTerm}
                        onChange={(e) => setSearchTerm(e.target.value)}
                        onClick={(e) => e.stopPropagation()}
                        autoFocus
                        style={{ marginBottom: "8px", width: "100%", padding: "5px", fontSize: '0.9rem' }}
                    />

                    {filteredOptions.length > 0 ? (
                        filteredOptions.map((opt, i) => (
                            <button
                                key={i}
                                className="sl-action-btn"
                                style={{
                                    textAlign: "left", padding: "8px", margin: "2px 0", border: "none",
                                    background: String(opt.value) === String(value) ? 'rgba(255, 255, 255, 0.2)' : 'transparent',
                                    color: String(opt.value) === String(value) ? '#ff9800' : 'white'
                                }}
                                onClick={(e) => {
                                    e.preventDefault();
                                    e.stopPropagation();
                                    onChange(opt.value);
                                    setIsOpen(false);
                                }}
                            >
                                {opt.label}
                            </button>
                        ))
                    ) : (
                        <div style={{ padding: "8px", color: "#777", textAlign: "center" }}>{t('common.no_items_found')}</div>
                    )}
                </div>
            )}
        </div>
    );
};

const useCustomImage = (url: string) => {
    const [image, setImage] = useState<HTMLImageElement | undefined>(undefined);
    useEffect(() => {
        if (!url) return;
        const img = new Image();
        img.src = url;
        img.onload = () => setImage(img);
    }, [url]);
    return [image];
};

// New Helper Hook: Monitors element size for Konva Stage dimensions
const useElementSize = (ref: React.RefObject<HTMLElement>) => {
    const [size, setSize] = useState({ width: 0, height: 0 });

    useEffect(() => {
        if (!ref.current) return;

        const updateSize = () => {
            if (ref.current) {
                // Use offsetWidth/Height to get layout size before CSS transforms
                setSize({
                    width: ref.current.offsetWidth,
                    height: ref.current.offsetHeight
                });
            }
        };

        updateSize();
        const observer = new ResizeObserver(updateSize);
        observer.observe(ref.current);

        return () => observer.disconnect();
    }, [ref]);

    return size;
};

const FileBrowserModal = ({ field, initialPath, onClose, onSelect }: { field: string, initialPath: string, onClose: () => void, onSelect: (f: string, p: string) => void }) => {
    const [currentPath, setCurrentPath] = useState(initialPath || "");
    const [folders, setFolders] = useState<string[]>([]);
    const [files, setFiles] = useState<string[]>([]);

    useEffect(() => {
        const handleResponse = (e: any) => {
            if (e.detail.field === field) {
                setFolders(e.detail.folders);
                setFiles(e.detail.files);
            }
        };
        window.addEventListener('fileListResponse', handleResponse);
        return () => window.removeEventListener('fileListResponse', handleResponse);
    }, [field]);

    useEffect(() => {
        if (typeof (window as any).requestFileList === 'function') {
            (window as any).requestFileList(JSON.stringify({ path: currentPath, field }));
        }
    }, [currentPath, field]);

    const goUp = () => {
        const parts = currentPath.split('/').filter(p => p);
        parts.pop();
        setCurrentPath(parts.join('/'));
    };

    const formatRelativeResult = (fileName: string) => {
        return currentPath ? `./${currentPath}/${fileName}` : `./${fileName}`;
    };

    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 7000 }}>
            <div className="skyrim-modal-content form-selector-modal">
                <h2>{t('browser.title')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '15px' }}></div>
                <p style={{ textAlign: 'left', color: '#4dd0e1', margin: '0 0 10px 0' }}>{t('browser.current_location')} ./{currentPath}</p>

                <div className="table-container" style={{ display: 'flex', flexDirection: 'column', padding: '10px', gap: '5px' }}>
                    {currentPath !== "" && (
                        <div className="browser-item folder" onClick={goUp}>
                            📁 {t('browser.go_back')}
                        </div>
                    )}
                    {folders.map(f => (
                        <div key={f} className="browser-item folder" onClick={() => setCurrentPath(currentPath ? `${currentPath}/${f}` : f)}>
                            📁 {f}
                        </div>
                    ))}
                    {files.map(f => (
                        <div key={f} className="browser-item file" onClick={() => onSelect(field, formatRelativeResult(f))}>
                            🖼️ {f}
                        </div>
                    ))}
                    {folders.length === 0 && files.length === 0 && <p style={{ color: '#666' }}>{t('browser.empty_folder')}</p>}
                </div>
                <div className="modal-actions" style={{ marginTop: '20px' }}>
                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>
        </div>
    );
};

function getEffectiveSettings(settings: SettingsData, rules: LevelRule[], targetLevel: number) {
    let eff = { ...settings.base };

    const hardCap = settings.base.skillCap || 100;

    if (!rules || rules.length === 0) return eff;

    const sortedRules = [...rules].sort((a, b) => a.level - b.level);
    for (const rule of sortedRules) {
        if (rule.level <= targetLevel) {
            if (rule.perksPerLevel !== undefined) eff.perksPerLevel = rule.perksPerLevel;
            if (rule.healthIncrease !== undefined) eff.healthIncrease = rule.healthIncrease;
            if (rule.staminaIncrease !== undefined) eff.staminaIncrease = rule.staminaIncrease;
            if (rule.magickaIncrease !== undefined) eff.magickaIncrease = rule.magickaIncrease;
            if (rule.skillPointsPerLevel !== undefined) eff.skillPointsPerLevel = rule.skillPointsPerLevel;
            if (rule.maxSkillPointsSpendablePerLevel !== undefined) eff.maxSkillPointsSpendablePerLevel = rule.maxSkillPointsSpendablePerLevel;
            if (rule.skillCap !== undefined) eff.skillCap = rule.skillCap;
            if (rule.carryWeightIncrease !== undefined) eff.carryWeightIncrease = rule.carryWeightIncrease;
            if (rule.useDynamicSkillCap !== undefined) eff.useDynamicSkillCap = rule.useDynamicSkillCap;
        }
    }

    if (eff.skillCap && eff.skillCap > hardCap) {
        eff.skillCap = hardCap;
    }

    return eff;
}

const DEFAULT_BG = './Assets/DefaultBG.png';
const DEFAULT_ICON = './Assets/Default.svg';
const DEFAULT_PERK_ICON = './Assets/Perk.svg';
const DEFAULT_COLOR = '#ffffff';

const imageValidationCache = new Map<string, string>();

function getNearestNode(nodes: PerkNode[], currentId: string | null, direction: 'up' | 'down' | 'left' | 'right'): string | null {
    if (!currentId) {
        if (nodes.length === 0) return null;
        return nodes.reduce((prev, curr) => (prev.y > curr.y ? prev : curr)).id;
    }

    const current = nodes.find(n => n.id === currentId);
    if (!current) return null;

    let candidates = nodes.filter(n => n.id !== currentId);
    let bestCandidate: string | null = null;
    let minDistance = Infinity;

    candidates = candidates.filter(target => {
        const dx = target.x - current.x;
        const dy = target.y - current.y;

        switch (direction) {
            case 'up': return dy < -1;
            case 'down': return dy > 1;
            case 'left': return dx < -1;
            case 'right': return dx > 1;
        }
    });

    candidates.forEach(target => {
        const dx = target.x - current.x;
        const dy = target.y - current.y;
        const dist = (dx * dx) + (dy * dy);

        if (dist < minDistance) {
            minDistance = dist;
            bestCandidate = target.id;
        }
    });

    return bestCandidate;
}

function useVisibility(rootMargin = '0px') {
    const [isVisible, setIsVisible] = useState(false);
    const ref = useRef<HTMLDivElement>(null);

    useEffect(() => {
        const currentRef = ref.current;
        if (!currentRef) return;
        const observer = new IntersectionObserver(
            ([entry]) => setIsVisible(entry.isIntersecting),
            { rootMargin }
        );
        observer.observe(currentRef);
        return () => observer.disconnect();
    }, [rootMargin]);

    return [ref, isVisible] as const;
}

function useValidImage(srcPath: string | undefined, defaultPath: string) {
    if (!srcPath || srcPath.trim() === "") return defaultPath;
    if (imageValidationCache.has(srcPath)) {
        return imageValidationCache.get(srcPath) as string;
    }
    imageValidationCache.set(srcPath, srcPath);
    return srcPath;
}

const playSound = (soundId: string) => {
    if (typeof (window as any).playUISound === 'function') {
        (window as any).playUISound(soundId);
    }
};

// OTIMIZAÇÃO: "listening={false}" equivalente no DOM. Removido pointer-events para
// que o motor de renderização ignore completamente cálculos de rato (hit-testing) em imagens estáticas/vetoriais.
const DiamondSeparator = () => (
    <svg className="diamond-separator" viewBox="0 0 24 24" style={{ pointerEvents: 'none' }}>
        <path d="M12 2L22 12L12 22L2 12Z" fill="none" stroke="currentColor" strokeWidth="1" />
        <path d="M12 6L18 12L12 18L6 12Z" fill="currentColor" opacity="0.5" />
    </svg>
);

const BarFrameSVG = () => (
    <img src="./Assets/Bar2.svg" className="bar-frame-svg" alt="Bar Frame" style={{ pointerEvents: 'none' }} />
);

const MiniBarFrameSVG = () => (
    <img src="./Assets/Bar.svg" className="bar-frame-svg" alt="Mini Bar Frame" style={{ pointerEvents: 'none' }} />
);

const RequirementInputRow = ({
    req, availableReqs, availableTrees, formLists, onUpdate, onRemove, onSelectTarget
}: {
    req: Requirement, availableReqs: RequirementDef[], availableTrees: string[], formLists: Record<string, AvailablePerk[]>,
    onUpdate: (field: string, val: any) => void, onRemove: () => void, onSelectTarget: (type: string) => void
}) => {
    const reqDef = availableReqs.find(r => r.id === req.type);
    const isForm = reqDef?.isForm === true;

    // Pega a lista correspondente de forma dinâmica e o item selecionado
    const selectedList = formLists[req.type];
    const selectedItem = isForm && selectedList ? selectedList.find(i => i.id === req.value) : null;

    const displayTargetText = selectedItem
        ? `${selectedItem.name} | ${selectedItem.id}`
        : (req.value ? req.value : t('common.select'));

    const reqTypeOptions = availableReqs.map(r => ({ value: r.id, label: r.name }));
    const treeOptions = [{ value: "", label: t('common.select') }, ...availableTrees.map(t => ({ value: t, label: t }))];
    const magicSchoolOptions = [
        { value: "", label: t('common.select_magic_school') },
        { value: "Alteration", label: t('magic_schools.alteration') },
        { value: "Conjuration", label: t('magic_schools.conjuration') },
        { value: "Destruction", label: t('magic_schools.destruction') },
        { value: "Illusion", label: t('magic_schools.illusion') },
        { value: "Restoration", label: t('magic_schools.restoration') }
    ];

    return (
        <div style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '5px' }}>

            {/* BOTÃO DE LÓGICA (AND/OR) */}
            <button
                className="modal-btn"
                style={{
                    width: '80px',
                    padding: '5px',
                    fontSize: '0.8rem',
                    background: req.isOr ? '#ff9800' : 'rgba(255,255,255,0.1)',
                    color: req.isOr ? 'black' : 'white',
                    border: req.isOr ? '1px solid #ff9800' : '1px solid #555'
                }}
                onClick={() => onUpdate('isOr', !req.isOr)}
                title={req.isOr ? t('reqs.logic_or_title') : t('reqs.logic_and_title')}
            >
                {req.isOr ? t('common.or') : t('common.and')}
            </button>
            <button
                className="modal-btn"
                style={{
                    width: '50px',
                    padding: '5px',
                    fontSize: '0.8rem',
                    background: req.isNot ? '#f44336' : 'rgba(255,255,255,0.1)',
                    color: 'white',
                    border: req.isNot ? '1px solid #f44336' : '1px solid #555'
                }}
                onClick={() => onUpdate('isNot', !req.isNot)}
                title={t('reqs.must_not_have')}
            >
                {req.isNot ? t('common.not') : t('common.has')}
            </button>
            <CustomSelect
                options={reqTypeOptions}
                value={req.type}
                onChange={(val) => onUpdate('type', val)}
                width="180px"
                disableSearch={true}
            />

            {isForm && (
                <button className="form-selector-trigger-btn" style={{ width: 'auto', padding: '5px', margin: 0, fontSize: '0.9rem' }} onClick={() => onSelectTarget(req.type)}>
                    {displayTargetText}
                </button>
            )}

            {req.type === 'any_skill' && (
                <>
                    <CustomSelect
                        options={treeOptions}
                        value={req.target || ''}
                        onChange={(val) => onUpdate('target', val)}
                        width="150px"
                        placeholder={t('common.select')}
                    />
                    <input type="number" placeholder={t('common.lvl')} value={req.value || ''} onChange={e => onUpdate('value', Number(e.target.value))} style={{ width: '60px', background: 'rgba(0,0,0,0.5)', color: 'white', padding: '5px' }} />
                </>
            )}

            {req.type === 'spells_known' && (
                <>
                    <CustomSelect
                        options={magicSchoolOptions}
                        value={req.target || ''}
                        onChange={(val) => onUpdate('target', val)}
                        width="150px"
                        disableSearch={true}
                    />
                    <input type="number" placeholder={t('common.qty')} value={req.value || ''} onChange={e => onUpdate('value', Number(e.target.value))} style={{ width: '60px', background: 'rgba(0,0,0,0.5)', color: 'white', padding: '5px' }} />
                </>
            )}

            {(req.type === 'is_vampire' || req.type === 'is_werewolf') && (
                <span style={{ color: 'gray', width: '130px', fontSize: '0.8rem', textAlign: 'center' }}>{t('reqs.boolean_base_value')}</span>
            )}

            {(req.type === 'level' || req.type === 'player_level' || req.type === 'kills') && (
                <input type="number" placeholder={t('common.value')} value={req.value || ''} onChange={e => onUpdate('value', Number(e.target.value))} style={{ width: '100px', background: 'rgba(0,0,0,0.5)', color: 'white', padding: '5px' }} />
            )}

            {(!isForm && !['any_skill', 'spells_known', 'is_vampire', 'is_werewolf', 'level', 'player_level', 'kills'].includes(req.type)) && (
                <input type="text" placeholder={t('common.id_value')} value={req.value || ''} onChange={e => onUpdate('value', e.target.value)} style={{ width: '100px', background: 'rgba(0,0,0,0.5)', color: 'white', padding: '5px' }} />
            )}

            <button className="delete-btn" onClick={onRemove}>X</button>
        </div>
    );
};

const FormSelectorModal = ({ items, onClose, onSelect }: { items: AvailablePerk[], onClose: () => void, onSelect: (id: string) => void }) => {
    const [search, setSearch] = useState("");

    const filteredItems = useMemo(() => {
        const lowerSearch = search.toLowerCase();
        return items.filter(item =>
            item.name.toLowerCase().includes(lowerSearch) ||
            item.id.toLowerCase().includes(lowerSearch) ||
            (item.editorId && item.editorId.toLowerCase().includes(lowerSearch))
        );
    }, [items, search]);

    return (
        <div className="skyrim-modal-overlay"
            style={{ zIndex: 4000 }}
            onClick={(e) => {
                if (e.target === e.currentTarget) onClose();
            }}
        >
            <div className="skyrim-modal-content form-selector-modal">
                <h2>{t('common.click_select')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '15px' }}></div>

                <input
                    type="text"
                    placeholder={t('common.search_placeholder')}
                    value={search}
                    onChange={e => setSearch(e.target.value)}
                    className="form-search-input"
                    autoFocus
                />

                <div className="table-container">
                    <table className="form-selector-table">
                        <thead>
                            <tr>
                                <th>{t('header.name')}</th>
                                <th>{t('header.plugin')}</th>
                                <th>{t('header.form_id')}</th>
                                <th>{t('header.editor_id')}</th>
                            </tr>
                        </thead>
                        <tbody>
                            {filteredItems.map(item => {
                                const splitId = item.id.split('|');
                                const plugin = splitId.length > 1 ? splitId[0] : t('common.unknown');
                                const formId = splitId.length > 1 ? splitId[1] : item.id;

                                return (
                                    <tr key={item.id} onClick={() => onSelect(item.id)}>
                                        <td>{item.name}</td>
                                        <td>{plugin}</td>
                                        <td>{formId}</td>
                                        <td>{item.editorId || 'N/A'}</td>
                                    </tr>
                                );
                            })}
                        </tbody>
                    </table>
                    {filteredItems.length === 0 && <p style={{ textAlign: 'center', marginTop: '20px', color: '#ccc' }}>No items found.</p>}
                </div>

                <div className="modal-actions" style={{ marginTop: '20px' }}>
                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>
        </div>
    );
};

const TreeEditorModal = ({ tree, settings, availableReqs, availableTrees, formLists, onClose, onSave, onRequestBrowse }: {
    tree: SkillTreeData, settings: SettingsData, availableReqs: RequirementDef[], availableTrees: string[], formLists: Record<string, AvailablePerk[]>, onClose: () => void,
    onSave: (t: SkillTreeData) => void, onRequestBrowse: (field: string) => void
}) => {
    const [formData, setFormData] = useState<SkillTreeData>(tree);
    const [selectingReqTarget, setSelectingReqTarget] = useState<{ idx: number, type: string } | null>(null);
    const [displayColorPicker, setDisplayColorPicker] = useState(false);
    const [isDirty, setIsDirty] = useState(false);

    useEffect(() => {
        const handleFileSelected = (e: any) => {
            const { field, path } = e.detail;
            setFormData(prev => ({ ...prev, [field]: path }));
        };
        window.addEventListener('fileSelected', handleFileSelected);
        return () => window.removeEventListener('fileSelected', handleFileSelected);
    }, []);

    useEffect(() => {
        const cleanForm = JSON.stringify(formData);
        const cleanTree = JSON.stringify(tree);
        setIsDirty(cleanForm !== cleanTree);
    }, [formData, tree]);

    const handleChange = (e: any) => {
        const { name, value, type, checked } = e.target;
        setFormData(prev => ({
            ...prev,
            [name]: type === 'checkbox' ? checked : (name === 'initialLevel' ? Number(value) : value)
        }));
    };

    const handleExpChange = (e: any) => {
        const { name, value } = e.target;
        setFormData(prev => ({
            ...prev,
            experienceFormula: {
                ...(prev.experienceFormula || { useMult: 1, useOffset: 0, improveMult: 1, improveOffset: 0 }),
                [name]: Number(value)
            }
        }));
    };

    const exp = formData.experienceFormula || { useMult: 1, useOffset: 0, improveMult: 1, improveOffset: 0 };

    const handleExport = () => {
        if (isDirty) return;
        if (typeof (window as any).exportTree === 'function') {
            (window as any).exportTree(JSON.stringify(formData));
        }
    };

    return (
        <div
            className="skyrim-modal-overlay"
            style={{ zIndex: 3000 }}
            onClick={(e) => {
                if (e.target === e.currentTarget) onClose();
            }}
        >
            <div
                className="skyrim-modal-content settings-modal-content ui-mode tree-editor-modal"
                onClick={(e) => e.stopPropagation()}
            >
                <h2>{t('tree_editor.title')}</h2>
                <div className="settings-grid">
                    <label>{t('tree_editor.internal_id')}
                        <input type="text" name="name" value={formData.name} disabled title={t('tree_editor.id_warning')} />
                    </label>
                    <label>{t('tree_editor.display_name')}
                        <input type="text" name="displayName" value={formData.displayName || formData.name} onChange={handleChange} />
                    </label>

                    <label>{t('tree_editor.category')}
                        <CustomSelect
                            options={settings.categories?.map(cat => ({ value: cat, label: cat })) || []}
                            value={formData.category}
                            onChange={(val) => setFormData(prev => ({ ...prev, category: val }))}
                            width="100%"
                            disableSearch={true}
                        />
                    </label>
                    <label>{t('tree_editor.base_color')}
                        <div className="form-row" style={{ position: 'relative' }}>
                            <div
                                onClick={() => setDisplayColorPicker(!displayColorPicker)}
                                style={{
                                    width: '60px',
                                    height: '38px',
                                    background: formData.color || '#ffffff',
                                    border: '2px solid rgba(255,255,255,0.5)',
                                    cursor: 'pointer',
                                    borderRadius: '4px',
                                    boxShadow: '0 0 5px rgba(0,0,0,0.5)'
                                }}
                            />
                            <input
                                type="text"
                                name="color"
                                value={formData.color || ""}
                                onChange={handleChange}
                                placeholder="#ffffff"
                                style={{ flex: 1, textTransform: 'uppercase' }}
                            />
                            {displayColorPicker && (
                                <div style={{ position: 'absolute', zIndex: 5000, top: '45px', left: '0' }}>
                                    <div
                                        style={{ position: 'fixed', top: '0px', right: '0px', bottom: '0px', left: '0px' }}
                                        onClick={() => setDisplayColorPicker(false)}
                                    />
                                    <SketchPicker
                                        color={formData.color || '#ffffff'}
                                        onChange={(color: any) => setFormData(prev => ({ ...prev, color: color.hex }))}
                                        disableAlpha={true}
                                        presetColors={['#D00000', '#FF8000', '#FFFF00', '#008000', '#0000FF', '#4B0082', '#EE82EE', '#FFFFFF', '#000000']}
                                    />
                                </div>
                            )}
                        </div>
                    </label>

                    <label>{t('tree_editor.initial_level')} <input type="number" name="initialLevel" value={formData.initialLevel} onChange={handleChange} /></label>
                    {!formData.isVanilla && (
                        <label className="checkbox-label" style={{ marginTop: '30px' }}>
                            <input type="checkbox" name="advancesPlayerLevel" checked={formData.advancesPlayerLevel || false} onChange={handleChange} />
                            {t('tree_editor.advances_player')}
                        </label>
                    )}
                    <label className="checkbox-label" style={{ marginTop: '30px' }}>
                        <input type="checkbox" name="isHidden" checked={formData.isHidden || false} onChange={handleChange} />
                        <span style={{ color: '#ff9800' }}>{t('tree_editor.is_hidden')}</span>
                    </label>
                    <label className="full-width">{t('tree_editor.bg_path')}
                        <div className="form-row">
                            <input type="text" name="bgPath" value={formData.bgPath} onChange={handleChange} placeholder={t('tree_editor.bg_placeholder')} style={{ flex: 1 }} />
                            <button className="browse-btn" onClick={() => onRequestBrowse('bgPath')}>{t('common.browse')}</button>
                        </div>
                    </label>
                    <label className="full-width">{t('tree_editor.icon_path')}
                        <div className="form-row">
                            <input type="text" name="iconPath" value={formData.iconPath} onChange={handleChange} placeholder={t('tree_editor.bg_placeholder')} style={{ flex: 1 }} />
                            <button className="browse-btn" onClick={() => onRequestBrowse('iconPath')}>{t('common.browse')}</button>
                        </div>
                    </label>
                    <label className="full-width">{t('tree_editor.default_perk_icon')}
                        <div className="form-row">
                            <input type="text" name="iconPerkPath" value={formData.iconPerkPath || ""} onChange={handleChange} placeholder={t('tree_editor.bg_placeholder')} style={{ flex: 1 }} />
                            <button className="browse-btn" onClick={() => onRequestBrowse('iconPerkPath')}>{t('common.browse')}</button>
                        </div>
                    </label>
                </div>

                {!formData.isVanilla && (
                    <div className="custom-skill-mechanics" style={{ marginTop: '20px' }}>
                        <h3 style={{ margin: '0 0 10px 0', fontSize: '1.2rem', color: '#ffd700' }}>{t('tree_editor.exp_formula')}</h3>
                        <div className="settings-grid compact-grid">
                            <label>{t('tree_editor.use_mult')} <input type="number" step="0.1" name="useMult" value={exp.useMult} onChange={handleExpChange} /></label>
                            <label>{t('tree_editor.use_offset')} <input type="number" step="0.1" name="useOffset" value={exp.useOffset} onChange={handleExpChange} /></label>
                            <label>{t('tree_editor.improve_mult')} <input type="number" step="0.1" name="improveMult" value={exp.improveMult} onChange={handleExpChange} /></label>
                            <label>{t('tree_editor.improve_offset')} <input type="number" step="0.1" name="improveOffset" value={exp.improveOffset} onChange={handleExpChange} /></label>
                        </div>
                    </div>
                )}

                <div className="dynamic-list-container" style={{ marginTop: '20px' }}>
                    <h3 style={{ margin: '0 0 10px 0', fontSize: '1.2rem', color: '#ff5252' }}>{t('tree_editor.requirements_title')}</h3>
                    {formData.treeRequirements?.map((req, idx) => (
                        <RequirementInputRow
                            key={idx}
                            req={req}
                            availableReqs={availableReqs}
                            availableTrees={availableTrees}
                            formLists={formLists}
                            onUpdate={(field, val) => {
                                setFormData(prev => {
                                    const newReqs = [...(prev.treeRequirements || [])];
                                    newReqs[idx] = { ...newReqs[idx], [field]: val };
                                    if (field === 'type') {
                                        newReqs[idx].value = '';
                                        newReqs[idx].target = '';
                                    }
                                    return { ...prev, treeRequirements: newReqs };
                                });
                            }}
                            onRemove={() => setFormData({ ...formData, treeRequirements: (formData.treeRequirements || []).filter((_, i) => i !== idx) })}
                            onSelectTarget={(type) => setSelectingReqTarget({ idx, type })}
                        />
                    ))}
                    <button className="add-btn" onClick={() => {
                        setFormData({ ...formData, treeRequirements: [...(formData.treeRequirements || []), { type: availableReqs[0]?.id || 'level', value: '' }] });
                    }} style={{ width: '200px', padding: '5px' }}>{t('tree_editor.new_req_btn')}</button>
                </div>
                <div className="modal-actions" style={{ marginTop: '25px', display: 'flex', gap: '15px', justifyContent: 'center' }}>
                    <button className="modal-btn yes-btn" onClick={() => onSave(formData)}>{t('common.save')}</button>

                    {formData.name && (
                        <button
                            className="modal-btn"
                            disabled={isDirty}
                            style={{
                                borderColor: isDirty ? '#555' : '#ff9800',
                                color: isDirty ? '#777' : '#ff9800',
                                cursor: isDirty ? 'not-allowed' : 'pointer',
                                opacity: isDirty ? 0.6 : 1
                            }}
                            onClick={handleExport}
                            title={isDirty ? t('tree_editor.save_before_export') : t('tree_editor.export_zip_tooltip')}
                        >
                            📦 {t('tree_editor.export_zip')}
                        </button>
                    )}

                    {!formData.isVanilla && formData.name && (
                        <button className="modal-btn danger-btn" onClick={() => {
                            window.dispatchEvent(new CustomEvent('requestDeleteTree', { detail: { name: formData.name } }));
                        }}>
                            🗑️ {t('tree_editor.delete_tree')}
                        </button>
                    )}

                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>

            {selectingReqTarget !== null && formLists[selectingReqTarget.type] && (
                <FormSelectorModal
                    items={formLists[selectingReqTarget.type]}
                    onClose={() => setSelectingReqTarget(null)}
                    onSelect={(id) => {
                        const newReqs = [...(formData.treeRequirements || [])];
                        newReqs[selectingReqTarget.idx] = { ...newReqs[selectingReqTarget.idx], value: id };
                        setFormData({ ...formData, treeRequirements: newReqs });
                        setSelectingReqTarget(null);
                    }}
                />
            )}
        </div>
    );
};

const svgContentCache: Record<string, string> = {};

// OTIMIZAÇÃO: Carregamento do SVG e injeção do HTML nativo para manter propriedades CSS.
const InlineSVGIcon = memo(({ src, className, alt }: { src: string, className?: string, alt?: string }) => {
    const [svgContent, setSvgContent] = useState<string | null>(svgContentCache[src] || null);

    useEffect(() => {
        if (!src) return;
        let isMounted = true;

        if (svgContentCache[src]) {
            setSvgContent(svgContentCache[src]);
            return;
        }

        fetch(src)
            .then(res => res.text())
            .then(data => {
                if (!isMounted) return;
                let finalContent = data;

                if (data.includes('<svg')) {
                    // MÁGICA AQUI: Remove width e height travados no arquivo SVG original
                    finalContent = data.replace(/(<svg[^>]*?)\s+width=(["']).*?\2/i, '$1')
                        .replace(/(<svg[^>]*?)\s+height=(["']).*?\2/i, '$1');
                    svgContentCache[src] = finalContent;
                } else {
                    // Fallback para PNG/JPG 
                    finalContent = `<img src="${src}" alt="${alt || ''}" style="width: 100%; height: 100%; object-fit: contain; pointer-events: none;" />`;
                    svgContentCache[src] = finalContent;
                }

                setSvgContent(finalContent);
            })
            .catch(err => {
                if (isMounted) {
                    const fallback = `<img src="${src}" alt="${alt || ''}" style="width: 100%; height: 100%; object-fit: contain; pointer-events: none;" />`;
                    svgContentCache[src] = fallback;
                    setSvgContent(fallback);
                }
            });

        return () => {
            isMounted = false;
        };
    }, [src, alt]);

    if (!svgContent) return null;

    return (
        <div
            className={`inline-svg-wrapper ${className || ''}`}
            dangerouslySetInnerHTML={{ __html: svgContent }}
            // Força a caixa do HTML injetado a respeitar o CSS Pai (perk-icon-img)
            style={{ pointerEvents: 'none', width: '100%', height: '100%', display: 'flex' }}
        />
    );
});

const PlayerHeader = ({ player, customResources }: { player: PlayerData, customResources?: CustomResource[] }) => {
    const [showResources, setShowResources] = useState(false);
    return (
        <div className="skyrim-header">
            <div className="header-top-row">
                <DiamondSeparator />
                <div className="header-item">
                    <span className="header-label">{t('header.name')}</span>
                    <span className="header-value">{player.name}</span>
                </div>

                <div className="header-item header-level-item">
                    <span className="header-label">{t('header.level')}</span>
                    <span className="header-value">{player.level || 32}</span>
                    <div className="level-bar-wrapper">
                        <div className="bar-fill cyan-fill" style={{ width: `${player.levelProgress || 0}%` }}></div>
                        <MiniBarFrameSVG />
                    </div>
                </div>

                <div className="header-item">
                    <span className="header-label">{t('header.race')}</span>
                    <span className="header-value">{player.race || 'Nord'}</span>
                </div>

                <div className="header-item"
                    onMouseEnter={() => setShowResources(true)}
                    onMouseLeave={() => setShowResources(false)}
                    style={{ position: 'relative', cursor: 'pointer' }}>
                    <span className="header-label">{t('header.resources', { defaultValue: 'Resources' })}</span>
                    <span className="header-value">{player.perkPoints}</span>

                    {showResources && (
                        <div className="resources-dropdown">
                            <div className="resource-item">
                                <span>{t('header.perk_points', { defaultValue: 'Perk Points' })}</span>
                                <span>{player.perkPoints}</span>
                            </div>
                            {customResources && customResources.map(res => (
                                <div key={res.id} className="resource-item">
                                    <span>{resolveText(res.name, false)}</span>
                                    <span>{player.resourceValues?.[res.id] || 0}</span>
                                </div>
                            ))}
                        </div>
                    )}
                </div>

                <div className="header-item">
                    <span className="header-label">{t('header.dragon_souls')}</span>
                    <span className="header-value">{player.dragonSouls || 0}</span>
                </div>
                <DiamondSeparator />
            </div>

            <div className="header-bottom-row">
                <div className="stat-container">
                    <span className="stat-label">{t('header.health')}</span>
                    <div className="stat-bar-wrapper main-bar">
                        <div className="bar-fill red-fill" style={{ width: `${(player.health.current / player.health.max) * 100}%` }}></div>
                        <BarFrameSVG />
                    </div>
                    <span className="stat-value">{Math.floor(player.health.current)}/{Math.floor(player.health.max)}</span>
                </div>

                <div className="stat-container">
                    <span className="stat-label">{t('header.magicka')}</span>
                    <div className="stat-bar-wrapper main-bar">
                        <div className="bar-fill blue-fill" style={{ width: `${(player.magicka.current / player.magicka.max) * 100}%` }}></div>
                        <BarFrameSVG />
                    </div>
                    <span className="stat-value">{Math.floor(player.magicka.current)}/{Math.floor(player.magicka.max)}</span>
                </div>

                <div className="stat-container">
                    <span className="stat-label">{t('header.stamina')}</span>
                    <div className="stat-bar-wrapper main-bar">
                        <div className="bar-fill green-fill" style={{ width: `${(player.stamina.current / player.stamina.max) * 100}%` }}></div>
                        <BarFrameSVG />
                    </div>
                    <span className="stat-value">{Math.floor(player.stamina.current)}/{Math.floor(player.stamina.max)}</span>
                </div>
            </div>
        </div>
    );
};

const KonvaPerkNode = memo(({ node, treeColor, iconPerkPath, width, height, setHoveredPerk, onNodeClick, hidePerkNames }: {
    node: PerkNode,
    treeColor: string,
    iconPerkPath?: string,
    width: number,
    height: number,
    setHoveredPerk?: (p: PerkNode | null) => void,
    onNodeClick?: (node: PerkNode) => void,
    hidePerkNames?: boolean
}) => {
    const groupRef = useRef<Konva.Group>(null);
    const iconGroupRef = useRef<Konva.Group>(null);

    // Calcula posições em PIXELS baseados na porcentagem
    const x = (node.x / 100) * width;
    const y = (node.y / 100) * height;

    // Resolve ícone
    const iconSource = useValidImage(node.icon || iconPerkPath, DEFAULT_PERK_ICON);
    const [image] = useCustomImage(iconSource);

    // Estados de cor
    const isMaxed = useMemo(() => {
        if (!node.isUnlocked) return false;
        if (!node.nextRanks || node.nextRanks.length === 0) return true;
        return node.nextRanks.every(rank => rank.isUnlocked);
    }, [node]);

    let iconOpacity = 0.5;

    if (isMaxed) {
        iconOpacity = 1;
    } else if (node.isUnlocked) {
        iconOpacity = 1;
    } else if (node.canUnlock) {
        iconOpacity = 0.8;
    }

    const nodeSize = 40;
    const iconSize = 24;

    // --- ANIMAÇÕES ---
    const handleMouseEnter = () => {
        document.body.style.cursor = 'pointer';
        setHoveredPerk?.(node);

        const nodeGroup = groupRef.current;
        if (nodeGroup) {
            // Removemos o cache temporariamente para a animação da sombra fluir sem cortes
            nodeGroup.clearCache();
            nodeGroup.moveToTop();
            nodeGroup.to({
                scaleX: 1.3,
                scaleY: 1.3,
                duration: 0.2,
                easing: Konva.Easings.BackEaseOut,
                shadowBlur: 15,
                shadowOpacity: 0.8
            });
        }
    };

    const handleMouseLeave = () => {
        document.body.style.cursor = 'default';
        setHoveredPerk?.(null);

        const nodeGroup = groupRef.current;
        if (nodeGroup) {
            nodeGroup.to({
                scaleX: 1,
                scaleY: 1,
                duration: 0.2,
                easing: Konva.Easings.EaseOut,
                shadowBlur: node.isUnlocked ? 10 : 0,
                shadowOpacity: 0.6,
                onFinish: () => {
                    // Reaplica o cache ao fim da animação
                    if (image && nodeGroup) {
                        nodeGroup.cache({ pixelRatio: 2, offset: 20 });
                    }
                }
            });
        }
    };

    // Aplica o cache inicial seguindo a lógica de Entrada/Saída
    useEffect(() => {

        if (!image) return;

        const iconNode = iconGroupRef.current;
        const groupNode = groupRef.current;


        if (iconNode) {
            iconNode.clearCache();
            iconNode.cache({ pixelRatio: 2, offset: 10 });
        }
        if (groupNode) {
            groupNode.clearCache();
            // O offset de 20 é fundamental para não cortar o Blur da sombra e gerar blocos cinzas
            groupNode.cache({ pixelRatio: 2, offset: 20 });
        }

        // remove o cache na saida
        return () => {
            if (iconNode) iconNode.clearCache();
            if (groupNode) groupNode.clearCache();
        };
    }, [image, isMaxed, node.isUnlocked, treeColor]);

    return (
        <Group
            ref={groupRef}
            x={x}
            y={y}
            onMouseEnter={handleMouseEnter}
            onMouseLeave={handleMouseLeave}
            onClick={(e) => {
                e.cancelBubble = true;
                onNodeClick?.(node);
            }}
        >
            {/* O fill="transparent" assegura que a sombra saiba de onde está sendo projetada sem bugar no cache */}
            <Circle
                radius={nodeSize / 2}
                fill="transparent"
                shadowColor={treeColor}
                shadowBlur={node.isUnlocked ? 20 : 0}
                shadowOpacity={node.isUnlocked ? 0.6 : 0}
            />

            {/* TRUQUE DO KONVA PARA COLORIR O SVG */}
            {image && (
                <Group ref={iconGroupRef} x={-iconSize / 2} y={-iconSize / 2}>
                    <KonvaImage
                        image={image}
                        width={iconSize}
                        height={iconSize}
                        opacity={iconOpacity}
                    />
                    {/* Se estiver no nível máximo, pintamos os pixels visíveis do SVG com a cor da árvore */}
                    {isMaxed && (
                        <Rect
                            width={iconSize}
                            height={iconSize}
                            fill={treeColor}
                            globalCompositeOperation="source-in"
                        />
                    )}
                </Group>
            )}
        </Group>
    );
});

const PerkNodeElement = memo(({ node, treeColor, iconPerkPath, isPreview, isEditorMode, isKeyboardSelected,
    setHoveredPerk, onNodeClick, uiSettings, onStartDrag, onNodeContextMenu }: {
        node: PerkNode, treeName: string, treeColor: string, iconPerkPath?: string, isPreview: boolean, isEditorMode: boolean, isKeyboardSelected?: boolean,
        setHoveredPerk?: (p: PerkNode | null) => void,
        uiSettings?: UISettings | null,
        onNodeClick?: (node: PerkNode) => void,
        onStartDrag?: (nodeId: string, e: React.MouseEvent) => void,
        onNodeContextMenu?: (e: React.MouseEvent, node: PerkNode) => void
    }) => {

    const isMaxed = useMemo(() => {
        if (!node.isUnlocked) return false;
        if (!node.nextRanks || node.nextRanks.length === 0) return true;
        return node.nextRanks.every(rank => rank.isUnlocked);
    }, [node]);

    let stateClass = "perk-node-locked";
    if (isMaxed) stateClass = "perk-node-maxed";
    else if (node.isUnlocked) stateClass = "perk-node-acquired";
    else if (node.canUnlock) stateClass = "perk-node-unlocked";

    const styleProps = { '--tree-glow': treeColor } as React.CSSProperties;
    const nodeClass = `perk-node-container ${stateClass} ${isPreview ? 'preview-mode' : ''} ${isKeyboardSelected ? 'forced-hover' : ''}`;
    let iconSource = node.icon;
    if (!iconSource || iconSource.trim() === "") {
        iconSource = iconPerkPath || DEFAULT_PERK_ICON;
    }
    const iconImage = useValidImage(iconSource, DEFAULT_PERK_ICON);
    const displayName = resolveText(node.name || t('common.unknown'), isEditorMode);

    return (
        <div
            className={nodeClass}
            style={{ ...styleProps, left: `${node.x}%`, top: `${node.y}%` }}
            onMouseEnter={() => {
                if (!isPreview && !isEditorMode) {
                    setHoveredPerk?.(node);
                    playSound('UISkillsFocusSD');
                }
            }}
            onMouseLeave={() => !isPreview && !isEditorMode && setHoveredPerk?.(null)}
            onMouseDown={(e) => {
                if (isEditorMode && !isPreview && onStartDrag) {
                    if (e.button === 0) {
                        e.stopPropagation();
                        onStartDrag(node.id, e);
                    }
                }
            }}
            onMouseUp={(e) => {
                if (isEditorMode && !isPreview && e.button === 2 && onNodeContextMenu) {
                    e.preventDefault();
                    e.stopPropagation();
                    onNodeContextMenu(e, node);
                }
            }}
            onContextMenu={(e) => e.preventDefault()}
            onClick={(e) => {
                e.stopPropagation();
                if (onNodeClick) onNodeClick(node);
            }}
        >
            <div className="perk-icon-img">
                <InlineSVGIcon src={iconImage} alt={displayName} />
            </div>
            {!isPreview && (uiSettings ? !uiSettings.hidePerkNames : true) && (
                <div className="perk-node-label" style={{ opacity: node.isUnlocked || node.canUnlock ? 1 : 0.6 }}>
                    {displayName.toUpperCase()}
                </div>
            )}
            {node.nextRanks && node.nextRanks.length > 0 && (
                <div className="perk-ranks-indicator">
                    {Array.from({ length: 1 + node.nextRanks.length }).map((_, i) => {
                        const isRankUnlocked = i === 0 ? node.isUnlocked : node.nextRanks![i - 1].isUnlocked;
                        return <div key={i} className={`rank-dot ${i === 0 ? 'rank-parent' : 'rank-child'} ${isRankUnlocked ? 'rank-unlocked' : ''}`} style={styleProps} />
                    })}
                </div>
            )}
        </div>
    );
});

const TreeCanvasLayer = memo(({ nodes, treeColor, width, height, bounds }: {
    nodes: PerkNode[], treeColor: string, width: number, height: number,
    bounds: { minX: number, maxX: number, minY: number, maxY: number }
}) => {
    if (width === 0 || height === 0) return null;
    const worldWidth = (bounds.maxX - bounds.minX + 20) * (width / 100);
    const worldHeight = (bounds.maxY - bounds.minY + 20) * (height / 100);

    return (
        <Stage
            width={worldWidth}
            height={worldHeight}
            style={{
                position: 'absolute',
                left: `${bounds.minX * (width / 100)}px`,
                top: `${bounds.minY * (height / 100)}px`,
                pointerEvents: 'none' // Linhas não devem bloquear o mouse dos nodos HTML
            }}
        >
            <Layer>
                {nodes.map(node => node.links.map(targetId => {
                    const targetNode = nodes.find(n => n.id === targetId);
                    if (!targetNode) return null;
                    const isCompleted = node.isUnlocked && targetNode.isUnlocked;
                    const isPathRevealed = node.isUnlocked;

                    const x1 = ((node.x - bounds.minX) / 100) * width;
                    const y1 = ((node.y - bounds.minY) / 100) * height;
                    const x2 = ((targetNode.x - bounds.minX) / 100) * width;
                    const y2 = ((targetNode.y - bounds.minY) / 100) * height;

                    return (
                        <Line
                            key={`link-${node.id}-${targetId}`}
                            points={[x1, y1, x2, y2]}
                            stroke={isCompleted ? treeColor : (isPathRevealed ? 'rgba(255, 255, 255, 0.5)' : 'rgba(255, 255, 255, 0.1)')}
                            strokeWidth={isCompleted ? 4 : 2}
                            dash={isCompleted ? [] : [5, 5]}
                            listening={false}
                        />
                    );
                }))}
            </Layer>
        </Stage>
    );
});


const TreeConnections = memo(({ nodes, treeColor, width, height }: {
    nodes: PerkNode[], treeColor: string, width: number, height: number
}) => {
    if (width === 0 || height === 0) return null;
    return (
        <Stage
            width={width}
            height={height}
            style={{ position: 'absolute', top: 0, left: 0, pointerEvents: 'none' }}
        >
            <Layer>
                {nodes.map(node => node.links.map(targetId => {
                    const targetNode = nodes.find(n => n.id === targetId);
                    if (!targetNode) return null;

                    const isSourceUnlocked = node.isUnlocked;
                    const isTargetUnlocked = targetNode.isUnlocked;
                    const isCompleted = isSourceUnlocked && isTargetUnlocked;
                    const isPathRevealed = isSourceUnlocked;

                    // Calculate pixel coordinates from percentages
                    const x1 = (node.x / 100) * width;
                    const y1 = (node.y / 100) * height;
                    const x2 = (targetNode.x / 100) * width;
                    const y2 = (targetNode.y / 100) * height;

                    return (
                        <Line
                            key={`${node.id}-${targetId}`}
                            points={[x1, y1, x2, y2]}
                            stroke={isCompleted ? treeColor : (isPathRevealed ? 'rgba(255, 255, 255, 0.5)' : 'rgba(255, 255, 255, 0.1)')}
                            strokeWidth={isCompleted ? 4 : 2}
                            dash={isCompleted ? [] : [5, 5]} // Konva uses array for dash, SVG used string "5,5"
                            lineCap="round"
                            lineJoin="round"
                            listening={false} // Performance optimization: lines don't need mouse events
                        />
                    );
                }))}
            </Layer>
        </Stage>
    );
});

const TreeVisualNodes = memo(({ treeData, isPreview, isEditorMode,
    setHoveredPerk, onNodeClick, uiSettings, containerWidth, containerHeight }: {
        treeData: SkillTreeData,
        isPreview: boolean,
        isEditorMode: boolean,
        containerWidth: number,
        containerHeight: number,
        uiSettings?: UISettings | null,
        setHoveredPerk?: (p: PerkNode | null) => void,
        onNodeClick?: (node: PerkNode) => void
    }) => {

    const { nodes, color, name, iconPerkPath } = treeData;
    const treeColor = color || DEFAULT_COLOR;

    if (containerWidth === 0 || containerHeight === 0) return null;

    return (
        <Stage width={containerWidth} height={containerHeight} style={{ pointerEvents: 'none' }}>
            <Layer>
                {/* 1. Renderiza as linhas via Canvas */}
                {nodes.map(node => node.links.map(targetId => {
                    const targetNode = nodes.find(n => n.id === targetId);
                    if (!targetNode) return null;

                    const isCompleted = node.isUnlocked && targetNode.isUnlocked;
                    const isPathRevealed = node.isUnlocked;

                    const x1 = (node.x / 100) * containerWidth;
                    const y1 = (node.y / 100) * containerHeight;
                    const x2 = (targetNode.x / 100) * containerWidth;
                    const y2 = (targetNode.y / 100) * containerHeight;

                    return (
                        <Line
                            key={`${node.id}-${targetId}`}
                            points={[x1, y1, x2, y2]}
                            stroke={isCompleted ? treeColor : (isPathRevealed ? 'rgba(255, 255, 255, 0.5)' : 'rgba(255, 255, 255, 0.1)')}
                            strokeWidth={isCompleted ? 4 : 2}
                            dash={isCompleted ? [] : [5, 5]}
                            lineCap="round"
                            lineJoin="round"
                            listening={false}
                        />
                    );
                }))}

                {/* 2. Renderiza os Nodos usando o Konva na Preview */}
                {nodes.map(node => (
                    <KonvaPerkNode
                        key={`knode-${node.id}`}
                        node={node}
                        treeColor={treeColor}
                        iconPerkPath={iconPerkPath}
                        width={containerWidth}
                        height={containerHeight}
                        setHoveredPerk={setHoveredPerk}
                        onNodeClick={onNodeClick}
                        hidePerkNames={uiSettings?.hidePerkNames}
                    />
                ))}
            </Layer>
        </Stage>
    );
});

const SettingsModal = ({ settings, rules, customResources, formLists, onClose, onSaveSettings, onSaveRules, onSaveResources, onDeleteResource, onResetAllPerks }: {
    settings: SettingsData, rules: LevelRule[], customResources: CustomResource[], formLists: Record<string, AvailablePerk[]>, onClose: () => void,
    onSaveSettings: (s: SettingsData) => void, onSaveRules: (r: LevelRule[]) => void, onSaveResources: (res: CustomResource[]) => void, onDeleteResource: (id: string) => void, onResetAllPerks: () => void
}) => {
    const [settingsData, setSettingsData] = useState<SettingsData>(JSON.parse(JSON.stringify(settings)));
    const [rulesData, setRulesData] = useState<LevelRule[]>(JSON.parse(JSON.stringify(rules || [])));

    const [activeTab, setActiveTab] = useState<'base' | 'rules' | 'codes' | 'categories' | 'resources'>('base');
    const [resourcesData, setResourcesData] = useState<CustomResource[]>(JSON.parse(JSON.stringify(customResources || [])));
    const [newCatName, setNewCatName] = useState("");

    const [selectingGlobalIdx, setSelectingGlobalIdx] = useState<number | null>(null);

    const handleBaseChange = (e: React.ChangeEvent<HTMLInputElement>) => {
        const { name, value, type, checked } = e.target;
        setSettingsData(prev => ({
            ...prev,
            base: { ...prev.base, [name]: type === 'checkbox' ? checked : Number(value) }
        }));
    };

    const addRule = () => setRulesData(prev => [...prev, { level: 2 }]);
    const removeRule = (index: number) => setRulesData(prev => prev.filter((_, i) => i !== index));
    const handleRuleChange = (index: number, field: keyof LevelRule, value: any) => {
        setRulesData(prev => {
            const newRules = [...prev];
            if (value === "" || value === null) {
                delete newRules[index][field];
            } else {
                newRules[index] = {
                    ...newRules[index],
                    [field]: (typeof value === 'boolean') ? value : Number(value)
                };
            }
            return newRules;
        });
    };

    const addCode = () => setSettingsData(prev => ({ ...prev, codes: [...(prev.codes || []), { code: t('settings.codes.default_code'), maxUses: 1, currentUses: 0, rewards: {} }] }));
    const removeCode = (idx: number) => setSettingsData(prev => ({ ...prev, codes: prev.codes.filter((_, i) => i !== idx) }));
    const handleCodeChange = (index: number, field: keyof CodeData, value: any) => {
        setSettingsData(prev => {
            const newCodes = [...prev.codes];
            newCodes[index] = { ...newCodes[index], [field]: value };
            return { ...prev, codes: newCodes };
        });
    };

    const handleRewardChange = (index: number, field: keyof Reward, value: string) => {
        setSettingsData(prev => {
            const newCodes = [...prev.codes];
            if (!newCodes[index].rewards) newCodes[index].rewards = {};
            if (value === "") delete newCodes[index].rewards[field];
            else newCodes[index].rewards[field] = Number(value);
            return { ...prev, codes: newCodes };
        });
    };

    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 2000 }}>
            <div className="skyrim-modal-content settings-modal-content ui-mode">
                <h2>{t('settings.title')}</h2>

                <div className="settings-tabs">
                    <button className={activeTab === 'base' ? 'active' : ''} onClick={() => setActiveTab('base')}>{t('settings.tabs.base')}</button>
                    <button className={activeTab === 'rules' ? 'active' : ''} onClick={() => setActiveTab('rules')}>{t('settings.tabs.rules')}</button>
                    <button className={activeTab === 'categories' ? 'active' : ''} onClick={() => setActiveTab('categories')}>{t('settings.tabs.categories')}</button>
                    <button className={activeTab === 'resources' ? 'active' : ''} onClick={() => setActiveTab('resources')}>{t('settings.tabs.resources')}</button>
                    <button className={activeTab === 'codes' ? 'active' : ''} onClick={() => setActiveTab('codes')}>{t('settings.tabs.codes')}</button>
                </div>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px', marginTop: '10px' }}></div>

                <div className="settings-tab-content">
                    {activeTab === 'base' && (
                        <div className="settings-grid">
                            <label>{t('settings.base.perks_per_level')} <input type="number" name="perksPerLevel" value={settingsData.base.perksPerLevel} onChange={handleBaseChange} /></label>
                            <label>{t('settings.base.skill_cap')} <input type="number" name="skillCap" value={settingsData.base.skillCap || 100} onChange={handleBaseChange} style={{ borderColor: '#ffd700' }} /></label>
                            <label className="checkbox-label" style={{ marginTop: '10px' }}>
                                <input
                                    type="checkbox"
                                    name="useDynamicSkillCap"
                                    checked={settingsData.base.useDynamicSkillCap !== false}
                                    onChange={handleBaseChange}
                                />
                                {t('settings.base.use_dynamic_cap')}
                            </label>

                            {settingsData.base.useDynamicSkillCap !== false && (
                                <div className="settings-grid compact-grid" style={{ gridColumn: 'span 2', marginLeft: '10px', paddingLeft: '10px', borderLeft: '2px solid rgba(255,255,255,0.1)', marginTop: '5px', marginBottom: '15px' }}>
                                    <label>{t('settings.base.cap_level_mult')} <input type="number" step="0.1" name="skillCapPerLevelMult" value={settingsData.base.skillCapPerLevelMult ?? 2.0} onChange={handleBaseChange} /></label>
                                    <label className="checkbox-label" style={{ gridColumn: 'span 2' }}>
                                        <input
                                            type="checkbox"
                                            name="applyRacialBonusToCap"
                                            checked={settingsData.base.applyRacialBonusToCap !== false}
                                            onChange={handleBaseChange}
                                        />
                                        {t('settings.base.apply_racial_bonus_to_cap')}
                                    </label>
                                </div>
                            )}
                            <label className="checkbox-label" style={{ marginTop: '10px' }}>
                                <input
                                    type="checkbox"
                                    name="enableLegendary"
                                    checked={settingsData.base.enableLegendary !== false}
                                    onChange={handleBaseChange}
                                />
                                {t('settings.base.enable_legendary')}
                            </label>
                            <label className="checkbox-label">
                                <input
                                    type="checkbox"
                                    name="refillAttributesOnLevelUp"
                                    checked={settingsData.base.refillAttributesOnLevelUp || false}
                                    onChange={handleBaseChange}
                                />
                                {t('settings.base.refill_attributes')}
                            </label>
                            <label className="checkbox-label" style={{ marginTop: '10px' }}>
                                <input
                                    type="checkbox"
                                    name="useBaseSkillLevel"
                                    checked={settingsData.base.useBaseSkillLevel !== false}
                                    onChange={handleBaseChange}
                                />
                                {t('settings.base.use_base_skill_level')}
                            </label>
                            <label className="checkbox-label" style={{ marginTop: '10px' }}>
                                <input
                                    type="checkbox"
                                    name="applyVanillaInitialLevels"
                                    checked={settingsData.base.applyVanillaInitialLevels !== false}
                                    onChange={handleBaseChange}
                                />
                                {t('settings.base.apply_vanilla_initial')}
                            </label>
                            <label>{t('settings.base.skill_points_per_level')} <input type="number" name="skillPointsPerLevel" value={settingsData.base.skillPointsPerLevel} onChange={handleBaseChange} /></label>
                            <label>{t('settings.base.max_spendable')} <input type="number" name="maxSkillPointsSpendablePerLevel" value={settingsData.base.maxSkillPointsSpendablePerLevel} onChange={handleBaseChange} /></label>
                            <label>{t('header.health')} (+): <input type="number" name="healthIncrease" value={settingsData.base.healthIncrease} onChange={handleBaseChange} /></label>
                            <label>{t('header.magicka')} (+): <input type="number" name="magickaIncrease" value={settingsData.base.magickaIncrease} onChange={handleBaseChange} /></label>
                            <label>{t('header.stamina')} (+): <input type="number" name="staminaIncrease" value={settingsData.base.staminaIncrease} onChange={handleBaseChange} /></label>
                            <div style={{ gridColumn: 'span 2', marginTop: '15px', borderTop: '1px solid rgba(255,255,255,0.1)', paddingTop: '15px' }}>
                                <h3 style={{ margin: '0 0 10px 0', fontSize: '1.1rem', color: '#4dd0e1' }}>{t('settings.base.carry_weight_title')}</h3>
                                <div className="settings-grid">
                                    <label>{t('settings.base.cw_method')}
                                        <CustomSelect
                                            options={[
                                                { value: 'none', label: t('settings.base.cw_method_none') },
                                                { value: 'auto', label: t('settings.base.cw_method_auto') },
                                                { value: 'linked', label: t('settings.base.cw_method_linked') }
                                            ]}
                                            value={settingsData.base.carryWeightMethod || 'none'}
                                            onChange={(val) => setSettingsData(prev => ({ ...prev, base: { ...prev.base, carryWeightMethod: val } }))}
                                            width="100%"
                                            disableSearch={true}
                                        />
                                    </label>
                                    <label>{t('settings.base.cw_increase')} <input type="number" name="carryWeightIncrease" value={settingsData.base.carryWeightIncrease || 0} onChange={handleBaseChange} /></label>

                                    {settingsData.base.carryWeightMethod === 'linked' && (
                                        <div style={{ gridColumn: 'span 2', display: 'flex', gap: '15px', alignItems: 'center' }}>
                                            <span style={{ color: '#ccc' }}>{t('settings.base.cw_linked_title')}</span>
                                            {['Health', 'Magicka', 'Stamina'].map(attr => (
                                                <label key={attr} className="checkbox-label" style={{ margin: 0 }}>
                                                    <input
                                                        type="checkbox"
                                                        checked={(settingsData.base.carryWeightLinkedAttributes || []).includes(attr)}
                                                        onChange={(e) => {
                                                            const current = settingsData.base.carryWeightLinkedAttributes || [];
                                                            const next = e.target.checked ? [...current, attr] : current.filter(a => a !== attr);
                                                            setSettingsData(prev => ({ ...prev, base: { ...prev.base, carryWeightLinkedAttributes: next } }));
                                                        }}
                                                    />
                                                    {t(`header.${attr.toLowerCase()}`)}
                                                </label>
                                            ))}
                                        </div>
                                    )}
                                </div>
                            </div>

                            <div style={{ gridColumn: 'span 2', marginTop: '20px', borderTop: '1px solid rgba(255,255,255,0.1)', paddingTop: '20px' }}>
                                <button className="modal-btn danger-btn" style={{ width: '100%' }} onClick={onResetAllPerks}>
                                    {t('settings.base.reset_all_btn')}
                                </button>
                            </div>
                        </div>
                    )}

                    {activeTab === 'rules' && (
                        <div className="dynamic-list-container">
                            <p className="tab-desc">{t('settings.rules.desc')}</p>
                            {rulesData.map((rule, idx) => (
                                <div key={idx} className="dynamic-card">
                                    <div className="card-header">
                                        <h4>{t('settings.rules.level_label')} <input type="number" className="inline-input" value={rule.level} onChange={e => handleRuleChange(idx, 'level', e.target.value)} style={{ width: '60px' }} /></h4>
                                        <button className="delete-btn" onClick={() => removeRule(idx)}>{t('common.delete')}</button>
                                    </div>
                                    <div className="settings-grid compact-grid">
                                        <label>{t('settings.rules.skill_cap')} <input type="number" placeholder={t('common.base')} value={rule.skillCap ?? ''} onChange={e => handleRuleChange(idx, 'skillCap', e.target.value)} style={{ borderColor: '#ffd700' }} /></label>
                                        <label className="checkbox-label" style={{ gridColumn: 'span 2', marginTop: '10px' }}>
                                            <input type="checkbox" checked={rule.useDynamicSkillCap !== false} onChange={e => handleRuleChange(idx, 'useDynamicSkillCap', e.target.checked)} />
                                            {t('settings.rules.use_dynamic_cap')}
                                        </label>
                                        <label>{t('settings.rules.skill_points')} <input type="number" placeholder={t('common.base')} value={rule.skillPointsPerLevel ?? ''} onChange={e => handleRuleChange(idx, 'skillPointsPerLevel', e.target.value)} /></label>
                                        <label>{t('settings.rules.max_spend')} <input type="number" placeholder={t('common.base')} value={rule.maxSkillPointsSpendablePerLevel ?? ''} onChange={e => handleRuleChange(idx, 'maxSkillPointsSpendablePerLevel', e.target.value)} /></label>
                                        <label>{t('settings.rules.perks_bonus')} <input type="number" placeholder={t('common.base')} value={rule.perksPerLevel ?? ''} onChange={e => handleRuleChange(idx, 'perksPerLevel', e.target.value)} /></label>
                                        <label>{t('settings.rules.health_bonus')} <input type="number" placeholder={t('common.base')} value={rule.healthIncrease ?? ''} onChange={e => handleRuleChange(idx, 'healthIncrease', e.target.value)} /></label>
                                        <label>{t('settings.rules.magicka_bonus')} <input type="number" placeholder={t('common.default')} value={rule.magickaIncrease ?? ''} onChange={e => handleRuleChange(idx, 'magickaIncrease', e.target.value)} /></label>
                                        <label>{t('settings.rules.stamina_bonus')} <input type="number" placeholder={t('common.default')} value={rule.staminaIncrease ?? ''} onChange={e => handleRuleChange(idx, 'staminaIncrease', e.target.value)} /></label>
                                        <label>{t('settings.rules.cw_bonus')} <input type="number" placeholder={t('common.default')} value={rule.carryWeightIncrease ?? ''} onChange={e => handleRuleChange(idx, 'carryWeightIncrease', e.target.value)} /></label>
                                    </div>
                                </div>
                            ))}
                            <button className="add-btn" onClick={addRule}>{t('settings.rules.add_rule')}</button>
                        </div>
                    )}

                    {activeTab === 'categories' && (
                        <div className="dynamic-list-container">
                            <p className="tab-desc">{t('settings.categories.desc')}</p>
                            <div style={{ display: 'flex', gap: '10px', marginBottom: '20px' }}>
                                <input type="text" placeholder={t('settings.categories.new_placeholder')} value={newCatName} onChange={e => setNewCatName(e.target.value)} style={{ flex: 1, padding: '10px', background: 'rgba(0,0,0,0.5)', color: 'white', border: '1px solid #4dd0e1' }} />
                                <button className="add-btn" style={{ margin: 0, padding: '10px 20px' }} onClick={() => {
                                    if (newCatName.trim() && !settingsData.categories?.includes(newCatName.trim())) {
                                        setSettingsData({ ...settingsData, categories: [...(settingsData.categories || []), newCatName.trim()] });
                                        setNewCatName("");
                                    }
                                }}>{t('settings.categories.add_btn')}</button>
                            </div>
                            {settingsData.categories?.map((cat, idx) => (
                                <div key={idx} style={{ display: 'flex', justifyContent: 'space-between', background: 'rgba(255,255,255,0.05)', padding: '10px', border: '1px solid rgba(255,255,255,0.1)' }}>
                                    <span style={{ fontSize: '1.2rem', color: '#ffd700' }}>{cat}</span>
                                    <button className="delete-btn" onClick={() => setSettingsData({ ...settingsData, categories: settingsData.categories.filter((_, i) => i !== idx) })}>{t('common.delete')}</button>
                                </div>
                            ))}
                        </div>
                    )}

                    {activeTab === 'resources' && (
                        <div className="dynamic-list-container">
                            <p className="tab-desc">{t('settings.resources.desc', { defaultValue: 'Crie Globals customizados que servirão como Moeda ao comprar Perks.' })}</p>
                            {resourcesData.map((res, idx) => (
                                <div key={idx} className="dynamic-card settings-grid compact-grid">
                                    <label>{t('settings.resources.unique_id', { defaultValue: 'Unique ID' })} <input type="text" value={res.id} onChange={e => { const r = [...resourcesData]; r[idx].id = e.target.value; setResourcesData(r); }} disabled={customResources.some(cr => cr.id === res.id)} /></label>
                                    <label>{t('settings.resources.display_name', { defaultValue: 'Display Name' })} <input type="text" value={res.name} onChange={e => { const r = [...resourcesData]; r[idx].name = e.target.value; setResourcesData(r); }} /></label>

                                    <label>{t('settings.resources.glob_id', { defaultValue: 'Glob ID' })}
                                        <button className="form-selector-trigger-btn" onClick={() => setSelectingGlobalIdx(idx)}>
                                            {res.glob ? (formLists['global']?.find(g => g.id === res.glob)?.name || res.glob) : t('settings.resources.select_glob', { defaultValue: 'Selecione uma Variável Global' })}
                                        </button>
                                    </label>

                                    <button className="delete-btn" style={{ gridColumn: 'span 2' }} onClick={() => {
                                        if (window.confirm(t('settings.resources.delete_confirm', { defaultValue: 'Remover este recurso permanentemente?' }))) {
                                            onDeleteResource(res.id);
                                            setResourcesData(resourcesData.filter((_, i) => i !== idx));
                                        }
                                    }}>{t('common.delete')}</button>
                                </div>
                            ))}
                            <button className="add-btn" onClick={() => setResourcesData([...resourcesData, { id: `res_${Date.now()}`, name: 'New Resource', glob: '' }])}>{t('settings.resources.add_btn', { defaultValue: 'Adicionar Recurso' })}</button>
                        </div>
                    )}

                    {activeTab === 'codes' && (
                        <div className="dynamic-list-container">
                            <p className="tab-desc">{t('settings.codes.desc')}</p>
                            {settingsData.codes?.map((codeObj, idx) => (
                                <div key={idx} className="dynamic-card">
                                    <div className="card-header">
                                        <input type="text" className="code-title-input" value={codeObj.code} onChange={e => handleCodeChange(idx, 'code', e.target.value)} placeholder={t('settings.codes.code_placeholder')} />
                                        <button className="delete-btn" onClick={() => removeCode(idx)}>{t('common.delete')}</button>
                                    </div>
                                    <div className="settings-grid compact-grid">
                                        <label>{t('settings.codes.max_uses')} <input type="number" value={codeObj.maxUses} onChange={e => handleCodeChange(idx, 'maxUses', Number(e.target.value))} /></label>
                                        <label className="checkbox-label" style={{ marginTop: '20px' }}>
                                            <input type="checkbox" checked={codeObj.isEditorCode || false} onChange={e => handleCodeChange(idx, 'isEditorCode', e.target.checked)} />
                                            {t('settings.codes.is_editor')}
                                        </label>

                                        <h5 className="rewards-title" style={{ gridColumn: 'span 2' }}>{t('settings.codes.rewards_title')}</h5>
                                        <label>{t('header.perk_points')} (+): <input type="number" placeholder="0" value={codeObj.rewards?.perkPoints ?? ''} onChange={e => handleRewardChange(idx, 'perkPoints', e.target.value)} /></label>
                                        <label>{t('header.health')} (+): <input type="number" placeholder="0" value={codeObj.rewards?.health ?? ''} onChange={e => handleRewardChange(idx, 'health', e.target.value)} /></label>
                                        <label>{t('header.magicka')} (+): <input type="number" placeholder="0" value={codeObj.rewards?.magicka ?? ''} onChange={e => handleRewardChange(idx, 'magicka', e.target.value)} /></label>
                                        <label>{t('header.stamina')} (+): <input type="number" placeholder="0" value={codeObj.rewards?.stamina ?? ''} onChange={e => handleRewardChange(idx, 'stamina', e.target.value)} /></label>
                                    </div>
                                </div>
                            ))}
                            <button className="add-btn" onClick={addCode}>{t('settings.codes.add_code')}</button>
                        </div>
                    )}

                </div>

                <div className="modal-actions" style={{ marginTop: '25px' }}>
                    <button className="modal-btn yes-btn" onClick={() => {
                        onSaveSettings(settingsData);
                        onSaveRules(rulesData);
                        onSaveResources(resourcesData);
                    }}>{t('common.save')}</button>
                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>
            {selectingGlobalIdx !== null && formLists['global'] && (
                <FormSelectorModal
                    items={formLists['global']}
                    onClose={() => setSelectingGlobalIdx(null)}
                    onSelect={(id) => {
                        const r = [...resourcesData];
                        r[selectingGlobalIdx].glob = id;
                        setResourcesData(r);
                        setSelectingGlobalIdx(null);
                    }}
                />
            )}
        </div>
    );
};

const SingleSkillTreeSlide = memo(({ treeData, isEditorMode,
    uiSettings, keyboardSelectedNodeId, onUpdateNodePosition,
    onUpdateNodes, onNodeClick, onTreeContextMenu, formLists,
    availableReqs, availableTrees, onRequestBrowse, onLegendary, globalSettings, playerData, customResources }: { 
        treeData: SkillTreeData,
        isEditorMode: boolean,
        keyboardSelectedNodeId?: string | null,
        onUpdateNodePosition: (t: string, n: string, x: number, y: number) => void,
        onUpdateNodes?: (t: string, nodes: PerkNode[]) => void,
        onNodeClick?: (node: PerkNode) => void,
        onTreeContextMenu?: (e: React.MouseEvent, name: string) => void,
        globalSettings?: SettingsData | null,
        uiSettings?: UISettings | null,
        formLists?: Record<string, AvailablePerk[]>,
        availableReqs: RequirementDef[],
        availableTrees: string[],
        onRequestBrowse: (field: string) => void,
        onLegendary?: (treeName: string) => void,
        playerData: PlayerData | null, 
        customResources: CustomResource[] 
    }) => {
    const detailBg = useValidImage(treeData.bgPath, DEFAULT_BG);
    const treeColor = treeData.color || DEFAULT_COLOR;
    const [treeBounds, setTreeBounds] = useState({ minX: 0, maxX: 100, minY: 0, maxY: 100 });
    const containerRef = useRef<HTMLDivElement>(null);
    const { width: containerWidth, height: containerHeight } = useElementSize(containerRef);
    const [zoom, setZoom] = useState(1);
    const [pan, setPan] = useState({ x: 0, y: 0 });

    const [isDraggingCanvas, setIsDraggingCanvas] = useState(false);
    const dragStartCanvas = useRef({ x: 0, y: 0 });
    const isDraggingTree = useRef(false);
    const dragStartTree = useRef({ x: 0, y: 0 });
    const initialTreeNodesRef = useRef<PerkNode[]>([]);

    const [hoveredPerk, setHoveredPerk] = useState<PerkNode | null>(null);
    const hoverTimeoutRef = useRef<any>(null);

    const [draggingNodeId, setDraggingNodeId] = useState<string | null>(null);
    const hasDraggedNode = useRef(false);

    const [tooltipRankIdx, setTooltipRankIdx] = useState(0);
    const [isHoveringTooltip, setIsHoveringTooltip] = useState(false);
    const stickyNodeRef = useRef<PerkNode | null>(null);

    const [contextMenu, setContextMenu] = useState<{ x: number, y: number, nodeId: string } | null>(null);
    const [connectingFrom, setConnectingFrom] = useState<string | null>(null);
    const [editingNode, setEditingNode] = useState<{ node: Partial<PerkNode>, sourceNodeId?: string } | null>(null);
    const hardCap = globalSettings?.base.skillCap || 100;
    const isLegendaryEnabled = globalSettings?.base.enableLegendary !== false;
    const canLegendary = isLegendaryEnabled && treeData.currentLevel >= hardCap;
    const resetLevel = treeData.initialLevel || 15;
    const resolvedTreeName = resolveText(treeData.displayName || treeData.name, isEditorMode);

    useEffect(() => {
        // Mantemos apenas o cálculo dos limites para o Canvas não "cortar" os ícones
        if (treeData.nodes.length === 0) return;

        let minX = 0, maxX = 100, minY = 0, maxY = 100;

        treeData.nodes.forEach(node => {
            if (node.x < minX) minX = node.x;
            if (node.x > maxX) maxX = node.x;
            if (node.y < minY) minY = node.y;
            if (node.y > maxY) maxY = node.y;
        });

        setTreeBounds({ minX, maxX, minY, maxY });

        setZoom(1);
        setPan({ x: 0, y: 0 });

    }, [treeData.name]); // Roda apenas quando muda de árvore

    useEffect(() => {
        if (hoveredPerk) {
            stickyNodeRef.current = hoveredPerk;
            let nextIndex = 0;
            if (hoveredPerk.isUnlocked) {
                nextIndex = 1;
                if (hoveredPerk.nextRanks) {
                    for (const rank of hoveredPerk.nextRanks) {
                        if (rank.isUnlocked) nextIndex++;
                        else break;
                    }
                }
            }
            const totalRanks = 1 + (hoveredPerk.nextRanks?.length || 0);
            setTooltipRankIdx(Math.min(nextIndex, totalRanks - 1));
        }
    }, [hoveredPerk]);

    const activeTooltipNode = hoveredPerk ||
        (isHoveringTooltip ? stickyNodeRef.current : null) ||
        (keyboardSelectedNodeId ? treeData.nodes.find(n => n.id === keyboardSelectedNodeId) : null);

    const handleTooltipWheel = useCallback((e: React.WheelEvent) => {
        if (!activeTooltipNode) return;
        const totalRanks = 1 + (activeTooltipNode.nextRanks?.length || 0);
        if (totalRanks <= 1) return;
        e.stopPropagation();
        if (e.deltaY > 0) setTooltipRankIdx(prev => Math.min(totalRanks - 1, prev + 1));
        else if (e.deltaY < 0) setTooltipRankIdx(prev => Math.max(0, prev - 1));
    }, [activeTooltipNode]);

    useEffect(() => {
        const handleClick = () => setContextMenu(null);
        window.addEventListener('click', handleClick);
        return () => window.removeEventListener('click', handleClick);
    }, []);

    const handleWheel = useCallback((e: any) => {
        if ((e.target as HTMLElement).closest('.perk-tooltip')) {
            return;
        }
        e.preventDefault();
        const scaleBy = 1.1;
        const newScale = e.deltaY < 0 ? zoom * scaleBy : zoom / scaleBy;
        const clampedScale = Math.max(0.2, Math.min(newScale, 3));

        if (containerRef.current && containerRef.current.parentElement) {
            const rect = containerRef.current.parentElement.getBoundingClientRect();


            const mouseX = e.clientX - rect.left;
            const mouseY = e.clientY - rect.top;


            const mousePointTo = {
                x: (mouseX - pan.x) / zoom,
                y: (mouseY - pan.y) / zoom,
            };

            setZoom(clampedScale);
            setPan({
                x: mouseX - mousePointTo.x * clampedScale,
                y: mouseY - mousePointTo.y * clampedScale,
            });
        }
    }, [zoom, pan]);

    const handleCanvasMouseDown = (e: React.MouseEvent) => {
        const isNodeClick = (e.target as HTMLElement).closest('.perk-node-container');
        if (isNodeClick) return;

        // --- NOVA LÓGICA: MOVER A ÁRVORE INTEIRA (Shift + Left Click) ---
        if (isEditorMode && e.button === 0 && e.shiftKey) {
            isDraggingTree.current = true;
            dragStartTree.current = { x: e.clientX, y: e.clientY };
            // Salva as posições originais antes de começar a arrastar para evitar "drift" (acúmulo de erros de cálculo)
            initialTreeNodesRef.current = JSON.parse(JSON.stringify(treeData.nodes));
            setContextMenu(null);
            e.preventDefault();
            return;
        }

        const isPanningAction = e.button === 1 || (e.button === 0 && e.ctrlKey) || (e.button === 0 && isEditorMode);

        if (isPanningAction) {
            setIsDraggingCanvas(true);
            dragStartCanvas.current = { x: e.clientX - pan.x, y: e.clientY - pan.y };
            setContextMenu(null);
            if (e.ctrlKey) e.preventDefault();
        }
    };

    const handleCanvasMouseMove = (e: React.MouseEvent) => {
        // --- NOVA LÓGICA: APLICAR O ARRASTO NA ÁRVORE INTEIRA ---
        if (isDraggingTree.current && containerRef.current && isEditorMode) {
            const containerRect = containerRef.current.parentElement!.getBoundingClientRect();

            // Calcula o quanto o mouse moveu em pixels desde o início do clique
            const dxPixels = e.clientX - dragStartTree.current.x;
            const dyPixels = e.clientY - dragStartTree.current.y;

            // Converte esse movimento de pixels para porcentagem (%) baseada no zoom
            const dxPercent = (dxPixels / (containerRect.width * zoom)) * 100;
            const dyPercent = (dyPixels / (containerRect.height * zoom)) * 100;

            // Aplica a diferença às posições originais salvas
            const newNodes = initialTreeNodesRef.current.map(node => ({
                ...node,
                x: node.x + dxPercent,
                y: node.y + dyPercent
            }));

            // Atualiza o estado global da árvore em tempo real
            if (onUpdateNodes) {
                onUpdateNodes(treeData.name, newNodes);
            }
            return;
        }

        if (isDraggingCanvas) {
            setPan({ x: e.clientX - dragStartCanvas.current.x, y: e.clientY - dragStartCanvas.current.y });
            return;
        }

        if (draggingNodeId && containerRef.current && isEditorMode) {
            hasDraggedNode.current = true;
            const containerRect = containerRef.current.parentElement!.getBoundingClientRect();
            const mouseXRel = e.clientX - containerRect.left - pan.x;
            const mouseYRel = e.clientY - containerRect.top - pan.y;
            const xPercent = (mouseXRel / (containerRect.width * zoom)) * 100;
            const yPercent = (mouseYRel / (containerRect.height * zoom)) * 100;

            onUpdateNodePosition(
                treeData.name,
                draggingNodeId,
                Math.max(0, Math.min(100, xPercent)), // Impede node individual de sair do 0-100 (se quiser liberar isso, remova o Math.max/min)
                Math.max(0, Math.min(100, yPercent))
            );
        }
    };

    const handleCanvasMouseUp = () => {
        // --- NOVA LÓGICA: FINALIZAR ARRASTO DA ÁRVORE ---
        if (isDraggingTree.current) {
            isDraggingTree.current = false;
            // Limpa a referência para economizar memória
            initialTreeNodesRef.current = [];
            return;
        }

        setIsDraggingCanvas(false);
        setDraggingNodeId(null);
        setTimeout(() => { hasDraggedNode.current = false; }, 50);
    };

    const handleNodeStartDrag = useCallback((nodeId: string) => {
        setDraggingNodeId(nodeId);
        hasDraggedNode.current = false;
        setContextMenu(null);
    }, []);

    const handleNodeContextMenu = useCallback((e: React.MouseEvent, node: PerkNode) => {
        const slideRect = containerRef.current?.parentElement?.getBoundingClientRect();
        if (slideRect) {
            setContextMenu({
                x: e.clientX - slideRect.left,
                y: e.clientY - slideRect.top,
                nodeId: node.id
            });
        }
    }, []);

    useEffect(() => {
        const container = containerRef.current;
        if (container) container.addEventListener('wheel', handleWheel as unknown as EventListener, { passive: false });
        return () => container?.removeEventListener('wheel', handleWheel as unknown as EventListener);
    }, [handleWheel]);

    const handleNodeSave = (savedNode: PerkNode) => {
        let newNodes = [...treeData.nodes];
        const exists = newNodes.findIndex(n => n.id === savedNode.id);
        if (exists >= 0) newNodes[exists] = savedNode;
        else {
            newNodes.push(savedNode);
            if (editingNode?.sourceNodeId) {
                const srcIdx = newNodes.findIndex(n => n.id === editingNode.sourceNodeId);
                if (srcIdx >= 0) newNodes[srcIdx] = { ...newNodes[srcIdx], links: [...(newNodes[srcIdx].links || []), savedNode.id] };
            }
        }
        if (onUpdateNodes) onUpdateNodes(treeData.name, newNodes);
        setEditingNode(null);
    };

    const handleNodeMouseEnter = useCallback((node: PerkNode) => {
        if (hoverTimeoutRef.current) clearTimeout(hoverTimeoutRef.current);
        setHoveredPerk(node);
    }, []);

    const handleNodeMouseLeave = useCallback(() => {
        hoverTimeoutRef.current = setTimeout(() => {
            setHoveredPerk(null);
        }, 100);
    }, []);

    // OTIMIZAÇÃO: Estabilizar referências para que o memo() do PerkNodeElement funcione durante o pan/zoom.
    const handlePerkNodeHover = useCallback((p: PerkNode | null) => {
        if (p) handleNodeMouseEnter(p);
        else handleNodeMouseLeave();
    }, [handleNodeMouseEnter, handleNodeMouseLeave]);

    const handlePerkNodeClick = useCallback((clickedNode: PerkNode) => {
        if (isEditorMode && connectingFrom) {
            if (connectingFrom !== clickedNode.id && onUpdateNodes) {
                let newNodes = [...treeData.nodes];
                const srcIdx = newNodes.findIndex(n => n.id === connectingFrom);

                if (srcIdx >= 0) {
                    const isLinked = newNodes[srcIdx].links.includes(clickedNode.id);

                    if (isLinked) {
                        // Já está conectado: Remove a conexão
                        newNodes[srcIdx] = {
                            ...newNodes[srcIdx],
                            links: newNodes[srcIdx].links.filter(id => id !== clickedNode.id)
                        };
                    } else {
                        // Não está conectado: Adiciona a conexão
                        newNodes[srcIdx] = {
                            ...newNodes[srcIdx],
                            links: [...newNodes[srcIdx].links, clickedNode.id]
                        };
                    }
                    onUpdateNodes(treeData.name, newNodes);
                }
            }
            setConnectingFrom(null); // Sai do modo de conexão após o clique
        } else if (isEditorMode && !connectingFrom) {
            if (!hasDraggedNode.current) setEditingNode({ node: clickedNode });
        } else if (!isEditorMode && onNodeClick) {
            onNodeClick(clickedNode);
        }
    }, [isEditorMode, connectingFrom, onUpdateNodes, treeData.name, treeData.nodes, onNodeClick]);

    const handleTooltipMouseEnter = () => {
        if (hoverTimeoutRef.current) clearTimeout(hoverTimeoutRef.current);
        setIsHoveringTooltip(true);
    };

    const handleTooltipMouseLeave = () => {
        setIsHoveringTooltip(false);
        hoverTimeoutRef.current = setTimeout(() => {
            setHoveredPerk(null);
        }, 100);
    };

    useEffect(() => {
        const handleRequestCreate = (e: any) => {
            if (e.detail.treeName === treeData.name) {
                setEditingNode({
                    node: {
                        x: 50,
                        y: 50,
                        perkCost: 1,
                        requirements: [],
                        links: [],
                        name: t('perk_editor.new_title')
                    }
                });
            }
        };
        window.addEventListener('requestCreatePerk', handleRequestCreate);
        return () => window.removeEventListener('requestCreatePerk', handleRequestCreate);
    }, [treeData.name]);

    return (
        <div className="single-tree-slide"
            onContextMenu={e => e.preventDefault()}
            onMouseUp={(e) => {
                const isPerk = (e.target as HTMLElement).closest('.perk-node-container');
                if (isEditorMode && e.button === 2) {
                    if (connectingFrom) {
                        setConnectingFrom(null);
                        return;
                    }
                    if (!isPerk && onTreeContextMenu) {
                        onTreeContextMenu(e, treeData.name);
                    }
                }
            }}
        >
            {/* OTIMIZAÇÃO: pointerEvents: none em elementos visuais desnecessários de escuta */}
            <div className="skill-detail-bg" style={{ backgroundImage: `url('${detailBg}')`, pointerEvents: 'none' }}></div>
            {isEditorMode && (
                <div style={{
                    position: 'absolute',
                    bottom: '20px', /* Ficará no canto inferior esquerdo da tela da constelação */
                    left: '30px',
                    color: 'rgba(255,255,255,0.4)',
                    fontSize: '1.5rem',
                    fontFamily: 'Sovngarde, Arial, sans-serif',
                    pointerEvents: 'none',
                    zIndex: 10
                }}>
                    {t('perk_editor.move_tree_hint')}
                </div>
            )}
            <div className="tree-title" style={{ '--tree-glow': treeColor } as React.CSSProperties}>
                {connectingFrom && (
                    <div className="connecting-indicator" style={{ color: '#ffeb3b', fontSize: '1.1rem', marginBottom: '5px', textShadow: '0 2px 4px rgba(0,0,0,0.8)', fontFamily: 'Sovngarde, Arial, sans-serif' }}>
                        {t('perk_editor.connecting_overlay')}
                    </div>
                )}

                {canLegendary && !isEditorMode && (
                    <div
                        className="legendary-btn-container"
                        onClick={() => onLegendary && onLegendary(treeData.name)}
                        title={`${t('legendary.title')} (Reset to ${resetLevel})\n${t('legendary.tooltip_req', { hardCap })}`}
                    >
                        <img src="./Assets/DragonSymbol.svg" className="legendary-icon" alt="Legendary" style={{ pointerEvents: 'none' }} />
                        <span className="legendary-text">{t('legendary.btn_text')}</span>
                    </div>
                )}
                <h1>{resolvedTreeName.toUpperCase()}</h1>
                <div className="tree-divider"></div>
            </div>

            {/* OTIMIZAÇÃO: Layering Equivalente ao Konva. willChange: 'transform' aplica compositing dedicado,
                libertando o thread principal e agilizando as transformações na GPU. */}
            <div className={`zoom-container ${isDraggingCanvas ? 'dragging' : ''}`}
                ref={containerRef}
                onMouseDown={handleCanvasMouseDown}
                onMouseMove={handleCanvasMouseMove}
                onMouseUp={handleCanvasMouseUp}
                onMouseLeave={handleCanvasMouseUp}
                style={{ transform: `translate(${pan.x}px, ${pan.y}px) scale(${zoom})`, transformOrigin: '0 0', width: '100%', height: '100%', willChange: 'transform' }}>


                <TreeCanvasLayer
                    nodes={treeData.nodes}
                    treeColor={treeColor}
                    width={containerWidth}
                    height={containerHeight}
                    bounds={treeBounds}
                />


                {treeData.nodes.map((node) => (
                    <PerkNodeElement
                        key={node.id}
                        node={node}
                        treeName={treeData.name}
                        treeColor={treeColor}
                        iconPerkPath={treeData.iconPerkPath}
                        isPreview={false}
                        isEditorMode={isEditorMode} // Modo normal ou editor
                        isKeyboardSelected={keyboardSelectedNodeId === node.id}
                        setHoveredPerk={handlePerkNodeHover}
                        uiSettings={uiSettings}
                        onStartDrag={handleNodeStartDrag}
                        onNodeContextMenu={handleNodeContextMenu}
                        onNodeClick={handlePerkNodeClick}
                    />
                ))}

                {isEditorMode && treeData.nodes.length === 0 && (
                    <button className="add-first-perk-btn" onClick={() => setEditingNode({ node: { x: 50, y: 50, perkCost: 1, requirements: [], links: [] } })}>{t('perk_editor.add_first_perk')}</button>
                )}

                

                {/*{treeData.nodes.map((node) => (*/}
                {/*    <PerkNodeElement*/}
                {/*        key={node.id}*/}
                {/*        node={node}*/}
                {/*        treeName={treeData.name}*/}
                {/*        treeColor={treeColor}*/}
                {/*        iconPerkPath={treeData.iconPerkPath}*/}
                {/*        isPreview={false}*/}
                {/*        isEditorMode={isEditorMode}*/}
                {/*        isKeyboardSelected={keyboardSelectedNodeId === node.id}*/}
                {/*        setHoveredPerk={handlePerkNodeHover}*/}
                {/*        uiSettings={uiSettings}*/}
                {/*        onStartDrag={handleNodeStartDrag}*/}
                {/*        onNodeContextMenu={handleNodeContextMenu}*/}
                {/*        onNodeClick={handlePerkNodeClick}*/}
                {/*    />*/}
                {/*))}*/}

                {activeTooltipNode && !isEditorMode && !draggingNodeId && (() => {
                    const nodeToShow = activeTooltipNode;
                    const totalRanks = 1 + (nodeToShow.nextRanks?.length || 0);
                    const safeIdx = Math.min(tooltipRankIdx, totalRanks - 1);
                    const isBaseRank = safeIdx === 0;
                    const currentData = isBaseRank ? nodeToShow : nodeToShow.nextRanks![safeIdx - 1];
                    const resolveReqValue = (req: Requirement) => {
                        if (formLists && formLists[req.type]) {
                            return formLists[req.type].find(item => item.id === req.value)?.name || req.value;
                        }
                        return req.value;
                    };

                    const displayName = resolveText(currentData.name, isEditorMode);
                    const displayDesc = resolveText(currentData.description, isEditorMode);

                    return (
                        <div
                            className="perk-tooltip"
                            onWheel={handleTooltipWheel}
                            style={{
                                left: `${nodeToShow.x}%`,
                                top: `${nodeToShow.y}%`,
                                transform: nodeToShow.y > 60 ? `translate(-50%, calc(-100% - 40px))` : `translate(-50%, 40px)`,
                                pointerEvents: 'auto'
                            }}
                            onMouseEnter={handleTooltipMouseEnter}
                            onMouseLeave={handleTooltipMouseLeave}
                        >
                            <div className="tooltip-header">
                                <h2>{displayName}</h2>
                                {totalRanks > 1 && (
                                    <div className="rank-navigation">
                                        <button className="rank-nav-btn" onClick={(e) => { e.stopPropagation(); setTooltipRankIdx(prev => Math.max(0, prev - 1)); }} disabled={safeIdx === 0}>{"<"}</button>
                                        <span className="rank-pagination">{safeIdx + 1} / {totalRanks}</span>
                                        <button className="rank-nav-btn" onClick={(e) => { e.stopPropagation(); setTooltipRankIdx(prev => Math.min(totalRanks - 1, prev + 1)); }} disabled={safeIdx === totalRanks - 1}>{">"}</button>
                                    </div>
                                )}
                            </div>
                            <div className="tooltip-divider" style={{ backgroundColor: treeColor }}></div>
                            {currentData.isUnlocked && <div className="rank-status-tag acquired">{t('unlock_perk.acquired')}</div>}
                            <p className="perk-desc" style={{ color: currentData.isUnlocked ? '#66bb6a' : '#ccc' }}>{displayDesc}</p>

                            {currentData.requirements && currentData.requirements.length > 0 && (
                                <div className="perk-reqs">
                                    <strong>{t('reqs.requirements_label')}</strong>
                                    <ul>
                                        {currentData.requirements.map((req, i) => (
                                            <li key={i} className={req.isMet ? 'req-met' : 'req-unmet'} >
                                                {req.isNot && <span style={{ color: '#f44336', fontWeight: 'bold', marginRight: '5px' }}>({t('common.not')})</span>}

                                                {t(`reqs.${req.type}`, {
                                                    val: resolveReqValue(req),
                                                    target: resolveText(req.target || treeData.displayName || treeData.name, false)
                                                })}
                                                {req.isOr && <span style={{ color: '#ff9800', fontWeight: 'bold', marginLeft: '5px' }}> ({t('common.or')})</span>}
                                            </li>
                                        ))}
                                    </ul>
                                </div>
                            )}

                            {!currentData.isUnlocked && ((currentData.perkCost ?? 0) > 0 || (currentData.customCosts && currentData.customCosts.length > 0)) && (
                                <div className="perk-reqs" style={{ marginTop: '5px' }}>
                                    <strong>{t('reqs.costs_label', { defaultValue: 'Costs' })}</strong>
                                    <ul>
                                        {(currentData.perkCost ?? 0) > 0 && (
                                            <li className={playerData && playerData.perkPoints >= (currentData.perkCost ?? 0) ? 'req-met' : 'req-unmet'}>
                                                {currentData.perkCost}x {t('header.perk_points', { defaultValue: 'Perk Points' })}
                                            </li>
                                        )}
                                        {currentData.customCosts?.map((cost: CustomCost, i: number) => {
                                            const res = customResources?.find((r: CustomResource) => r.id === cost.resourceId);
                                            const resName = res ? resolveText(res.name, false) : cost.resourceId;
                                            const currentAmount = playerData?.resourceValues?.[cost.resourceId] || 0;
                                            const hasEnough = currentAmount >= cost.amount;
                                            return (
                                                <li key={`custom-cost-${i}`} className={hasEnough ? 'req-met' : 'req-unmet'}>
                                                    {cost.amount}x {resName} (Possui: {currentAmount})
                                                </li>
                                            );
                                        })}
                                    </ul>
                                </div>
                            )}
                        </div>
                    );
                })()}
            </div>

            {contextMenu && isEditorMode && (
                <div className="perk-context-menu" style={{ left: contextMenu.x, top: contextMenu.y }} onClick={e => e.stopPropagation()}>
                    <button onClick={() => { setConnectingFrom(contextMenu.nodeId); setContextMenu(null); }}>{t('perk_editor.context.connect')}</button>
                    <button onClick={() => {
                        const src = treeData.nodes.find(n => n.id === contextMenu.nodeId);
                        setEditingNode({ node: { x: (src?.x || 50) + 5, y: (src?.y || 50) + 5, perkCost: 1, requirements: [], links: [] }, sourceNodeId: contextMenu.nodeId });
                        setContextMenu(null);
                    }}>{t('perk_editor.context.create_connected')}</button>
                    <button onClick={() => { setEditingNode({ node: treeData.nodes.find(n => n.id === contextMenu.nodeId) }); setContextMenu(null); }}>{t('perk_editor.context.edit')}</button>
                    <button className="danger-text" onClick={() => {
                        if (onUpdateNodes) onUpdateNodes(treeData.name, treeData.nodes.filter(n => n.id !== contextMenu.nodeId).map(n => ({ ...n, links: n.links.filter(l => l !== contextMenu.nodeId) })));
                        setContextMenu(null);
                    }}>{t('perk_editor.context.delete')}</button>
                </div>
            )}
            {editingNode && formLists && (
                <PerkEditorModal
                    node={editingNode.node}
                    availableTrees={availableTrees}
                    formLists={formLists}
                    availableReqs={availableReqs}
                    onRequestBrowse={onRequestBrowse}
                    onSave={handleNodeSave}
                    onClose={() => setEditingNode(null)}
                    customResources={customResources}
                />
            )}
        </div>
    );
});

const SkillTreeDetail = ({
    trees, initialSkillName, isEditorMode, uiSettings, globalSettings, formLists, availableReqs, availableTrees, onRequestBrowse,
    onUpdateNodePosition, onUpdateNodes, onClose, onNodeClick, onTreeContextMenu, onLegendary, onSlideChange, playerData, customResources // <-- ADICIONADO AQUI
}: {
    trees: SkillTreeData[], initialSkillName: string, isEditorMode: boolean, globalSettings: SettingsData | null, formLists?: Record<string, AvailablePerk[]>, availableReqs: RequirementDef[], availableTrees: string[], onRequestBrowse: (field: string) => void,
    playerData: PlayerData | null, customResources: CustomResource[],
    onUpdateNodePosition: (t: string, n: string, x: number, y: number) => void,
    onUpdateNodes?: (t: string, nodes: PerkNode[]) => void,
    uiSettings: UISettings | null,
    onClose: (name?: string) => void, onNodeClick?: (node: PerkNode) => void,
    onTreeContextMenu: (e: React.MouseEvent, name: string) => void,
    onLegendary: (treeName: string) => void,
    onSlideChange: (name: string) => void 
}) => {
    const initialIndex = useMemo(() => Math.max(0, trees.findIndex(t => t.name === initialSkillName)), [trees, initialSkillName]);
    const [currentIndex, setCurrentIndex] = useState(initialIndex);
    const [keyboardNodeId, setKeyboardNodeId] = useState<string | null>(null);
    const [isCtrlPressed, setIsCtrlPressed] = useState(false);
    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => { if (e.key === 'Control') setIsCtrlPressed(true); };
        const handleKeyUp = (e: KeyboardEvent) => { if (e.key === 'Control') setIsCtrlPressed(false); };
        window.addEventListener('keydown', handleKeyDown);
        window.addEventListener('keyup', handleKeyUp);
        return () => {
            window.removeEventListener('keydown', handleKeyDown);
            window.removeEventListener('keyup', handleKeyUp);
        };
    }, []);
    const [emblaRef, emblaApi] = useEmblaCarousel({
        loop: true,
        align: 'center',
        startIndex: currentIndex,
        watchDrag: !isEditorMode && !isCtrlPressed
    });

    useEffect(() => {
        if (!emblaApi) return;
        const onSelect = () => {
            const newIndex = emblaApi.selectedScrollSnap();
            setCurrentIndex(newIndex);
            setKeyboardNodeId(null);
            onSlideChange(trees[newIndex].name); 
        };
        emblaApi.on('select', onSelect);
        return () => { emblaApi.off('select', onSelect); };
    }, [emblaApi, trees, onSlideChange]);

    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            if (isEditorMode) return;

            const key = e.key.toLowerCase();
            if (!['w', 'a', 's', 'd', 'enter', 'e'].includes(key)) return;

            const currentTree = trees[currentIndex];
            if (!currentTree || currentTree.nodes.length === 0) {
                if (key === 's') onClose();
                if (key === 'a' && emblaApi) emblaApi.scrollPrev();
                if (key === 'd' && emblaApi) emblaApi.scrollNext();
                return;
            }

            if (!keyboardNodeId) {
                if (['w', 'a', 's', 'd'].includes(key)) {
                    const startNode = currentTree.nodes.reduce((prev, curr) => (prev.y > curr.y ? prev : curr));
                    setKeyboardNodeId(startNode.id);
                }
                return;
            }

            if (key === 'enter' || key === 'e') {
                const node = currentTree.nodes.find(n => n.id === keyboardNodeId);
                if (node && onNodeClick) onNodeClick(node);
                return;
            }

            let direction: 'up' | 'down' | 'left' | 'right' | null = null;
            if (key === 'w') direction = 'up';
            if (key === 's') direction = 'down';
            if (key === 'a') direction = 'left';
            if (key === 'd') direction = 'right';

            if (direction) {
                const nextId = getNearestNode(currentTree.nodes, keyboardNodeId, direction);

                if (nextId) {
                    setKeyboardNodeId(nextId);
                } else {
                    if (direction === 'down') {
                        onClose(trees[currentIndex].name);
                    } else if (direction === 'left') {
                        if (emblaApi) emblaApi.scrollPrev();
                    } else if (direction === 'right') {
                        if (emblaApi) emblaApi.scrollNext();
                    }
                }
            }
        };

        window.addEventListener('keydown', handleKeyDown);
        return () => window.removeEventListener('keydown', handleKeyDown);
    }, [currentIndex, keyboardNodeId, trees, emblaApi, onClose, isEditorMode, onNodeClick]);

    return (
        <div className="skill-tree-overlay">
            <button className="close-tree-btn" onClick={() => {
                playSound('UISkillsBackwardSD');
                onClose(trees[currentIndex].name);
            }}>
                <img src="./Assets/Back.svg" alt="Back" style={{ pointerEvents: 'none' }} />
            </button>

            <div className="tree-detail-slider embla" ref={emblaRef}>
                <div className="embla__container">
                    {trees.map((tree) => (
                        <div className="embla__slide" key={`detail-${tree.name}`} style={{ flex: '0 0 50%', minWidth: 0, height: '100%' }}>
                            <SingleSkillTreeSlide
                                treeData={tree}
                                isEditorMode={!!isEditorMode}
                                keyboardSelectedNodeId={tree.name === trees[currentIndex]?.name ? keyboardNodeId : null}
                                globalSettings={globalSettings}
                                formLists={formLists}
                                availableReqs={availableReqs}
                                availableTrees={availableTrees}
                                onRequestBrowse={onRequestBrowse}
                                onUpdateNodePosition={onUpdateNodePosition}
                                onUpdateNodes={onUpdateNodes}
                                onNodeClick={onNodeClick}
                                onTreeContextMenu={onTreeContextMenu}
                                uiSettings={uiSettings}
                                onLegendary={onLegendary}
                                playerData={playerData}             
                                customResources={customResources}
                            />
                        </div>
                    ))}
                </div>
            </div>
        </div>
    );
};

const UISettingsModal = ({ settings, onClose, onSave }: {
    settings: UISettings, onClose: () => void, onSave: (s: UISettings) => void
}) => {
    const [formData, setFormData] = useState<UISettings>(settings);

    const handleChange = (e: React.ChangeEvent<HTMLInputElement | HTMLSelectElement>) => {
        const { name, value, type } = e.target;
        const val = type === 'checkbox' ? (e.target as HTMLInputElement).checked : value;
        setFormData(prev => ({ ...prev, [name]: val }));
    };

    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 6000 }}>
            <div className="skyrim-modal-content settings-modal-content ui-mode" style={{ maxWidth: '500px', minWidth: '400px' }}>
                <h2>{t('ui_options.title')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px' }}></div>

                <div className="settings-grid compact-grid" style={{ display: 'flex', flexDirection: 'column', gap: '15px' }}>
                    <label style={{ fontSize: '1.2rem', display: 'flex', flexDirection: 'column', gap: '5px' }}>
                        {t('ui_options.column_preview_label')}
                        <CustomSelect
                            options={[
                                { value: 'full', label: t('ui_options.preview_modes.full') },
                                { value: 'bg', label: t('ui_options.preview_modes.bg') },
                                { value: 'tree', label: t('ui_options.preview_modes.tree') },
                                { value: 'none', label: t('ui_options.preview_modes.none') }
                            ]}
                            value={formData.columnPreviewMode || 'full'}
                            onChange={(val) => setFormData(prev => ({ ...prev, columnPreviewMode: val }))}
                            width="100%"
                            disableSearch={true}
                        />
                    </label>
                    <label className="checkbox-label" style={{ fontSize: '1.2rem' }}>
                        <input type="checkbox" name="hideLockedTreeNames" checked={formData.hideLockedTreeNames} onChange={handleChange} />
                        {t('ui_options.hide_locked_names')}
                    </label>

                    <label className="checkbox-label" style={{ fontSize: '1.2rem' }}>
                        <input type="checkbox" name="hideLockedTreeBG" checked={formData.hideLockedTreeBG} onChange={handleChange} />
                        {t('ui_options.hide_locked_bg')}
                    </label>

                    <label className="checkbox-label" style={{ fontSize: '1.2rem' }}>
                        <input type="checkbox" name="performanceMode" checked={formData.performanceMode} onChange={handleChange} />
                        {t('ui_options.performance_mode')}
                    </label>

                    <label className="checkbox-label" style={{ fontSize: '1.2rem' }}>
                        <input type="checkbox" name="hidePerkNames" checked={formData.hidePerkNames} onChange={handleChange} />
                        {t('ui_options.hide_perk_names')}
                    </label>

                    <div className="tooltip-divider" style={{ width: '100%', margin: '10px 0' }}></div>

                    <label className="checkbox-label" style={{ fontSize: '1.2rem', color: '#4dd0e1' }}>
                        <input type="checkbox" name="enableEditorMode" checked={formData.enableEditorMode} onChange={handleChange} />
                        {t('ui_options.enable_editor')}
                    </label>
                </div>

                <div className="modal-actions" style={{ marginTop: '30px' }}>
                    <button className="modal-btn yes-btn" onClick={() => onSave(formData)}>{t('common.save')}</button>
                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>
        </div>
    );
};

const SkillColumn = memo(({ treeData, uiSettings, globalSettings, formLists, onSelect, onContextMenu, isForcedHover, isEditorMode }: {
    treeData: SkillTreeData,
    uiSettings: UISettings | null,
    globalSettings: SettingsData | null,
    formLists?: Record<string, AvailablePerk[]>,
    onSelect: (name: string) => void,
    onContextMenu: (e: React.MouseEvent, name: string) => void,
    isForcedHover?: boolean,
    isEditorMode: boolean
}) => {
    const ref = useRef<HTMLDivElement>(null);
    const { width, height } = useElementSize(ref);
    const [isVisible] = useVisibility('0px 100px 0px 100px');

    const iconImage = useValidImage(treeData.iconPath, DEFAULT_ICON);
    const resolvedTreeBG = useValidImage(treeData.bgPath, DEFAULT_BG);
    const treeColor = treeData.color || DEFAULT_COLOR;

    const isLocked = treeData.treeRequirements && treeData.treeRequirements.some(req => req.isMet === false);
    const hideName = isLocked && (uiSettings?.hideLockedTreeNames ?? true);
    const shouldForceDefaultBG = isLocked && (uiSettings?.hideLockedTreeBG ?? false);
    const bgImage = shouldForceDefaultBG ? DEFAULT_BG : resolvedTreeBG;

    const previewMode = uiSettings?.columnPreviewMode || 'full';
    const showTree = previewMode === 'full' || previewMode === 'tree';
    const showBG = previewMode === 'full' || previewMode === 'bg';

    const rawTreeName = treeData.displayName || treeData.name;
    const resolvedTreeName = resolveText(rawTreeName, isEditorMode);
    const displayTreeName = hideName ? "????" : resolvedTreeName.toUpperCase();

    const handleClick = useCallback(() => {
        if (!isLocked || isEditorMode)
            playSound('UISkillsForwardSD');
        onSelect(treeData.name);
    }, [onSelect, treeData.name, isLocked, isEditorMode]);

    const handleMouseUp = useCallback((e: React.MouseEvent) => {
        if (e.button === 2) {
            onContextMenu(e, treeData.name);
        }
    }, [onContextMenu, treeData.name]);

    const resolveReqValue = (req: Requirement) => {
        if (formLists && formLists[req.type]) {
            return formLists[req.type].find(item => item.id === req.value)?.name || req.value;
        }
        return req.value;
    };

    const handleMouseEnter = () => {
        playSound('UIMenuFocus');
    };

    return (
        <div className={`skill-column ${isForcedHover ? 'forced-hover' : ''} ${isLocked ? 'column-locked' : ''}`} ref={ref} onClick={handleClick} onContextMenu={e => e.preventDefault()} onMouseUp={handleMouseUp} onMouseEnter={handleMouseEnter} style={{ '--glow-color': treeColor } as React.CSSProperties}>

            <div className="column-tree-preview">
                {showTree && isVisible && !isLocked && (
                    <TreeVisualNodes
                        treeData={treeData}
                        isPreview={true}
                        isEditorMode={false}
                        containerWidth={width}
                        containerHeight={height * 0.6}
                    />
                )}
            </div>

            {showBG && (
                <div
                    className="column-bg-image"
                    style={{ backgroundImage: `url(${bgImage})`, pointerEvents: 'none' }}
                />
            )}
            <div className="column-text-gradient" />

            {isLocked && (
                <div className="locked-tree-overlay">
                    <svg viewBox="0 0 24 24" className="lock-icon" fill="currentColor" style={{ pointerEvents: 'none' }}>
                        <path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zM9 6c0-1.66 1.34-3 3-3s3 1.34 3 3v2H9V6zm9 14H6V10h12v10zm-6-3c1.1 0 2-.9 2-2s-.9-2-2-2-2 .9-2 2 .9 2 2 2z" />
                    </svg>
                    <h3>{t('reqs.locked_tree_title')}</h3>
                    <ul className="locked-req-list">
                        {treeData.treeRequirements.map((req, idx) => (
                            <li key={idx} className={req.isMet ? 'req-met' : 'req-unmet'}>
                                {req.isNot && <span style={{ color: '#f44336', fontWeight: 'bold', marginRight: '5px' }}>[NOT] </span>}

                                {t(`reqs.${req.type}`, {
                                    val: resolveReqValue(req),
                                    target: resolveText(req.target || treeData.displayName || treeData.name, false)
                                })}
                            </li>
                        ))}
                    </ul>
                </div>
            )}

            <div className="column-content">
                <div className="skill-icon-container">
                    <InlineSVGIcon src={iconImage} className="skill-icon" alt={displayTreeName} />
                </div>
                <div className="skill-info-container">
                    <div className="skill-info">
                        <span className="skill-name">{displayTreeName}</span>
                        {!isLocked && <span className="skill-level">{treeData.currentLevel}</span>}
                    </div>
                    {!isLocked && (
                        <div className="stat-bar-wrapper mini-bar">
                            <div className="bar-fill cyan-fill" style={{ width: `${treeData.currentProgress}%` }}></div>
                            <MiniBarFrameSVG />
                        </div>
                    )}
                </div>
            </div>
        </div>
    );
});

const BottomSkillGrid = ({ trees, uiSettings, onHoverSkill, onClickSkill, onContextMenu, isEditorMode }: {
    trees: SkillTreeData[], globalSettings: SettingsData | null,
    onHoverSkill: (name: string | null) => void, onClickSkill: (name: string) => void,
    uiSettings: UISettings | null,
    onContextMenu: (e: React.MouseEvent, name: string) => void,
    isEditorMode: boolean 
}) => {
    const scrollRef = useRef<HTMLDivElement>(null);
    const isDragging = useRef(false);
    const hasDragged = useRef(false);
    const startX = useRef(0);
    const scrollLeft = useRef(0);

    const handleWheel = (e: React.WheelEvent) => { if (scrollRef.current) scrollRef.current.scrollLeft += e.deltaY; };

    const handleMouseDown = (e: React.MouseEvent) => {
        if (!scrollRef.current) return;
        isDragging.current = true;
        hasDragged.current = false;
        startX.current = e.pageX - scrollRef.current.offsetLeft;
        scrollLeft.current = scrollRef.current.scrollLeft;
    };
    const handleMouseMove = (e: React.MouseEvent) => {
        if (!isDragging.current || !scrollRef.current) return;
        e.preventDefault();
        const x = e.pageX - scrollRef.current.offsetLeft;
        const walk = (x - startX.current) * 1.5;
        if (Math.abs(walk) > 5) hasDragged.current = true;
        scrollRef.current.scrollLeft = scrollLeft.current - walk;
    };
    const handleMouseUp = () => { isDragging.current = false; };
    const handleMouseLeave = () => {
        isDragging.current = false;
        onHoverSkill(null);
    };

    return (
        <div
            className="bottom-skills-grid"
            ref={scrollRef}
            onWheel={handleWheel}
            onMouseDown={handleMouseDown}
            onMouseMove={handleMouseMove}
            onMouseUp={handleMouseUp}
            onMouseLeave={handleMouseLeave}
        >
            {trees.map((tree, index) => {
                const isLocked = tree.treeRequirements && tree.treeRequirements.some(req => req.isMet === false);
                const hideName = isLocked && (uiSettings?.hideLockedTreeNames ?? true);

                const resolvedTreeName = resolveText(tree.displayName || tree.name, isEditorMode);
                const displayTreeName = hideName ? "????" : resolvedTreeName.toUpperCase();

                return (
                    <div key={`${tree.name}-${index}`} className={`bottom-grid-item ${isLocked ? 'bottom-locked' : ''}`}
                        onMouseEnter={() => !isDragging.current && onHoverSkill(tree.name)}
                        onClick={(e) => {
                            if (hasDragged.current) {
                                e.preventDefault();
                                e.stopPropagation();
                                return;
                            }
                            if (!isLocked || isEditorMode) onClickSkill(tree.name);
                        }}
                        onContextMenu={e => e.preventDefault()}
                        onMouseUp={(e) => {
                            if (hasDragged.current) return;
                            if (e.button === 2) onContextMenu(e, tree.name);
                        }}
                    >
                        <div className="bottom-grid-info">
                            <span className="grid-skill-name">{displayTreeName} {isLocked && '🔒'}</span>
                            {!isLocked && <span className="grid-skill-level">{tree.currentLevel}</span>}
                        </div>
                        {!isLocked && (
                            <div className="stat-bar-wrapper mini-bar">
                                <div className="bar-fill cyan-fill" style={{ width: `${tree.currentProgress}%` }}></div>
                                <MiniBarFrameSVG />
                            </div>
                        )}
                    </div>
                );
            })}
        </div>
    );
};

const LevelUpModal = ({ trees, settings, rules, currentLevel, pendingLevelUps, onSelect }: {
    trees: SkillTreeData[], settings: SettingsData,
    rules: LevelRule[],
    currentLevel: number, pendingLevelUps: number, onSelect: (payload: any) => void
}) => {
    const [allocations, setAllocations] = useState<Record<string, number>>({});
    const [selectedAttributes, setSelectedAttributes] = useState<Record<number, string>>({});
    const [activeCategory, setActiveCategory] = useState<string>("All");
    const [isProcessing, setIsProcessing] = useState(false);

    const categories = settings?.categories || ["All", "Combat", "Magic", "Stealth", "Special", "Custom"];

    // Calcula os níveis a serem processados e soma os recursos
    const { levelsToProcess, totalSkillPoints, totalMaxSpendable, currentCapEffective } = useMemo(() => {
        const processList = [];
        let skillPts = 0;
        let maxSpend = 0;
        let maxCap = 100;

        const startLevel = currentLevel + 1;
        const endLevel = currentLevel + pendingLevelUps;

        for (let l = startLevel; l <= endLevel; l++) {
            const eff = getEffectiveSettings(settings, rules, l);
            processList.push({ level: l, eff });
            skillPts += eff.skillPointsPerLevel || 0;
            maxSpend += eff.maxSkillPointsSpendablePerLevel || 0;
            if ((eff.skillCap || 100) > maxCap) maxCap = eff.skillCap || 100;
        }

        const currentEff = getEffectiveSettings(settings, rules, currentLevel);

        return {
            levelsToProcess: processList,
            totalSkillPoints: skillPts,
            totalMaxSpendable: maxSpend,
            currentCapEffective: currentEff
        };
    }, [currentLevel, pendingLevelUps, settings, rules]);

    const totalSpent = Object.values(allocations).reduce((a, b) => a + b, 0);
    const maxAllowed = Math.min(totalSkillPoints, totalMaxSpendable);
    const pointsRemaining = maxAllowed - totalSpent;

    const addPoint = (tree: SkillTreeData) => {
        const skillName = tree.name;
        const current = tree.currentLevel;
        const allocated = allocations[skillName] || 0;
        const cap = tree.cap || currentCapEffective.skillCap || 100;

        if (pointsRemaining > 0 && (current + allocated < cap)) {
            setAllocations(prev => ({ ...prev, [skillName]: (prev[skillName] || 0) + 1 }));
        }
    };

    const removePoint = (skillName: string) => {
        if (allocations[skillName] > 0) {
            setAllocations(prev => ({ ...prev, [skillName]: prev[skillName] - 1 }));
        }
    };

    const canConfirm = levelsToProcess.every(l => selectedAttributes[l.level]);

    const handleConfirm = () => {
        if (!canConfirm || isProcessing) return;

        setIsProcessing(true);
        onSelect({
            levelUps: levelsToProcess.map(l => ({
                level: l.level,
                attribute: selectedAttributes[l.level]
            })),
            skills: allocations
        });
    };

    const filteredTrees = useMemo(() => {
        let availableTrees = trees;
        if (activeCategory !== "All") {
            availableTrees = availableTrees.filter(t => t.category === activeCategory);
        }
        return availableTrees;
    }, [trees, activeCategory]);

    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 5000 }}>
            <div className="skyrim-modal-content level-up-modal-advanced" style={{ minWidth: '600px' }}>
                <h2>{t('level_up.title')} <span style={{ fontSize: '1rem', color: '#ffd700' }}>({pendingLevelUps} Níveis)</span></h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px' }}></div>

                <div className="level-up-layout" style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: '20px' }}>

                    {/* ESQUERDA: Distribuição de Skills (Pooled) */}
                    <div className="skill-allocation-section">
                        <h3>{t('level_up.allocate', { points: pointsRemaining })}</h3>

                        <div className="level-up-category-filters">
                            {categories.map(cat => (
                                <button
                                    key={cat}
                                    className={`level-up-category-btn ${activeCategory === cat ? 'active' : ''}`}
                                    onClick={() => setActiveCategory(cat)}
                                >
                                    {cat === "All" ? t('common.all').toUpperCase() : resolveText(cat, false).toUpperCase()}
                                </button>
                            ))}
                        </div>

                        <div className="allocation-list-grid" style={{ maxHeight: '300px', overflowY: 'auto' }}>
                            {filteredTrees.map(tree => {
                                const allocated = allocations[tree.name] || 0;
                                const cap = tree.cap || currentCapEffective.skillCap || 100;
                                const isCapped = (tree.currentLevel + allocated) >= cap;

                                const resolvedTreeName = resolveText(tree.displayName || tree.name, false);

                                return (
                                    <div className="allocation-item" key={tree.name}>
                                        <div className="alloc-info">
                                            <span className="alloc-name">{resolvedTreeName.toUpperCase()}</span>
                                            <div className="alloc-level-wrapper">
                                                <span className={`alloc-level ${isCapped ? 'capped-text' : ''}`}>
                                                    {tree.currentLevel}
                                                    {allocated > 0 && <span className="alloc-plus">+{allocated}</span>}
                                                </span>
                                                <span className="alloc-cap-limit"> / {cap}</span>
                                            </div>
                                        </div>
                                        <div className="alloc-controls">
                                            <button onClick={() => removePoint(tree.name)} disabled={allocated === 0}>-</button>
                                            <button
                                                onClick={() => addPoint(tree)}
                                                disabled={pointsRemaining === 0 || isCapped}
                                                title={isCapped ? t('level_up.cap_reached') : ""}
                                            >
                                                +
                                            </button>
                                        </div>
                                    </div>
                                );
                            })}
                        </div>
                    </div>

                    {/* DIREITA: Escolha de Atributos por Nível */}
                    <div className="level-up-attributes-section" style={{ display: 'flex', flexDirection: 'column', gap: '15px', maxHeight: '400px', overflowY: 'auto', paddingRight: '10px' }}>
                        <h3 style={{ margin: '0 0 5px 0' }}>{t('level_up.select_attributes', { defaultValue: 'Selecione os Atributos' })}</h3>

                        {levelsToProcess.map(({ level, eff }) => {
                            const selectedAttr = selectedAttributes[level];
                            return (
                                <div key={level} className="level-attribute-row" style={{ background: 'rgba(255,255,255,0.05)', padding: '10px', borderRadius: '5px', border: '1px solid rgba(255,255,255,0.1)' }}>
                                    <div style={{ marginBottom: '8px', fontWeight: 'bold', color: '#4dd0e1', fontSize: '1.1rem' }}>{t('common.lvl')} {level}</div>
                                    <div style={{ display: 'flex', gap: '10px', justifyContent: 'space-between' }}>
                                        {['Health', 'Magicka', 'Stamina'].map(attr => {
                                            const isSelected = selectedAttr === attr;
                                            const incValue = attr === 'Health' ? eff.healthIncrease : attr === 'Magicka' ? eff.magickaIncrease : eff.staminaIncrease;

                                            const cwInc = eff.carryWeightIncrease || 0;
                                            const cwMethod = eff.carryWeightMethod || 'none';
                                            const linked = eff.carryWeightLinkedAttributes || [];
                                            const givesCW = cwInc > 0 && (cwMethod === 'auto' || (cwMethod === 'linked' && linked.includes(attr)));

                                            return (
                                                <button
                                                    key={attr}
                                                    className={`attribute-btn ${attr.toLowerCase()} ${isSelected ? 'selected-attr' : ''}`}
                                                    onClick={() => setSelectedAttributes(prev => ({ ...prev, [level]: attr }))}
                                                    style={{
                                                        flex: 1, padding: '8px', fontSize: '0.85rem',
                                                        opacity: isSelected ? 1 : 0.6,
                                                        border: isSelected ? '1px solid white' : '1px solid transparent'
                                                    }}
                                                >
                                                    <div>{t(`header.${attr.toLowerCase()}`)}</div>
                                                    <div style={{ color: '#ffd700' }}>(+{incValue})</div>
                                                    {givesCW && <div style={{ fontSize: '0.7rem', color: '#ccc', marginTop: '2px' }}>{t('common.cw', { defaultValue: 'CW' })} +{cwInc}</div>}
                                                </button>
                                            );
                                        })}
                                    </div>
                                </div>
                            );
                        })}
                    </div>
                </div>

                <div className="modal-actions" style={{ marginTop: '25px' }}>
                    <button
                        className={`modal-btn yes-btn ${(!canConfirm || isProcessing) ? 'disabled-btn' : ''}`}
                        onClick={handleConfirm}
                        disabled={!canConfirm || isProcessing}
                    >
                        {isProcessing ? t('common.processing') : t('level_up.confirm_btn')}
                    </button>
                </div>
            </div>
        </div>
    );
};

const ConfirmPerkModal = ({ perkName, cost, customCosts, customResources, onConfirm, onCancel }: { perkName: string, cost: number, customCosts?: CustomCost[], customResources: CustomResource[], onConfirm: () => void, onCancel: () => void }) => {
    const plural = cost > 1 ? 's' : '';
    return (
        <div className="skyrim-modal-overlay">
            <div className="skyrim-modal-content">
                <h2>{t('unlock_perk.title')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '15px' }}></div>
                <p>{t('unlock_perk.message', { cost, plural, perkName })}</p>
                {customCosts && customCosts.length > 0 && (
                    <div style={{ marginBottom: '15px' }}>
                        {customCosts.map(c => {
                            const res = customResources.find(r => r.id === c.resourceId);
                            return <p key={c.resourceId} style={{ margin: '5px 0', fontSize: '1.1rem', color: '#ffb74d' }}>- {c.amount}x {res ? resolveText(res.name, false) : c.resourceId}</p>
                        })}
                    </div>
                )}
                <div className="modal-actions">
                    <button className="modal-btn yes-btn" onClick={onConfirm}>{t('common.yes')}</button>
                    <button className="modal-btn no-btn" onClick={onCancel}>{t('common.no')}</button>
                </div>
            </div>
        </div>
    );
};

const PerkEditorModal = ({ node, availableTrees, formLists, availableReqs, customResources, onSave, onClose, onRequestBrowse }: {
    node: Partial<PerkNode>, availableTrees: string[], formLists: Record<string, AvailablePerk[]>, availableReqs: RequirementDef[], customResources: CustomResource[], onSave: (n: PerkNode) => void,
    onClose: (currentTreeName?: string) => void, onRequestBrowse: (field: string) => void
}) => {
    const [formData, setFormData] = useState<Partial<PerkNode>>(node);
    const [selectingFormTarget, setSelectingFormTarget] = useState<string | null>(null);
    const [selectingReqTarget, setSelectingReqTarget] = useState<{ isRank: boolean, rIdx: number, reqIdx: number, type: string } | null>(null);

    useEffect(() => {
        const handleFileSelected = (e: any) => {
            const { field, path } = e.detail;
            if (field === 'icon') {
                setFormData(prev => ({ ...prev, [field]: path }));
            }
        };
        window.addEventListener('fileSelected', handleFileSelected);
        return () => window.removeEventListener('fileSelected', handleFileSelected);
    }, []);

    const handleChange = (e: any) => {
        const { name, value } = e.target;
        setFormData(prev => ({ ...prev, [name]: name === 'perkCost' ? Number(value) : value }));
    };

    const handleFormSelect = (id: string) => {
        const perkList = formLists['perk'] || [];
        const selected = perkList.find(p => p.id === id);

        const generatedRanks: PerkRank[] = [];
        let currentNextId = selected?.nextPerk;
        let safetyCounter = 0;
        while (currentNextId && safetyCounter < 10) {
            const nextP = perkList.find(p => p.id === currentNextId);
            if (!nextP) break;

            generatedRanks.push({
                perk: nextP.id,
                name: nextP.name,
                description: nextP.description || "",
                perkCost: 1,
                requirements: nextP.requirements || []
            });

            currentNextId = nextP.nextPerk;
            safetyCounter++;
        }

        if (selectingFormTarget === 'base') {
            setFormData(prev => ({
                ...prev,
                perk: id,
                name: (!prev.name || prev.name === "New Perk") && selected ? selected.name : prev.name,
                description: (!prev.description || prev.description === "") && selected ? (selected.description || "") : prev.description,
                requirements: selected?.requirements || [],
                nextRanks: generatedRanks
            }));
        } else if (selectingFormTarget !== null) {
            const rankIdx = parseInt(selectingFormTarget);
            setFormData(prev => {
                const ranks = [...(prev.nextRanks || [])];
                ranks[rankIdx] = {
                    ...ranks[rankIdx],
                    perk: id,
                    name: selected ? selected.name : ranks[rankIdx].name,
                    description: selected ? (selected.description || "") : ranks[rankIdx].description,
                    requirements: selected?.requirements || [],
                };
                if (generatedRanks.length > 0) {
                    ranks.splice(rankIdx + 1, 0, ...generatedRanks);
                }
                return { ...prev, nextRanks: ranks };
            });
        }
        setSelectingFormTarget(null);
    };

    const addReq = () => setFormData(p => ({ ...p, requirements: [...(p.requirements || []), { type: availableReqs[0]?.id || 'level', value: '' }] }));
    const updateReq = (idx: number, field: string, val: any) => {
        setFormData(p => {
            const reqs = [...(p.requirements || [])];
            reqs[idx] = { ...reqs[idx], [field]: val };
            if (field === 'type') {
                reqs[idx].value = '';
                reqs[idx].target = '';
            }
            return { ...p, requirements: reqs };
        });
    };

    const updateRankReq = (rIdx: number, reqIdx: number, field: string, val: any) => {
        setFormData(p => {
            const ranks = [...(p.nextRanks || [])];
            const reqs = [...ranks[rIdx].requirements];
            reqs[reqIdx] = { ...reqs[reqIdx], [field]: val };
            if (field === 'type') {
                reqs[reqIdx].value = '';
                reqs[reqIdx].target = '';
            }
            ranks[rIdx].requirements = reqs;
            return { ...p, nextRanks: ranks };
        });
    };
    const removeReq = (idx: number) => setFormData(p => ({ ...p, requirements: (p.requirements || []).filter((_, i) => i !== idx) }));

    const updateRank = (idx: number, field: string, val: any) => {
        const ranks = [...(formData.nextRanks || [])]; ranks[idx] = { ...ranks[idx], [field]: val };
        setFormData({ ...formData, nextRanks: ranks });
    };

    const addRankReq = (rIdx: number) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks[rIdx].requirements = [...ranks[rIdx].requirements, { type: availableReqs[0]?.id || 'level', value: '' }];
        setFormData({ ...formData, nextRanks: ranks });
    };
    const removeRankReq = (rIdx: number, reqIdx: number) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks[rIdx].requirements = ranks[rIdx].requirements.filter((_, i) => i !== reqIdx);
        setFormData({ ...formData, nextRanks: ranks });
    };
    const addManualRank = () => {
        const ranks = [...(formData.nextRanks || [])];
        ranks.push({ perk: '', name: t('perk_editor.new_manual_rank_name'), description: '', perkCost: 1, requirements: [] });
        setFormData({ ...formData, nextRanks: ranks });
    };
    const removeRank = (rIdx: number) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks.splice(rIdx, 1);
        setFormData({ ...formData, nextRanks: ranks });
    };

    // Funções para controle do custo
    const addCustomCost = () => setFormData(p => ({ ...p, customCosts: [...(p.customCosts || []), { resourceId: customResources[0]?.id || '', amount: 1 }] }));
    const updateCustomCost = (idx: number, field: string, val: any) => {
        setFormData(p => {
            const costs = [...(p.customCosts || [])];
            costs[idx] = { ...costs[idx], [field]: val };
            return { ...p, customCosts: costs };
        });
    };
    const removeCustomCost = (idx: number) => setFormData(p => ({ ...p, customCosts: (p.customCosts || []).filter((_, i) => i !== idx) }));

    const addRankCustomCost = (rIdx: number) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks[rIdx].customCosts = [...(ranks[rIdx].customCosts || []), { resourceId: customResources[0]?.id || '', amount: 1 }];
        setFormData({ ...formData, nextRanks: ranks });
    };
    const updateRankCustomCost = (rIdx: number, idx: number, field: string, val: any) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks[rIdx].customCosts[idx] = { ...ranks[rIdx].customCosts[idx], [field]: val };
        setFormData({ ...formData, nextRanks: ranks });
    };
    const removeRankCustomCost = (rIdx: number, idx: number) => {
        const ranks = [...(formData.nextRanks || [])];
        ranks[rIdx].customCosts = ranks[rIdx].customCosts.filter((_, i) => i !== idx);
        setFormData({ ...formData, nextRanks: ranks });
    };

    const handleSave = () => {
        if (!formData.id) formData.id = `node_${Date.now()}`;
        if (!formData.perk) return alert(t('perk_editor.alert_select_native'));
        onSave(formData as PerkNode);
    };

    return createPortal(
        <>
            <div
                className="skyrim-modal-overlay"
                style={{ zIndex: 3000 }}
                onClick={(e) => {
                    if (e.target === e.currentTarget) onClose();
                }}
            >
                <div
                    className="skyrim-modal-content settings-modal-content ui-mode fixed-footer-modal"
                    onClick={(e) => e.stopPropagation()}
                >
                    <h2>{node.id ? t('perk_editor.edit_title') : t('perk_editor.new_title')}</h2>

                    <div className="modal-scroll-content">
                        <div className="settings-grid">
                            <label>{t('perk_editor.base_engine')}
                                <button className="form-selector-trigger-btn" onClick={() => setSelectingFormTarget('base')}>
                                    {formData.perk ? (formLists['perk']?.find(p => p.id === formData.perk)?.name || formData.perk) : t('common.click_select')}
                                </button>
                            </label>
                            <label>{t('perk_editor.name_ui')} <input type="text" name="name" value={formData.name || ""} onChange={handleChange} /></label>
                            <label className="full-width" style={{ gridColumn: 'span 2' }}>{t('perk_editor.description')}
                                <textarea
                                    name="description"
                                    value={formData.description || ""}
                                    onChange={handleChange}
                                    className="settings-textarea"
                                />
                            </label>
                            <label>{t('tree_editor.icon_path')}
                                <div className="form-row">
                                    <input type="text" name="icon" value={formData.icon || ""} onChange={handleChange} placeholder={t('tree_editor.bg_placeholder')} style={{ flex: 1 }} />
                                    <button className="browse-btn" onClick={() => onRequestBrowse('icon')}>{t('common.browse')}</button>
                                </div>
                            </label>
                            <label>{t('perk_editor.cost')} <input type="number" name="perkCost" value={formData.perkCost ?? ''} onChange={handleChange} min={0} /></label>
                        </div>

                        <div className="dynamic-list-container" style={{ marginTop: '20px', background: 'rgba(255,152,0,0.1)', padding: '10px', borderLeft: '3px solid #ffb74d' }}>
                            <h4 style={{ color: '#ffb74d', marginBottom: '5px' }}>Custom Costs</h4>
                            {formData.customCosts?.map((cost, idx) => (
                                <div key={idx} style={{ display: 'flex', gap: '10px', alignItems: 'center' }}>
                                    <CustomSelect
                                        options={[{ value: '', label: t('common.select') }, ...customResources.map(r => ({ value: r.id, label: resolveText(r.name, true) }))]}
                                        value={cost.resourceId}
                                        onChange={val => updateCustomCost(idx, 'resourceId', val)}
                                        width="200px"
                                        disableSearch={true}
                                    />
                                    <input type="number" value={cost.amount} onChange={e => updateCustomCost(idx, 'amount', Number(e.target.value))} style={{ width: '80px' }} />
                                    <button className="delete-btn" onClick={() => removeCustomCost(idx)}>X</button>
                                </div>
                            ))}
                            <button className="add-btn" onClick={addCustomCost} style={{ width: '200px', padding: '5px' }}>+ Add Custom Cost</button>
                        </div>

                        <div className="dynamic-list-container" style={{ marginTop: '20px' }}>
                            <h4 style={{ color: '#ffd700', marginBottom: '5px' }}>{t('perk_editor.reqs_rank_1')}</h4>
                            {formData.requirements?.map((req, idx) => (
                                <RequirementInputRow
                                    key={idx} req={req}
                                    availableReqs={availableReqs} availableTrees={availableTrees}
                                    formLists={formLists}
                                    onUpdate={(field, val) => updateReq(idx, field, val)}
                                    onRemove={() => removeReq(idx)}
                                    onSelectTarget={(type) => setSelectingReqTarget({ isRank: false, rIdx: -1, reqIdx: idx, type })}
                                />
                            ))}
                            <button className="add-btn" onClick={addReq} style={{ width: '200px', padding: '5px' }}>{t('perk_editor.add_req')}</button>
                        </div>

                        <div className="dynamic-list-container" style={{ marginTop: '20px' }}>
                            <h4 style={{ color: '#4dd0e1', marginBottom: '5px' }}>{t('perk_editor.sub_ranks')}</h4>

                            {formData.nextRanks && formData.nextRanks.map((rank, idx) => (
                                <div key={idx} className="dynamic-card" style={{ marginBottom: '10px', position: 'relative' }}>

                                    {/* CABEÇALHO DO RANK */}
                                    <div style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '10px' }}>
                                        <strong style={{ color: '#4dd0e1' }}>{t('perk_editor.rank_label')} {idx + 2}:</strong>
                                        <span>{rank.name}</span>
                                        <button className="delete-btn" style={{ position: 'absolute', right: '10px', top: '10px' }} onClick={() => removeRank(idx)}>X</button>
                                    </div>

                                    {/* PROPRIEDADES DO RANK */}
                                    <div className="settings-grid">
                                        <label>{t('perk_editor.base_engine')}
                                            <button className="form-selector-trigger-btn" onClick={() => setSelectingFormTarget(idx.toString())}>
                                                {rank.perk ? (formLists['perk']?.find(p => p.id === rank.perk)?.name || rank.perk) : t('common.click_select')}
                                            </button>
                                        </label>
                                        <label>{t('perk_editor.name_ui')} <input type="text" value={rank.name} onChange={e => updateRank(idx, 'name', e.target.value)} /></label>
                                        <label>{t('perk_editor.cost')} <input type="number" value={rank.perkCost ?? ''} onChange={e => updateRank(idx, 'perkCost', Number(e.target.value))} min={0} /></label>
                                        <label className="full-width" style={{ gridColumn: 'span 2' }}>{t('perk_editor.description')}
                                            <textarea
                                                value={rank.description}
                                                onChange={e => updateRank(idx, 'description', e.target.value)}
                                                className="settings-textarea"
                                            />
                                        </label>
                                    </div>

                                    {/* REQUISITOS DO RANK */}
                                    <div style={{ marginTop: '10px' }}>
                                        {rank.requirements?.map((req, rIdx) => (
                                            <RequirementInputRow
                                                key={rIdx}
                                                req={req}
                                                availableReqs={availableReqs}
                                                availableTrees={availableTrees}
                                                formLists={formLists}
                                                onUpdate={(field, val) => updateRankReq(idx, rIdx, field, val)}
                                                onRemove={() => removeRankReq(idx, rIdx)}
                                                onSelectTarget={(type) => setSelectingReqTarget({ isRank: true, rIdx: idx, reqIdx: rIdx, type })}
                                            />
                                        ))}
                                        <button className="add-btn" onClick={() => addRankReq(idx)} style={{ padding: '5px', marginTop: '10px', width: '200px' }}>{t('perk_editor.rank_req_btn')}</button>
                                    </div>

                                    {/* === CUSTOM COSTS AJUSTADO (AGORA DENTRO DO PAI DYNAMIC-CARD) === */}
                                    <div style={{ marginTop: '10px', background: 'rgba(255,152,0,0.1)', padding: '10px', borderLeft: '3px solid #ffb74d' }}>
                                        <h5 style={{ color: '#ffb74d', margin: '0 0 5px 0' }}>Custom Costs</h5>
                                        {rank.customCosts?.map((cost, cIdx) => (
                                            <div key={cIdx} style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '5px' }}>
                                                <CustomSelect
                                                    options={[{ value: '', label: t('common.select') }, ...customResources.map(r => ({ value: r.id, label: resolveText(r.name, true) }))]}
                                                    value={cost.resourceId}
                                                    onChange={val => updateRankCustomCost(idx, cIdx, 'resourceId', val)}
                                                    width="200px"
                                                    disableSearch={true}
                                                />
                                                <input type="number" value={cost.amount} onChange={e => updateRankCustomCost(idx, cIdx, 'amount', Number(e.target.value))} style={{ width: '80px' }} />
                                                <button className="delete-btn" onClick={() => removeRankCustomCost(idx, cIdx)}>X</button>
                                            </div>
                                        ))}
                                        <button className="add-btn" onClick={() => addRankCustomCost(idx)} style={{ padding: '5px', width: '200px' }}>+ Add Custom Cost</button>
                                    </div>
                                    {/* ================================================================ */}

                                </div> /* <-- FECHAMENTO DO DYNAMIC-CARD */
                            ))}

                            <button className="add-btn" onClick={addManualRank} style={{ marginTop: '15px', width: '100%', padding: '10px', border: '1px dashed #4dd0e1', background: 'rgba(77, 208, 225, 0.1)' }}>
                                {t('perk_editor.add_manual_rank')}
                            </button>
                        </div>
                    </div>

                    <div className="modal-actions modal-fixed-footer">
                        <button className="modal-btn yes-btn" onClick={handleSave}>{t('perk_editor.save_perk')}</button>
                        <button className="modal-btn no-btn" onClick={() => {
                            playSound('UIMenuCancelSD');
                            onClose();
                        }}>
                            {t('common.cancel')}
                        </button>
                    </div>
                </div>
            </div>

            {selectingFormTarget !== null && formLists['perk'] && (
                <FormSelectorModal items={formLists['perk']} onClose={() => setSelectingFormTarget(null)} onSelect={handleFormSelect} />
            )}

            {selectingReqTarget !== null && formLists[selectingReqTarget.type] && (
                <FormSelectorModal
                    items={formLists[selectingReqTarget.type]}
                    onClose={() => setSelectingReqTarget(null)}
                    onSelect={(id) => {
                        if (selectingReqTarget.isRank) updateRankReq(selectingReqTarget.rIdx, selectingReqTarget.reqIdx, 'value', id);
                        else updateReq(selectingReqTarget.reqIdx, 'value', id);
                        setSelectingReqTarget(null);
                    }}
                />
            )}
        </>, document.body
    );
};

const ConfirmationModal = ({ title, message, onConfirm, onCancel }: { title: string, message: string, onConfirm: () => void, onCancel: () => void }) => {
    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 9000 }}>
            <div className="skyrim-modal-content">
                <h2 style={{ color: '#ffd700' }}>{title}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px' }}></div>
                <p style={{ fontSize: '1.2rem', lineHeight: '1.6' }}>{message}</p>
                <div className="modal-actions">
                    <button className="modal-btn yes-btn" onClick={onConfirm}>{t('common.confirm')}</button>
                    <button className="modal-btn no-btn" onClick={onCancel}>{t('common.cancel')}</button>
                </div>
            </div>
        </div>
    );
};

const AlertModal = ({ title, message, onClose }: { title: string, message: string, onClose: () => void }) => {
    return (
        <div className="skyrim-modal-overlay" style={{ zIndex: 9500 }}>
            <div className="skyrim-modal-content">
                <h2 style={{ color: '#f44336' }}>{title}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px' }}></div>
                <p style={{ fontSize: '1.2rem', lineHeight: '1.6' }}>{message}</p>
                <div className="modal-actions" style={{ justifyContent: 'center' }}>
                    <button className="modal-btn no-btn" onClick={onClose}>
                        {t('common.ok', { defaultValue: 'OK' })}
                    </button>
                </div>
            </div>
        </div>
    );
};

// --- Main App ---
function App() {
    const [playerData, setPlayerData] = useState<PlayerData | null>(null);
    const [skillTrees, setSkillTrees] = useState<SkillTreeData[]>([]);
    const [availableReqs, setAvailableReqs] = useState<RequirementDef[]>([]);
    const [browserField, setBrowserField] = useState<string | null>(null);
    const [availableLanguages, setAvailableLanguages] = useState<string[]>(['en']);
    const [activeCategory, setActiveCategory] = useState<string>("All");
    const [currentLang, setCurrentLang] = useState<string>('en');
    const [selectedSkill, setSelectedSkill] = useState<string | null>(null);
    const [isEditorMode, setIsEditorMode] = useState(false);
    const [isExiting, setIsExiting] = useState(false);
    const [isLoaded, setIsLoaded] = useState(false);
    const [formLists, setFormLists] = useState<Record<string, AvailablePerk[]>>({});
    const [confirmingPerk, setConfirmingPerk] = useState<PerkNode | null>(null);
    const [settings, setSettings] = useState<SettingsData | null>(null);
    const [rules, setRules] = useState<LevelRule[]>([]);
    const [uiSettings, setUiSettings] = useState<UISettings | null>(null);
    const [isEditingUISettings, setIsEditingUISettings] = useState(false);
    const [isEditingSettings, setIsEditingSettings] = useState(false);
    const [treeContextMenu, setTreeContextMenu] = useState<{ x: number, y: number, treeName: string } | null>(null);
    const [editingTree, setEditingTree] = useState<SkillTreeData | null>(null);
    const [hoveredSkillName, setHoveredSkillName] = useState<string | null>(null);
    const [isCreatingTree, setIsCreatingTree] = useState(false);
    const [newTreeName, setNewTreeName] = useState("");
    const [confirmAction, setConfirmAction] = useState<{ title: string, message: string, action: () => void } | null>(null);
    const [alertMessage, setAlertMessage] = useState<{ title: string, message: string } | null>(null);
    const [, setIsLangLoading] = useState(false);
    const hoverDebounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);
    const [customResources, setCustomResources] = useState<CustomResource[]>([]);

    const categories = settings?.categories && settings.categories.length > 0
        ? ["All", ...settings.categories]
        : ["All", "Combat", "Magic", "Stealth", "Special", "Custom"];
    const allTreeNames = skillTrees.map(t => t.name);
    const filteredTrees = useMemo(() => {
        let availableTrees = skillTrees;
        // Se NÃO estiver no modo editor, oculta as árvores marcadas com isHidden
        if (!isEditorMode) {
            availableTrees = availableTrees.filter(t => !t.isHidden);
        }

        if (activeCategory !== "All") {
            availableTrees = availableTrees.filter(tree => tree.category === activeCategory);
        }
        return availableTrees;
    }, [skillTrees, activeCategory, isEditorMode]);

    const [emblaRef, emblaApi] = useEmblaCarousel({
        loop: true,
        align: 'center',
        dragFree: !uiSettings?.performanceMode,
        containScroll: 'trimSnaps'
    });


    const shouldShowLevelUp = useMemo(() => {
        return (playerData?.pendingLevelUps || 0) > 0 && !isEditorMode && settings;
    }, [playerData?.pendingLevelUps, isEditorMode, settings]);

    const handleCloseDetail = useCallback((currentTreeName?: string) => {
        // Usa o nome recebido ou o que estava focado no momento
        const targetName = currentTreeName || hoveredSkillName;

        if (targetName) {
            setHoveredSkillName(targetName);
            if (emblaApi) {
                const index = filteredTrees.findIndex(t => t.name === targetName);
                if (index !== -1) {
                    // Força a posição imediatamente
                    emblaApi.scrollTo(index, true);

                    // Garante que o Embla não vai resetar pro 0 após o recálculo visual do navegador
                    setTimeout(() => {
                        if (emblaApi) emblaApi.scrollTo(index, true);
                    }, 10);
                }
            }
        }
        setSelectedSkill(null);
    }, [emblaApi, filteredTrees, hoveredSkillName]);

    const applyHoveredSkill = useCallback((name: string | null) => {
        if (hoverDebounceRef.current) clearTimeout(hoverDebounceRef.current);
        if (name === null) {
            setHoveredSkillName(null);
        } else {
            hoverDebounceRef.current = setTimeout(() => {
                setHoveredSkillName(prev => {
                    if (prev === name) return prev;
                    return name;
                });
            }, 300); // Debounce de 2 segundos garantido
        }
    }, []);

    useEffect(() => {
        return () => {
            Object.keys(svgContentCache).forEach(key => delete svgContentCache[key]);
            imageValidationCache.clear();
        };
    }, []);

    useEffect(() => {
        return () => {
            if (hoverDebounceRef.current) clearTimeout(hoverDebounceRef.current);
        };
    }, []);

    const loadLanguage = useCallback((lang: string) => {
        if (hasTranslation(lang)) {
            setLanguage(lang);
            setCurrentLang(lang);
            return;
        }

        setIsLangLoading(true);
        if (typeof (window as any).requestLocalization === 'function') {
            (window as any).requestLocalization(lang);
        }
    }, []);

    const handleSaveUISettings = useCallback((newUISettings: UISettings) => {
        setUiSettings(newUISettings);

        if (typeof (window as any).saveUISettings === 'function') {
            (window as any).saveUISettings(JSON.stringify(newUISettings));
        }

        setIsEditingUISettings(false);

        if (!newUISettings.enableEditorMode) {
            setIsEditorMode(false);
        }
    }, []);

    const dragStart = useRef({ x: 0, y: 0 });
    const isDraggingCarousel = useRef(false);

    useEffect(() => {
        const handleLocalizationResponse = (event: any) => {
            const { lang, data } = event.detail;
            if (lang && data) {
                console.log(`Recebido dados de tradução para: ${lang}`);
                addTranslation(lang, data);

                setLanguage(lang);
                setCurrentLang(lang);
                setIsLangLoading(false);
            }
        };

        window.addEventListener('receiveLocalization', handleLocalizationResponse);
        return () => window.removeEventListener('receiveLocalization', handleLocalizationResponse);
    }, []);

    const handleMainCarouselMouseDown = (e: React.MouseEvent) => {
        dragStart.current = { x: e.clientX, y: e.clientY };
        isDraggingCarousel.current = false;
    };

    const handleMainCarouselMouseMove = (e: React.MouseEvent) => {
        if (Math.abs(e.clientX - dragStart.current.x) > 5 || Math.abs(e.clientY - dragStart.current.y) > 5) {
            isDraggingCarousel.current = true;
        }
    };

    const handleMainCarouselMouseUp = () => {
        setTimeout(() => {
            isDraggingCarousel.current = false;
        }, 50);
    };

    useEffect(() => {
        const handleClick = () => setTreeContextMenu(null);
        window.addEventListener('click', handleClick);
        return () => window.removeEventListener('click', handleClick);
    }, []);

    const handleTreeContextMenu = useCallback((e: React.MouseEvent, treeName: string) => {
        if (!isEditorMode) return;
        e.preventDefault();
        e.stopPropagation();
        setTreeContextMenu({ x: e.clientX, y: e.clientY, treeName });
    }, [isEditorMode]);

    const handleSaveTreeProps = useCallback((updatedTree: SkillTreeData) => {
        setSkillTrees(prev => prev.map(t => t.name === updatedTree.name ? updatedTree : t));
        setEditingTree(null);
    }, []);

    const handleSaveSettings = useCallback((newSettings: SettingsData) => {
        setSettings(newSettings);
        if (typeof (window as any).saveSettings === 'function') {
            (window as any).saveSettings(JSON.stringify(newSettings));
        }
        setIsEditingSettings(false);
    }, []);

    const closeMenuWithAnimation = useCallback(() => {
        setIsExiting(true);
        setTimeout(() => {
            if (typeof (window as any).hideWindow === 'function') {
                (window as any).hideWindow("");
            }
            setIsExiting(false);
            setIsLoaded(false);
        }, 500);
    }, []);

    useEffect(() => {
        const handleHardwareBack = () => {
            // A ordem aqui define a prioridade de fechamento (do mais alto para o mais baixo)
            if (alertMessage) {
                setAlertMessage(null); 
            } else if (confirmAction) {
                setConfirmAction(null);
            } else if (browserField) {
                setBrowserField(null);
            } else if (isCreatingTree) {
                setIsCreatingTree(false);
            } else if (isEditingUISettings) {
                setIsEditingUISettings(false);
            } else if (editingTree) {
                setEditingTree(null);
            } else if (isEditingSettings) {
                setIsEditingSettings(false);
            } else if (confirmingPerk) {
                setConfirmingPerk(null);
            } else if (selectedSkill) {
                handleCloseDetail(hoveredSkillName || selectedSkill);
            } else if (playerData?.isLevelUpMenuOpen && !isEditorMode) {
                // Se o Level Up estiver aberto, não faz nada (obriga o jogador a alocar pontos)
            } else {
                // Se estiver na tela principal e sem modais, fecha o menu
                closeMenuWithAnimation();
            }
        };

        window.addEventListener('HardwareBack', handleHardwareBack);

        return () => window.removeEventListener('HardwareBack', handleHardwareBack);

        // É importante passar todas as dependências de estado para que o React saiba a ordem certa
    }, [
        confirmAction, browserField, isCreatingTree, isEditingUISettings,
        editingTree, isEditingSettings, confirmingPerk, selectedSkill,
        playerData?.isLevelUpMenuOpen, isEditorMode, handleCloseDetail,
        closeMenuWithAnimation, alertMessage
    ]);

    const updateTreeNodes = useCallback((treeName: string, newNodes: PerkNode[]) => {
        setSkillTrees(prevTrees => prevTrees.map(t => {
            if (t.name === treeName) return { ...t, nodes: newNodes };
            return t;
        }));
    }, []);

    const handleCarouselSkillSelect = useCallback((name: string) => {
        if (isDraggingCarousel.current) return;
        setSelectedSkill(name);
    }, []);

    const handleBottomSkillSelect = useCallback((name: string) => {
        setSelectedSkill(name);
    }, []);

    const handleSnapToSkill = useCallback((skillName: string | null) => {
        if (skillName === null) {
            if (emblaApi) {
                const index = emblaApi.selectedScrollSnap();
                const tree = filteredTrees[index];
                if (tree) applyHoveredSkill(tree.name);
            } else {
                applyHoveredSkill(null);
            }
            return;
        }

        applyHoveredSkill(skillName);
        if (emblaApi && !isDraggingCarousel.current) {
            const index = filteredTrees.findIndex(t => t.name === skillName);
            if (index !== -1) {
                const shouldJump = uiSettings?.performanceMode;
                emblaApi.scrollTo(index, shouldJump);
            }
        }
    }, [filteredTrees, emblaApi, uiSettings, applyHoveredSkill]);

    const wheelTimeout = useRef<ReturnType<typeof setTimeout> | null>(null);
    const handleCarouselWheel = useCallback((e: React.WheelEvent) => {
        if (!emblaApi) return;
        if (wheelTimeout.current) return;

        if (e.deltaY > 0) emblaApi.scrollNext();
        else emblaApi.scrollPrev();

        wheelTimeout.current = setTimeout(() => { wheelTimeout.current = null; }, 100);
    }, [emblaApi]);


    // Trava para evitar múltiplas chamadas do React Strict Mode
    const hasRequestedSkills = useRef(false);

    useEffect(() => {
        const handleUpdateSkills = (event: any) => {
            const data = event.detail;
            if (!data || !data.player) {
                console.warn("[PrismaUI] Dados inválidos recebidos.");
                setPlayerData(null);
                setSkillTrees([]);
                setIsLoaded(false);
                return;
            }
            

            if (data.fallbackTranslation && Object.keys(data.fallbackTranslation).length > 0) {
                addTranslation('en', data.fallbackTranslation);
            }

            if (data.activeTranslation && Object.keys(data.activeTranslation).length > 0) {
                addTranslation('NSM_Language', data.activeTranslation);
                setLanguage('NSM_Language');
                setCurrentLang('NSM_Language');
            }
            if (data.player && data.trees) {
                console.log(`[PrismaUI] Dados carregados. Jogador: ${data.player.name}, Level Atual: ${data.player.level}`);
                if (data.settings) setSettings(data.settings);
                if (data.rules) setRules(data.rules);
                if (data.uiSettings) setUiSettings(data.uiSettings);
                if (data.availableRequirements) setAvailableReqs(data.availableRequirements);
                if (data.formLists) setFormLists(data.formLists);
                if (data.availableLanguages) setAvailableLanguages(data.availableLanguages);
                if (data.customResources) setCustomResources(data.customResources);
                setIsLoaded(false);
                setIsExiting(false);

                setPlayerData(data.player);
                setSkillTrees(data.trees);

                const pathsToLoad = new Set<string>();
                data.trees.forEach((tree: SkillTreeData) => {
                    if (tree.bgPath) pathsToLoad.add(tree.bgPath);
                    if (tree.iconPath) pathsToLoad.add(tree.iconPath);
                    if (tree.iconPerkPath) pathsToLoad.add(tree.iconPerkPath);
                    tree.nodes.forEach(node => {
                        if (node.icon) pathsToLoad.add(node.icon);
                    });
                });

                // LIBERA A TELA IMEDIATAMENTE! Não trava mais a UI se um SVG demorar pra carregar
                requestAnimationFrame(() => requestAnimationFrame(() => {
                    setIsLoaded(true);
                }));

                // Faz o pre-fetch background em paralelo (Cache na Entrada)
                Array.from(pathsToLoad).forEach(path => {
                    if (!path || path.trim() === "") return;

                    if (path.toLowerCase().endsWith('.svg')) {
                        if (!svgContentCache[path]) {
                            fetch(path)
                                .then(res => res.text())
                                .then(text => {
                                    if (text.includes('<svg')) {
                                        // Limpa também durante o pré-carregamento
                                        const cleaned = text.replace(/(<svg[^>]*?)\s+width=(["']).*?\2/i, '$1')
                                            .replace(/(<svg[^>]*?)\s+height=(["']).*?\2/i, '$1');
                                        svgContentCache[path] = cleaned;
                                    } else {
                                        svgContentCache[path] = `<img src="${path}" alt="" style="width: 100%; height: 100%; object-fit: contain; pointer-events: none;" />`;
                                    }
                                })
                                .catch(() => { /* ignora erros de arquivos ausentes */ });
                        }
                    } else {
                        // Força carregamento nativo para cache de memório JPG/PNG
                        const img = new Image();
                        img.src = path;
                    }
                });
            }
        };

        window.addEventListener('updateSkills', handleUpdateSkills);

        // Faz a requisição inicial estritamente UMA vez
        if (!hasRequestedSkills.current) {
            hasRequestedSkills.current = true;
            if (typeof (window as any).requestSkills === 'function') {
                (window as any).requestSkills("");
            }
        }

        return () => window.removeEventListener('updateSkills', handleUpdateSkills);
    }, []);

    const handleSaveResources = useCallback((res: CustomResource[]) => {
        setCustomResources(res);
        if (typeof (window as any).saveResources === 'function') {
            (window as any).saveResources(JSON.stringify(res));
        }
    }, []);
    const handleDeleteResource = useCallback((id: string) => {
        if (typeof (window as any).deleteResource === 'function') {
            (window as any).deleteResource(JSON.stringify({ id }));
        }
    }, []);

    const updateNodePosition = useCallback((treeName: string, nodeId: string, x: number, y: number) => {
        setSkillTrees(currentTrees => currentTrees.map(tree => {
            if (tree.name !== treeName) return tree;
            return {
                ...tree,
                nodes: tree.nodes.map(node => node.id === nodeId ? { ...node, x, y } : node)
            };
        }));
    }, []);

    const handleAttributeSelect = useCallback((payload: any) => {
        console.log("[PrismaUI] Enviando payload de Level Up para o plugin C++:", payload);

        playSound('UISkillIncreaseSD');
        if (typeof (window as any).chooseAttribute === 'function') {
            (window as any).chooseAttribute(JSON.stringify(payload));
        }
    }, []);

    const handleUnlockPerkConfirm = useCallback(() => {
        if (confirmingPerk && typeof (window as any).unlockPerk === 'function') {
            const cost = confirmingPerk.perkCost ?? 0;
            playSound('UISkillsPerkSelect2D');
            (window as any).unlockPerk(JSON.stringify({ id: confirmingPerk.perk, cost }));
        }
        setConfirmingPerk(null);
    }, [confirmingPerk]);

    const handleNodeClick = useCallback((node: PerkNode) => {
        if (isEditorMode) return;

        let targetNodeData: any = node;
        let targetPerkId = node.perk;
        let targetCost = node.perkCost ?? 0;
        let canUnlockTarget = node.canUnlock;
        let isTargetUnlocked = node.isUnlocked;
        let targetName = node.name;

        if (node.isUnlocked && node.nextRanks && node.nextRanks.length > 0) {
            const nextRank = node.nextRanks.find(r => !r.isUnlocked);
            if (nextRank) {
                targetNodeData = nextRank;
                targetPerkId = nextRank.perk;
                targetCost = nextRank.perkCost ?? 0;
                canUnlockTarget = nextRank.canUnlock;
                isTargetUnlocked = nextRank.isUnlocked;
                targetName = nextRank.name;
            }
        }

        if (canUnlockTarget && !isTargetUnlocked && playerData) {
            let canAfford = playerData.perkPoints >= targetCost;

            if (canAfford && targetNodeData.customCosts) {
                for (const cost of targetNodeData.customCosts) {
                    const currentAmt = playerData.resourceValues?.[cost.resourceId] || 0;
                    if (currentAmt < cost.amount) {
                        canAfford = false;
                        break;
                    }
                }
            }

            if (canAfford) {
                setConfirmingPerk({ ...node, name: targetName, perk: targetPerkId, perkCost: targetCost, customCosts: targetNodeData.customCosts });
            } else {
                playSound('UIMenuCancelSD');
                setAlertMessage({
                    title: t('common.warning', { defaultValue: 'Aviso' }),
                    message: t('unlock_perk.insufficient_points', { defaultValue: 'Você não possui recursos suficientes para desbloquear este Perk.' })
                });
            }
        }
    }, [isEditorMode, playerData]);

    const handleSaveTrees = useCallback(() => {
        if (typeof (window as any).saveSkillTrees === 'function') {
            (window as any).saveSkillTrees(JSON.stringify(skillTrees));
        }
    }, [skillTrees]);

    const handleCreateNewTree = () => {
        setNewTreeName("");
        setIsCreatingTree(true);
    };

    const confirmCreateTree = () => {
        if (newTreeName && newTreeName.trim() !== "") {
            if (typeof (window as any).createTree === 'function') {
                (window as any).createTree(JSON.stringify({ name: newTreeName.trim() }));
            }
        }
        setIsCreatingTree(false);
    };

    const handleSaveRules = useCallback((newRules: LevelRule[]) => {
        setRules(newRules);
        if (typeof (window as any).saveRules === 'function') {
            (window as any).saveRules(JSON.stringify(newRules));
        }
    }, []);

    useEffect(() => {
        const handleKeyDown = (e: KeyboardEvent) => {
            const activeElement = document.activeElement as HTMLElement;
            if (activeElement && ['INPUT', 'TEXTAREA', 'SELECT'].includes(activeElement.tagName)) {
                return;
            }

            const isAnyModalOpen =
                selectedSkill !== null ||
                isEditingSettings ||
                editingTree !== null ||
                confirmingPerk !== null ||
                browserField !== null ||
                isCreatingTree ||
                (playerData?.isLevelUpMenuOpen && !isEditorMode);

            if (isAnyModalOpen || !emblaApi) return;

            const key = e.key.toLowerCase();

            if (key === 'a') {
                emblaApi.scrollPrev();
            } else if (key === 'd') {
                emblaApi.scrollNext();
            }
            else if (key === 'w' || key === 'enter') {
                if (hoveredSkillName) {
                    const isLocked = skillTrees.find(t => t.name === hoveredSkillName)?.treeRequirements?.some(req => req.isMet === false);
                    if (!isLocked) {
                        setSelectedSkill(hoveredSkillName);
                    }
                }
            }
        };

        window.addEventListener('keydown', handleKeyDown);
        return () => window.removeEventListener('keydown', handleKeyDown);
    }, [
        emblaApi,
        selectedSkill,
        isEditingSettings,
        editingTree,
        confirmingPerk,
        browserField,
        isCreatingTree,
        playerData?.isLevelUpMenuOpen,
        isEditorMode,
        hoveredSkillName,
        skillTrees
    ]);

    useEffect(() => {
        if (!emblaApi) return;

        const onSelect = () => {
            const index = emblaApi.selectedScrollSnap();
            const tree = filteredTrees[index];
            if (tree) {
                applyHoveredSkill(tree.name);
            }
        };

        emblaApi.on('select', onSelect);
        emblaApi.on('init', onSelect);

        return () => {
            emblaApi.off('select', onSelect);
        };
    }, [emblaApi, filteredTrees, applyHoveredSkill]);



    const handleLegendaryRequest = useCallback((treeName: string) => {
        const targetTree = skillTrees.find(t => t.name === treeName);
        const resetLvl = targetTree?.initialLevel || 15;

        setConfirmAction({
            title: t('legendary.title'),
            message: t('legendary.confirm_message', { treeName, resetLevel: resetLvl }),
            action: () => {
                if (typeof (window as any).legendarySkill === 'function') {
                    (window as any).legendarySkill(JSON.stringify({ treeName }));
                }
                setConfirmAction(null);
            }
        });
    }, [skillTrees]);

    const handleResetAllRequest = useCallback(() => {
        setConfirmAction({
            title: t('reset_all.title'),
            message: t('reset_all.confirm_message'),
            action: () => {
                if (typeof (window as any).resetAllPerks === 'function') {
                    (window as any).resetAllPerks("");
                }
                setConfirmAction(null);
            }
        });
    }, []);

    useEffect(() => {
        const handleDeleteRequest = (e: any) => {
            const treeName = e.detail.name;
            setConfirmAction({
                title: t('delete_tree.title'),
                message: t('delete_tree.confirm_message', { treeName }),
                action: () => {
                    if (typeof (window as any).deleteTree === 'function') {
                        (window as any).deleteTree(JSON.stringify({ name: treeName }));
                    }
                    setEditingTree(null);
                    setSelectedSkill(null);
                    setConfirmAction(null);
                }
            });
        };
        window.addEventListener('requestDeleteTree', handleDeleteRequest);
        return () => window.removeEventListener('requestDeleteTree', handleDeleteRequest);
    }, []);

    return (
        <div className={`app-container ${isLoaded ? 'loaded' : ''} ${isExiting ? 'exiting' : ''} ${uiSettings?.performanceMode ? 'performance-mode' : ''}`}>
            {playerData && <PlayerHeader player={playerData} customResources={customResources} />}

            {uiSettings?.enableEditorMode && (
                <div className="editor-toolbar">
                    <button className={`editor-btn ${isEditorMode ? 'active' : ''}`} onClick={() => setIsEditorMode(!isEditorMode)}>
                        {isEditorMode ? t('editor_toolbar.close_btn') : t('editor_toolbar.mode_btn')}
                    </button>

                    {isEditorMode && (
                        <>
                            <button className="editor-btn" onClick={handleCreateNewTree} style={{ borderColor: '#4dd0e1', color: '#4dd0e1' }}>
                                {t('editor_toolbar.new_tree')}
                            </button>
                            <button className="editor-btn" onClick={() => setIsEditingSettings(true)}>
                                {t('editor_toolbar.edit_settings')}
                            </button>
                            <button className="editor-btn save-btn" onClick={handleSaveTrees}>
                                {t('editor_toolbar.save_changes')}
                            </button>
                            
                        </>
                    )}
                </div>
            )}

            <div style={{ position: 'absolute', bottom: '40px', right: '40px', zIndex: 1500 }}>
                <button className="ui-settings-btn" onClick={() => setIsEditingUISettings(true)}>
                    {t('ui_options.btn_text')}
                </button>
            </div>

            <div style={{ position: 'absolute', bottom: '40px', right: '40px', zIndex: 1500 }}>
                <button className="ui-settings-btn" onClick={() => setIsEditingUISettings(true)}>
                    {t('ui_options.btn_text')}
                </button>
            </div>

            <div
                className="skills-infinite-container embla"
                ref={emblaRef}
                style={selectedSkill ? { visibility: 'hidden', opacity: 0, pointerEvents: 'none' } : undefined}
                onWheel={handleCarouselWheel}
                onMouseDown={handleMainCarouselMouseDown}
                onMouseMove={handleMainCarouselMouseMove}
                onMouseUp={handleMainCarouselMouseUp}
                onMouseLeave={handleMainCarouselMouseUp}
            >
                <div className={`skills-columns-track embla__container ${hoveredSkillName ? 'has-forced-hover' : ''}`}>
                    {filteredTrees.map((tree) => {
                        const isHovered = tree.name === hoveredSkillName;
                        return (
                            <div className={`embla__slide ${isHovered ? 'is-forced-hover' : ''}`} key={`col-${tree.name}`}>
                                <SkillColumn
                                    treeData={tree}
                                    globalSettings={settings}
                                    uiSettings={uiSettings}
                                    formLists={formLists}
                                    onSelect={handleCarouselSkillSelect}
                                    onContextMenu={handleTreeContextMenu}
                                    isForcedHover={isHovered}
                                    isEditorMode={isEditorMode}
                                />
                            </div>
                        );
                    })}
                </div>
            </div>

            {skillTrees.length > 0 && (
                <div
                    className="bottom-ui-panel"
                    style={selectedSkill ? { visibility: 'hidden', opacity: 0, pointerEvents: 'none' } : undefined}
                >
                    <div className="category-filter-container">
                        {categories.map(cat => (
                            <button key={cat} className={`category-btn ${activeCategory === cat ? 'active' : ''}`} onClick={() => setActiveCategory(cat)}>
                                {cat === "All" ? t('common.all').toUpperCase() : cat.toUpperCase()}
                            </button>
                        ))}
                    </div>

                    <BottomSkillGrid
                        trees={filteredTrees}
                        globalSettings={settings}
                        uiSettings={uiSettings}
                        onHoverSkill={handleSnapToSkill}
                        onClickSkill={handleBottomSkillSelect}
                        onContextMenu={handleTreeContextMenu}
                        isEditorMode={isEditorMode}
                    />
                </div>
            )}

            {!selectedSkill && skillTrees.length > 0 && (
                <div
                    className="bottom-ui-panel"
                    style={selectedSkill ? { visibility: 'hidden', opacity: 0, pointerEvents: 'none' } : undefined}
                >
                    <div className="category-filter-container">
                        {categories.map(cat => (
                            <button key={cat} className={`category-btn ${activeCategory === cat ? 'active' : ''}`} onClick={() => setActiveCategory(cat)}>
                                {cat === "All" ? t('common.all').toUpperCase() : cat.toUpperCase()}
                            </button>
                        ))}
                    </div>

                    <BottomSkillGrid
                        trees={filteredTrees}
                        globalSettings={settings}
                        uiSettings={uiSettings}
                        onHoverSkill={handleSnapToSkill}
                        onClickSkill={handleBottomSkillSelect}
                        onContextMenu={handleTreeContextMenu}
                        isEditorMode={isEditorMode}
                    />
                </div>
            )}

            {selectedSkill && (
                <SkillTreeDetail
                    initialSkillName={selectedSkill}
                    trees={filteredTrees}
                    isEditorMode={!!isEditorMode}
                    globalSettings={settings}
                    formLists={formLists}
                    availableReqs={availableReqs}
                    availableTrees={allTreeNames}
                    onRequestBrowse={(field: string) => setBrowserField(field)}
                    onUpdateNodePosition={updateNodePosition}
                    onUpdateNodes={updateTreeNodes}
                    onClose={handleCloseDetail}
                    onNodeClick={handleNodeClick}
                    onTreeContextMenu={handleTreeContextMenu}
                    uiSettings={uiSettings}
                    onLegendary={handleLegendaryRequest}
                    playerData={playerData}            
                    customResources={customResources}
                    onSlideChange={(name) => {
                        const index = filteredTrees.findIndex(t => t.name === name);
                        if (index !== -1 && emblaApi) {
                            emblaApi.scrollTo(index, true); // O 'true' faz pular instantaneamente sem animar!
                            applyHoveredSkill(name); // Garante que a árvore correta fique "acesa"
                        }
                    }}
                />
            )}

            {confirmingPerk && !isEditorMode && (
                <ConfirmPerkModal
                    perkName={confirmingPerk.name}
                    cost={confirmingPerk.perkCost ?? 0}
                    onConfirm={handleUnlockPerkConfirm}
                    onCancel={() => setConfirmingPerk(null)}
                    customResources={customResources}
                    customCosts={confirmingPerk.customCosts}
                />
            )}

            {shouldShowLevelUp && (
                <LevelUpModal
                    key={playerData!.level}
                    trees={skillTrees}
                    rules={rules}
                    settings={settings}
                    currentLevel={playerData!.level || 1}
                    pendingLevelUps={playerData!.pendingLevelUps || 0}
                    onSelect={handleAttributeSelect}
                />
            )}

            {isEditingSettings && settings && (
                <SettingsModal
                    settings={settings}
                    rules={rules}
                    formLists={formLists}
                    onClose={() => setIsEditingSettings(false)}
                    onSaveSettings={handleSaveSettings}
                    onSaveRules={handleSaveRules}
                    onResetAllPerks={handleResetAllRequest}
                    customResources={customResources}
                    onSaveResources={handleSaveResources}
                    onDeleteResource={handleDeleteResource}
                />
            )}

            {treeContextMenu && isEditorMode && (
                <div className="perk-context-menu" style={{ left: treeContextMenu.x, top: treeContextMenu.y }} onClick={e => e.stopPropagation()}>
                    <button onClick={() => {
                        const targetTree = skillTrees.find(t => t.name === treeContextMenu.treeName);
                        if (targetTree) setEditingTree(targetTree);
                        setTreeContextMenu(null);
                    }}>
                        {t('tree_editor.edit_tree')}
                    </button>

                    {selectedSkill && (
                        <button onClick={() => {
                            window.dispatchEvent(new CustomEvent('requestCreatePerk', {
                                detail: { treeName: treeContextMenu.treeName }
                            }));
                            setTreeContextMenu(null);
                        }}>
                            {t('perk_editor.add_new_perk')}
                        </button>
                    )}
                </div>
            )}

            {editingTree && settings && (
                <TreeEditorModal
                    tree={editingTree}
                    settings={settings}
                    availableReqs={availableReqs}
                    availableTrees={allTreeNames}
                    formLists={formLists}
                    onRequestBrowse={(field: string) => setBrowserField(field)}
                    onClose={() => setEditingTree(null)}
                    onSave={handleSaveTreeProps}
                />
            )}

            {browserField && (
                <FileBrowserModal
                    field={browserField}
                    initialPath=""
                    onClose={() => setBrowserField(null)}
                    onSelect={(field, path) => {
                        window.dispatchEvent(new CustomEvent('fileSelected', { detail: { field, path } }));
                        setBrowserField(null);
                    }}
                />
            )}

            {isCreatingTree && (
                <div className="skyrim-modal-overlay" style={{ zIndex: 6000 }}>
                    <div className="skyrim-modal-content">
                        <h2>{t('tree_editor.new_title')}</h2>
                        <div className="tooltip-divider" style={{ width: '100%', marginBottom: '15px' }}></div>
                        <p style={{ fontSize: '1rem', marginBottom: '15px' }}>{t('tree_editor.create_instruction')}</p>
                        <input
                            type="text"
                            value={newTreeName}
                            onChange={e => setNewTreeName(e.target.value)}
                            style={{
                                width: '100%', padding: '12px', marginBottom: '25px',
                                background: 'rgba(0,0,0,0.8)', color: 'white',
                                border: '1px solid #4dd0e1', fontSize: '1.2rem',
                                outline: 'none', fontFamily: 'Sovngarde, sans-serif'
                            }}
                            autoFocus={true}
                        />
                        <div className="modal-actions">
                            <button className="modal-btn yes-btn" onClick={confirmCreateTree}>{t('common.create')}</button>
                            <button className="modal-btn no-btn" onClick={() => setIsCreatingTree(false)}>{t('common.cancel')}</button>
                        </div>
                    </div>
                </div>
            )}

            {isEditingUISettings && uiSettings && (
                <UISettingsModal
                    settings={uiSettings}
                    onClose={() => setIsEditingUISettings(false)}
                    onSave={handleSaveUISettings}
                />
            )}

            {confirmAction && (
                <ConfirmationModal
                    title={confirmAction.title}
                    message={confirmAction.message}
                    onConfirm={confirmAction.action}
                    onCancel={() => setConfirmAction(null)}
                />
            )}

            {alertMessage && (
                <AlertModal
                    title={alertMessage.title}
                    message={alertMessage.message}
                    onClose={() => setAlertMessage(null)}
                />
            )}
        </div>
    );
}

export default App;