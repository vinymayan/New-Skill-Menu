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
interface PerkRank {
    perk: string; name: string; description: string;
    perkCost: number; requirements: Requirement[];
    isUnlocked?: boolean; canUnlock?: boolean;
}
interface PerkNode {
    id: string; perk: string; name: string; description: string;
    icon: string; x: number; y: number; requirements: Requirement[];
    links: string[]; isUnlocked: boolean; canUnlock?: boolean;
    perkCost: number;
    nextRanks?: PerkRank[];
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
    pendingLevelUp?: boolean;
    isLevelUpMenuOpen?: boolean;
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

// OTIMIZAÇÃO: "pointerEvents: 'none'" para que SVGs inline não dispersem eventos no hit-tester
const InlineSVGIcon = memo(({ src, className, alt }: { src: string, className?: string, alt?: string }) => {
    const [svgContent, setSvgContent] = useState<string | null>(svgContentCache[src] || null);

    useEffect(() => {
        if (!src || !src.toLowerCase().includes('.svg') || svgContentCache[src]) return;

        let isMounted = true;
        fetch(src)
            .then(res => res.text())
            .then(text => {
                if (isMounted && text.includes('<svg')) {
                    svgContentCache[src] = text;
                    setSvgContent(text);
                }
            })
            .catch(err => console.error("Erro ao carregar SVG:", src, err));

        return () => { isMounted = false; };
    }, [src]);

    if (!src) return null;

    if (src.toLowerCase().includes('.svg') && svgContent) {
        return (
            <div
                className={`${className} inline-svg-wrapper`}
                dangerouslySetInnerHTML={{ __html: svgContent }}
                title={alt}
                style={{ pointerEvents: 'none' }}
            />
        );
    }

    return <img src={src} className={className} alt={alt} style={{ pointerEvents: 'none' }} />;
});

const PlayerHeader = ({ player }: { player: PlayerData }) => {
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

                <div className="header-item">
                    <span className="header-label">{t('header.perk_points')}</span>
                    <span className="header-value">{player.perkPoints}</span>
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
    // Nova referência necessária para poder colorir o ícone sem bugar as sombras do Canvas
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
                shadowOpacity: 0.6
            });
        }
    };

    // Aplica o cache inicial. O Konva precisa do cache ativado para pintar o SVG usando source-in corretamente.
    useEffect(() => {
        if (iconGroupRef.current) iconGroupRef.current.cache();
        if (groupRef.current) groupRef.current.cache();
    }, [image, isMaxed, node.isUnlocked]);

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
            {/* 1. RESTAURAMOS A COR DE FUNDO E BORDA AQUI */}
            <Circle
                radius={nodeSize / 2}
                shadowColor={treeColor}
                shadowBlur={node.isUnlocked ? 20 : 0}
                shadowOpacity={node.isUnlocked ? 0.6 : 0}
            />

            {/* 2. TRUQUE DO KONVA PARA COLORIR O SVG */}
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

            {/*{!hidePerkNames && (*/}
            {/*    <KonvaText*/}
            {/*        text={node.name.toUpperCase()}*/}
            {/*        y={25}*/}
            {/*        fontSize={14}*/}
            {/*        fontFamily="Sovngarde"*/}
            {/*        fill="white"*/}
            {/*        align="center"*/}
            {/*        width={200}*/}
            {/*        x={-100}*/}
            {/*        shadowColor="black"*/}
            {/*        shadowBlur={2}*/}
            {/*        opacity={node.isUnlocked || node.canUnlock ? 1 : 0.6}*/}
            {/*        listening={false}*/}
            {/*    />*/}
            {/*)}*/}
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
                <InlineSVGIcon src={iconImage} alt={node.name} />
            </div>
            {!isPreview && (uiSettings ? !uiSettings.hidePerkNames : true) && (
                <div className="perk-node-label" style={{ opacity: node.isUnlocked || node.canUnlock ? 1 : 0.6 }}>
                    {(node.name || t('common.unknown')).toUpperCase()}
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

const SettingsModal = ({ settings, rules, onClose, onSaveSettings, onSaveRules, onResetAllPerks }: {
    settings: SettingsData,
    rules: LevelRule[],
    onClose: () => void,
    onSaveSettings: (s: SettingsData) => void,
    onSaveRules: (r: LevelRule[]) => void,
    onResetAllPerks: () => void
}) => {
    const [settingsData, setSettingsData] = useState<SettingsData>(JSON.parse(JSON.stringify(settings)));
    const [rulesData, setRulesData] = useState<LevelRule[]>(JSON.parse(JSON.stringify(rules || [])));

    const [activeTab, setActiveTab] = useState<'base' | 'rules' | 'codes' | 'categories'>('base');
    const [newCatName, setNewCatName] = useState("");

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
                    }}>{t('common.save')}</button>
                    <button className="modal-btn no-btn" onClick={onClose}>{t('common.cancel')}</button>
                </div>
            </div>
        </div>
    );
};

const SingleSkillTreeSlide = memo(({ treeData, isEditorMode,
    uiSettings, keyboardSelectedNodeId, onUpdateNodePosition,
    onUpdateNodes, onNodeClick, onTreeContextMenu, formLists, 
    availableReqs, availableTrees, onRequestBrowse, onLegendary, globalSettings }: {
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
        onLegendary?: (treeName: string) => void
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
                <h1>{(treeData.displayName || treeData.name).toUpperCase()}</h1>
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
                        // Se a lista desse tipo de requisito existir no nosso dicionário, procura nela:
                        if (formLists && formLists[req.type]) {
                            return formLists[req.type].find(item => item.id === req.value)?.name || req.value;
                        }
                        return req.value;
                    };

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
                                <h2>{currentData.name}</h2>
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
                            <p className="perk-desc" style={{ color: currentData.isUnlocked ? '#66bb6a' : '#ccc' }}>{currentData.description}</p>

                            {currentData.requirements && currentData.requirements.length > 0 && (
                                <div className="perk-reqs">
                                    <strong>{t('reqs.requirements_label')}</strong>
                                    <ul>
                                        {currentData.requirements.map((req, i) => (
                                            <li key={i} className={req.isMet ? 'req-met' : 'req-unmet'} >
                                                {req.isNot && <span style={{ color: '#f44336', fontWeight: 'bold', marginRight: '5px' }}>({t('common.not')})</span>}

                                                {t(`reqs.${req.type}`, {
                                                    val: resolveReqValue(req),
                                                    target: req.target || treeData.displayName || treeData.name
                                                })}
                                                {req.isOr && <span style={{ color: '#ff9800', fontWeight: 'bold', marginLeft: '5px' }}> ({t('common.or')})</span>}
                                            </li>
                                        ))}
                                    </ul>
                                    {!currentData.isUnlocked && (
                                        <div className={`cost-tag ${currentData.canUnlock ? 'req-met' : 'req-unmet'}`}>
                                            {t('unlock_perk.cost_tag', { cost: currentData.perkCost || 1 })}
                                        </div>
                                    )}
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
                />
            )}
        </div>
    );
});

const SkillTreeDetail = ({
    trees, initialSkillName, isEditorMode, uiSettings, globalSettings, formLists, availableReqs, availableTrees, onRequestBrowse,
    onUpdateNodePosition, onUpdateNodes, onClose, onNodeClick, onTreeContextMenu, onLegendary
}: {
        trees: SkillTreeData[], initialSkillName: string, isEditorMode: boolean, globalSettings: SettingsData | null, formLists?: Record<string, AvailablePerk[]>, availableReqs: RequirementDef[], availableTrees: string[], onRequestBrowse: (field: string) => void,
    onUpdateNodePosition: (t: string, n: string, x: number, y: number) => void,
    onUpdateNodes?: (t: string, nodes: PerkNode[]) => void,
    uiSettings: UISettings | null,
    onClose: (name?: string) => void, onNodeClick?: (node: PerkNode) => void,
    onTreeContextMenu: (e: React.MouseEvent, name: string) => void,
    onLegendary: (treeName: string) => void
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
            setCurrentIndex(emblaApi.selectedScrollSnap());
            setKeyboardNodeId(null);
        };
        emblaApi.on('select', onSelect);
        return () => { emblaApi.off('select', onSelect); };
    }, [emblaApi]);

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
                            />
                        </div>
                    ))}
                </div>
            </div>
        </div>
    );
};

const UISettingsModal = ({ availableLanguages, settings, onClose, onSave }: {
    availableLanguages: string[],
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
                <label style={{ fontSize: '1.1rem', color: '#ccc', display: 'flex', flexDirection: 'column', gap: '5px' }}>
                    {t('ui_options.language_label')}
                    <CustomSelect
                        options={availableLanguages.map(lang => ({ value: lang, label: lang.toUpperCase() }))}
                        value={formData.language}
                        onChange={(val) => setFormData(prev => ({ ...prev, language: val as Language }))}
                        width="100%"
                        disableSearch={true}
                    />
                </label>

                <div className="tooltip-divider" style={{ width: '100%', margin: '10px 0' }}></div>
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

const SkillColumn = memo(({ treeData, uiSettings, globalSettings, formLists, onSelect, onContextMenu, isForcedHover }: {
    treeData: SkillTreeData,
    uiSettings: UISettings | null,
    globalSettings: SettingsData | null,
    formLists?: Record<string, AvailablePerk[]>, 
    onSelect: (name: string) => void,
    onContextMenu: (e: React.MouseEvent, name: string) => void,
    isForcedHover?: boolean
}) => {
    const ref = useRef<HTMLDivElement>(null);
    const { width, height } = useElementSize(ref);
    const [isVisible] = useVisibility('0px 100px 0px 100px'); // logic remains same but separate hooks

    const iconImage = useValidImage(treeData.iconPath, DEFAULT_ICON);
    const resolvedTreeBG = useValidImage(treeData.bgPath, DEFAULT_BG);
    const treeColor = treeData.color || DEFAULT_COLOR;

    const isLocked = treeData.treeRequirements && treeData.treeRequirements.some(req => req.isMet === false);
    const isEditorActive = uiSettings?.enableEditorMode || false;
    const hideName = isLocked && (uiSettings?.hideLockedTreeNames ?? true);
    const shouldForceDefaultBG = isLocked && (uiSettings?.hideLockedTreeBG ?? false);
    const bgImage = shouldForceDefaultBG ? DEFAULT_BG : resolvedTreeBG;

    // --- LÓGICA NOVA ---
    const previewMode = uiSettings?.columnPreviewMode || 'full';

    // Determina o que renderizar baseado no modo escolhido
    const showTree = previewMode === 'full' || previewMode === 'tree';
    const showBG = previewMode === 'full' || previewMode === 'bg';
    const displayTreeName = hideName ? "????" : (treeData.displayName || treeData.name).toUpperCase();

    const handleClick = useCallback(() => {
        if (!isLocked || isEditorActive)
            playSound('UISkillsForwardSD');
        onSelect(treeData.name);
    }, [onSelect, treeData.name, isLocked, isEditorActive]);

    const handleMouseUp = useCallback((e: React.MouseEvent) => {
        if (e.button === 2) {
            onContextMenu(e, treeData.name);
        }
    }, [onContextMenu, treeData.name]);

    const resolveReqValue = (req: Requirement) => {
        // Se a lista desse tipo de requisito existir no nosso dicionário, procura nela:
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
                {/* Renderiza a Árvore de Perks se o modo permitir */}
                {showTree && isVisible && !isLocked && (
                    <TreeVisualNodes
                        treeData={treeData}
                        isPreview={true}
                        isEditorMode={false}
                        containerWidth={width} // Pass width
                        containerHeight={height * 0.6} // Use 60% height for preview area matching CSS .column-tree-preview height
                    />
                )}
            </div>

            {/* Renderiza o Background se o modo permitir */}
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
                                    target: req.target || treeData.displayName || treeData.name
                                })}
                            </li>
                        ))}
                    </ul>
                </div>
            )}

            <div className="column-content">
                <div className="skill-icon-container">
                    <InlineSVGIcon src={iconImage} className="skill-icon" alt={treeData.displayName || treeData.name} />
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

const BottomSkillGrid = ({ trees, uiSettings, onHoverSkill, onClickSkill, onContextMenu }: {
    trees: SkillTreeData[], globalSettings: SettingsData | null,
    onHoverSkill: (name: string | null) => void, onClickSkill: (name: string) => void,
    uiSettings: UISettings | null,
    onContextMenu: (e: React.MouseEvent, name: string) => void
}) => {
    const scrollRef = useRef<HTMLDivElement>(null);
    const isDragging = useRef(false);
    const hasDragged = useRef(false);
    const startX = useRef(0);
    const scrollLeft = useRef(0);

    const handleWheel = (e: React.WheelEvent) => { if (scrollRef.current) scrollRef.current.scrollLeft += e.deltaY; };
    const isEditorActive = uiSettings?.enableEditorMode || false;
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
        const walk = (x - startX.current) * 1.5; // Multiplicador de velocidade

        // Se moveu mais que 5 pixels, considera que está arrastando (evita cliques acidentais)
        if (Math.abs(walk) > 5) {
            hasDragged.current = true;
        }
        scrollRef.current.scrollLeft = scrollLeft.current - walk;
    };

    const handleMouseUp = () => {
        isDragging.current = false;
    };

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
                const displayTreeName = hideName ? "????" : (tree.displayName || tree.name).toUpperCase();

                return (
                    <div key={`${tree.name}-${index}`} className={`bottom-grid-item ${isLocked ? 'bottom-locked' : ''}`}
                        onMouseEnter={() => !isDragging.current && onHoverSkill(tree.name)}
                        onClick={(e) => {
                            // Ignora o clique se o usuário estava arrastando a barra
                            if (hasDragged.current) {
                                e.preventDefault();
                                e.stopPropagation();
                                return;
                            }
                            if (!isLocked || isEditorActive) onClickSkill(tree.name);
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

const LevelUpModal = ({ trees, settings, rules, targetLevel, onSelect }: {
    trees: SkillTreeData[], settings: SettingsData,
    rules: LevelRule[],
    targetLevel: number, onSelect: (payload: any) => void
}) => {
    const [allocations, setAllocations] = useState<Record<string, number>>({});
    const [selectedAttribute, setSelectedAttribute] = useState<string | null>(null);
    const [activeCategory, setActiveCategory] = useState<string>("All");

    const effSettings = getEffectiveSettings(settings, rules, targetLevel);

    const [isProcessing, setIsProcessing] = useState(false);
    const categories = settings?.categories || ["All", "Combat", "Magic", "Stealth", "Special", "Custom"];

    const totalSpent = Object.values(allocations).reduce((a, b) => a + b, 0);
    const pointsAvailable = effSettings.skillPointsPerLevel;
    const maxAllowed = Math.min(pointsAvailable, effSettings.maxSkillPointsSpendablePerLevel);
    const pointsRemaining = maxAllowed - totalSpent;

    const addPoint = (tree: SkillTreeData) => {
        const skillName = tree.name;
        const current = tree.currentLevel;
        const allocated = allocations[skillName] || 0;

        const cap = tree.cap || effSettings.skillCap || 100;

        if (pointsRemaining > 0 && (current + allocated < cap)) {
            setAllocations(prev => ({ ...prev, [skillName]: (prev[skillName] || 0) + 1 }));
        }
    };

    const removePoint = (skillName: string) => {
        if (allocations[skillName] > 0) {
            setAllocations(prev => ({ ...prev, [skillName]: prev[skillName] - 1 }));
        }
    };

    const handleConfirm = () => {
        if (!selectedAttribute || isProcessing) return;

        setIsProcessing(true);
        onSelect({
            attribute: selectedAttribute,
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
            <div className="skyrim-modal-content level-up-modal-advanced">
                <h2>{t('level_up.title')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '20px' }}></div>

                <div className="level-up-layout">
                    <div className="skill-allocation-section">
                        <h3>{t('level_up.allocate', { points: pointsRemaining })}</h3>

                        <div className="level-up-category-filters">
                            {categories.map(cat => (
                                <button
                                    key={cat}
                                    className={`level-up-category-btn ${activeCategory === cat ? 'active' : ''}`}
                                    onClick={() => setActiveCategory(cat)}
                                >
                                    {cat === "All" ? t('common.all').toUpperCase() : cat.toUpperCase()}
                                </button>
                            ))}
                        </div>

                        <div className="allocation-list-grid">
                            {filteredTrees.map(tree => {
                                const allocated = allocations[tree.name] || 0;
                                const cap = tree.cap || effSettings.skillCap || 100;
                                const isCapped = (tree.currentLevel + allocated) >= cap;

                                return (
                                    <div className="allocation-item" key={tree.name}>
                                        <div className="alloc-info">
                                            <span className="alloc-name">{(tree.displayName || tree.name).toUpperCase()}</span>
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

                    <div className="level-up-options-row">
                        {['Health', 'Magicka', 'Stamina'].map(attr => {
                            const isSelected = selectedAttribute === attr;
                            const incValue = attr === 'Health' ? effSettings.healthIncrease : attr === 'Magicka' ? effSettings.magickaIncrease : effSettings.staminaIncrease;

                            // Verifica se este botão dá carry weight
                            const cwInc = effSettings.carryWeightIncrease || 0;
                            const cwMethod = effSettings.carryWeightMethod || 'none';
                            const linked = effSettings.carryWeightLinkedAttributes || [];
                            const givesCW = cwInc > 0 && (cwMethod === 'auto' || (cwMethod === 'linked' && linked.includes(attr)));

                            return (
                                <div key={attr} style={{ position: 'relative', display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
                                    {/* "Janela de conversa" simulada como um pequeno balão sobre o botão */}
                                    {givesCW && (
                                        <div className="cw-bonus-bubble" style={{ opacity: isSelected ? 1 : 0.7 }}>
                                            {t('level_up.cw_bonus_tooltip', { amount: cwInc })}
                                            <div className="cw-bonus-triangle" />
                                        </div>
                                    )}
                                    <button
                                        className={`attribute-btn ${attr.toLowerCase()} ${isSelected ? 'selected-attr' : ''}`}
                                        onClick={() => setSelectedAttribute(attr)}
                                        style={{ marginTop: '10px' }}
                                    >
                                        {t(`header.${attr.toLowerCase()}`)} (+{incValue})
                                    </button>
                                </div>
                            );
                        })}
                    </div>
                </div>

                <div className="modal-actions" style={{ marginTop: '25px' }}>
                    <button
                        className={`modal-btn yes-btn ${(!selectedAttribute || isProcessing) ? 'disabled-btn' : ''}`}
                        onClick={handleConfirm}
                        disabled={!selectedAttribute || isProcessing}
                    >
                        {isProcessing ? t('common.processing') : t('level_up.confirm_btn')}
                    </button>
                </div>
            </div>
        </div>
    );
};

const ConfirmPerkModal = ({ perkName, cost, onConfirm, onCancel }: { perkName: string, cost: number, onConfirm: () => void, onCancel: () => void }) => {
    const plural = cost > 1 ? 's' : '';
    return (
        <div className="skyrim-modal-overlay">
            <div className="skyrim-modal-content">
                <h2>{t('unlock_perk.title')}</h2>
                <div className="tooltip-divider" style={{ width: '100%', marginBottom: '15px' }}></div>
                <p>{t('unlock_perk.message', { cost, plural, perkName })}</p>
                <div className="modal-actions">
                    <button className="modal-btn yes-btn" onClick={onConfirm}>{t('common.yes')}</button>
                    <button className="modal-btn no-btn" onClick={onCancel}>{t('common.no')}</button>
                </div>
            </div>
        </div>
    );
};

const PerkEditorModal = ({ node, availableTrees, formLists, availableReqs, onSave, onClose, onRequestBrowse }: {
    node: Partial<PerkNode>, availableTrees: string[], formLists: Record<string, AvailablePerk[]>, availableReqs: RequirementDef[], onSave: (n: PerkNode) => void,
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
                            <label>{t('perk_editor.cost')} <input type="number" name="perkCost" value={formData.perkCost || 1} onChange={handleChange} min={0} /></label>
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
                                    <div style={{ display: 'flex', gap: '10px', alignItems: 'center', marginBottom: '10px' }}>
                                        <strong style={{ color: '#4dd0e1' }}>{t('perk_editor.rank_label')} {idx + 2}:</strong>
                                        <span>{rank.name}</span>
                                        <button className="delete-btn" style={{ position: 'absolute', right: '10px', top: '10px' }} onClick={() => removeRank(idx)}>X</button>
                                    </div>
                                    <div className="settings-grid">
                                        <label>{t('perk_editor.base_engine')}
                                            <button className="form-selector-trigger-btn" onClick={() => setSelectingFormTarget(idx.toString())}>
                                                {rank.perk ? (formLists['perk']?.find(p => p.id === rank.perk)?.name || rank.perk) : t('common.click_select')}
                                            </button>
                                        </label>
                                        <label>{t('perk_editor.name_ui')} <input type="text" value={rank.name} onChange={e => updateRank(idx, 'name', e.target.value)} /></label>
                                        <label>{t('perk_editor.cost')} <input type="number" value={rank.perkCost || 1} onChange={e => updateRank(idx, 'perkCost', Number(e.target.value))} /></label>
                                        <label className="full-width" style={{ gridColumn: 'span 2' }}>{t('perk_editor.description')}
                                            <textarea
                                                value={rank.description}
                                                onChange={e => updateRank(idx, 'description', e.target.value)}
                                                className="settings-textarea"
                                            />
                                        </label>
                                    </div>
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
                                </div>
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
    const [, setIsLangLoading] = useState(false);
    const hoverDebounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

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

    const shouldScrollOnBack = useRef(false);
    const shouldShowLevelUp = useMemo(() => {
        return playerData?.isLevelUpMenuOpen && !isEditorMode && settings;
    }, [playerData?.isLevelUpMenuOpen, isEditorMode, settings]);

    const handleCloseDetail = useCallback((currentTreeName?: string) => {
        if (currentTreeName) {
            setHoveredSkillName(currentTreeName);
            shouldScrollOnBack.current = true;
        }
        setSelectedSkill(null);
    }, []);

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

        if (newUISettings.language !== currentLang) {
            loadLanguage(newUISettings.language);
        }

        setIsEditingUISettings(false);

        if (!newUISettings.enableEditorMode) {
            setIsEditorMode(false);
        }
    }, [currentLang, loadLanguage]);

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
            if (confirmAction) {
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
                // Se estiver dentro de uma constelação, volta pro carrossel
                handleCloseDetail(selectedSkill);
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
        closeMenuWithAnimation
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
            console.log(`[PrismaUI] Dados recebidos. Level: ${data.player.level}, MenuOpen: ${data.player.isLevelUpMenuOpen}`);

            if (data.fallbackTranslation && Object.keys(data.fallbackTranslation).length > 0) {
                addTranslation('en', data.fallbackTranslation);
            }

            if (data.uiSettings?.language) {
                const langFromSettings = data.uiSettings.language;

                if (data.activeTranslation && Object.keys(data.activeTranslation).length > 0) {
                    addTranslation(langFromSettings, data.activeTranslation);
                    console.log(`Tradução inicial carregada via payload: ${langFromSettings}`);
                }
                else if (langFromSettings !== 'en' && !hasTranslation(langFromSettings)) {
                    console.log(`Solicitando tradução tardia para: ${langFromSettings}`);
                    if (typeof (window as any).requestLocalization === 'function') {
                        (window as any).requestLocalization(langFromSettings);
                    }
                }

                setLanguage(langFromSettings);
                setCurrentLang(langFromSettings);
            }
            if (data.player && data.trees) {
                console.log(`[PrismaUI] Dados carregados. Jogador: ${data.player.name}, Level Atual: ${data.player.level}`);
                if (data.settings) setSettings(data.settings);
                if (data.rules) setRules(data.rules);
                if (data.uiSettings) setUiSettings(data.uiSettings);
                if (data.availableRequirements) setAvailableReqs(data.availableRequirements);
                if (data.formLists) setFormLists(data.formLists);
                if (data.availableLanguages) setAvailableLanguages(data.availableLanguages);
                setIsLoaded(false);
                setIsExiting(false);

                setPlayerData(data.player);
                setSkillTrees(data.trees);

                const pathsToLoad = new Set<string>();
                data.trees.forEach((tree: SkillTreeData) => {
                    if (tree.bgPath) pathsToLoad.add(tree.bgPath);
                    if (tree.iconPath) pathsToLoad.add(tree.iconPath);
                });

                const promises = Array.from(pathsToLoad).map(path => {
                    return new Promise<void>((resolve) => {
                        if (!path || path.trim() === "") resolve();
                        else if (path.toLowerCase().endsWith('.svg')) fetch(path).then(() => resolve()).catch(() => resolve());
                        else {
                            const img = new Image();
                            img.onload = () => resolve(); img.onerror = () => resolve();
                            img.src = path;
                        }
                    });
                });

                Promise.all(promises).then(() => {
                    requestAnimationFrame(() => requestAnimationFrame(() => {
                        setIsLoaded(true);
                    }));
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
            const cost = confirmingPerk.perkCost || 1;
            playSound('UISkillsPerkSelect2D');
            (window as any).unlockPerk(JSON.stringify({ id: confirmingPerk.perk, cost }));
        }
        setConfirmingPerk(null);
    }, [confirmingPerk]);

    const handleNodeClick = useCallback((node: PerkNode) => {
        if (isEditorMode) return;

        let targetPerkId = node.perk;
        let targetCost = node.perkCost || 1;
        let canUnlockTarget = node.canUnlock;
        let isTargetUnlocked = node.isUnlocked;
        let targetName = node.name;

        if (node.isUnlocked && node.nextRanks && node.nextRanks.length > 0) {
            const nextRank = node.nextRanks.find(r => !r.isUnlocked);
            if (nextRank) {
                targetPerkId = nextRank.perk;
                targetCost = nextRank.perkCost || 1;
                canUnlockTarget = nextRank.canUnlock;
                isTargetUnlocked = nextRank.isUnlocked;
                targetName = nextRank.name;
            }
        }

        if (canUnlockTarget && !isTargetUnlocked && playerData && playerData.perkPoints >= targetCost) {
            setConfirmingPerk({ ...node, name: targetName, perk: targetPerkId, perkCost: targetCost });
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

    useEffect(() => {
        if (emblaApi && !selectedSkill && shouldScrollOnBack.current && hoveredSkillName) {
            const index = filteredTrees.findIndex(t => t.name === hoveredSkillName);
            if (index !== -1) {
                setTimeout(() => {
                    emblaApi.scrollTo(index);
                    shouldScrollOnBack.current = false;
                }, 50);
            }
        }
    }, [emblaApi, selectedSkill, hoveredSkillName, filteredTrees]);

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
            {playerData && <PlayerHeader player={playerData} />}

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

            {!selectedSkill && (
                <div
                    className="skills-infinite-container embla"
                    ref={emblaRef}
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
                                    />
                                </div>
                            );
                        })}
                    </div>
                </div>
            )}

            {!selectedSkill && skillTrees.length > 0 && (
                <div className="bottom-ui-panel">
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
                />
            )}

            {confirmingPerk && !isEditorMode && (
                <ConfirmPerkModal
                    perkName={confirmingPerk.name}
                    cost={confirmingPerk.perkCost || 1}
                    onConfirm={handleUnlockPerkConfirm}
                    onCancel={() => setConfirmingPerk(null)}
                />
            )}

            {shouldShowLevelUp && (
                <LevelUpModal
                    key={playerData!.level}
                    trees={skillTrees}
                    rules={rules}
                    settings={settings}
                    targetLevel={(playerData!.level || 1) + 1}
                    onSelect={handleAttributeSelect}
                />
            )}

            {isEditingSettings && settings && (
                <SettingsModal
                    settings={settings}
                    rules={rules}
                    onClose={() => setIsEditingSettings(false)}
                    onSaveSettings={handleSaveSettings}
                    onSaveRules={handleSaveRules}
                    onResetAllPerks={handleResetAllRequest}
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
                    availableLanguages={availableLanguages}
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
        </div>
    );
}

export default App;