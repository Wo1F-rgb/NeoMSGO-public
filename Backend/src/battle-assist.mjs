// Shared catalogue-driven validation. The client never supplies slot capacity, effects or TIME.
export const emptyProgress = () => ({ gp: 0, revision: 0, developed_module_ids: [], materials: {} });
export const emptyAssist = () => ({ moduleIds: [], acModuleIds: [] });
export function validModuleDefinition(module, isNative = false) {
  const validName = value => typeof value === 'string' && value.length > 0 && value.toLowerCase() !== 'none';
  return Boolean(module && validName(module.family_id) && module.support === 'Implemented' && module.evidence === 'Confirmed'
    && Number.isInteger(module.slot_cost) && (isNative ? module.slot_cost === 0 : module.slot_cost >= 1 && module.slot_cost <= 10)
    && Array.isArray(module.effects) && module.effects.length
    && module.effects.every(effect => effect && Number.isFinite(effect.value) && effect.value >= 0 && validName(effect.unit)));
}
export function exactObject(value, keys, fail, code = 'invalid_fields') {
  if (!value || Array.isArray(value) || typeof value !== 'object' || Object.keys(value).some(k => !keys.includes(k)))
    fail(422, code, '未知のfieldまたは不正なオブジェクトです。');
}
export function validateProgress(value, modules, fail) {
  exactObject(value, ['gp', 'revision', 'developed_module_ids', 'materials'], fail, 'invalid_progress');
  if (!Number.isSafeInteger(value.gp) || value.gp < 0 || !Number.isSafeInteger(value.revision) || value.revision < 0
    || !Array.isArray(value.developed_module_ids) || value.developed_module_ids.length > 1024
    || new Set(value.developed_module_ids).size !== value.developed_module_ids.length
    || value.developed_module_ids.some(id => typeof id !== 'string' || !/^MSBattleAssistModuleData:[A-Za-z0-9_]+$/.test(id)))
    fail(422, 'invalid_progress', '開発原簿が不正です。');
  exactObject(value.materials, Object.keys(value.materials ?? {}), fail, 'invalid_progress');
  if (Object.keys(value.materials).length > 256 || Object.entries(value.materials).some(([k, v]) => !k || k.length > 128 || !Number.isSafeInteger(v) || v < 0 || v > 2147483647))
    fail(422, 'invalid_progress', '開発素材が不正です。');
  return structuredClone(value);
}
export function validateAssist(value, suit, catalog, modules, profiles, progress, fail) {
  exactObject(value, ['moduleIds', 'acModuleIds'], fail, 'invalid_battle_assist');
  if (!Array.isArray(value.moduleIds) || !Array.isArray(value.acModuleIds) || value.moduleIds.length > 10 || value.acModuleIds.length > 6)
    fail(422, 'invalid_battle_assist', 'モジュール枠が不正です。');
  if (value.acModuleIds.length) fail(422, 'invalid_ac', 'ACの最終期規則は未確認のため装備できません。');
  const ids = [...value.moduleIds, ...value.acModuleIds];
  if (new Set(ids).size !== ids.length) fail(422, 'duplicate_module', 'モジュールは重複して装備できません。');
  if (!suit) {
    if (ids.length) fail(422, 'empty_suit_with_module', '空の機体枠にモジュールは保存できません。');
    return emptyAssist();
  }
  const profile = profiles.get(suit.id);
  if (!profile && ids.length) fail(422, 'missing_module_profile', '機体のモジュール枠が未設定です。');
  const native = profile?.native_module_ids ?? [];
  const selected = [], seen = new Set(), families = new Set();
  let slots = 0;
  for (const [id, isNative] of [...native.map(id => [id, true]), ...ids.map(id => [id, false])]) {
    const module = typeof id === 'string' && /^MSBattleAssistModuleData:[A-Za-z0-9_]+$/.test(id) && modules.get(id);
    if (!module) fail(422, 'unknown_module', '未登録のモジュールです。');
    if (seen.has(id)) fail(422, 'duplicate_module', 'モジュールは重複して装備できません。');
    if (families.has(module.family_id)) fail(422, 'module_family_conflict', '同じ系列は同時に装備できません。');
    if (!Number.isInteger(module.slot_cost) || (isNative ? module.slot_cost !== 0 : module.slot_cost < 1) || module.slot_cost > 10)
      fail(422, 'invalid_module_slots', 'モジュールのスロット数が不正です。');
    if (!validModuleDefinition(module, isNative)) fail(422, 'unsupported_module', 'このモジュールは装備に未対応です。');
    if (module.allowed_unit_types?.length && !module.allowed_unit_types.includes(suit.unit_type))
      fail(422, 'module_class_restricted', 'この兵科では装備できません。');
    if (!isNative && !progress.developed_module_ids.includes(id)) fail(422, 'module_not_developed', '先にモジュールを開発してください。');
    for (const other of selected) {
      if (module.conflicting_families?.includes(other.family_id) || other.conflicting_families?.includes(module.family_id)
        || module.explicit_conflict_ids?.includes(other.id) || other.explicit_conflict_ids?.includes(id))
        fail(422, 'module_conflict', '同時に装備できない組み合わせです。');
    }
    seen.add(id); families.add(module.family_id); selected.push(module);
    if (!isNative) slots += module.slot_cost;
  }
  if (profile && (!Number.isInteger(profile.slot_capacity) || profile.slot_capacity < 0 || profile.slot_capacity > 10 || slots > profile.slot_capacity))
    fail(422, 'module_slot_overflow', 'モジュールの最大スロット数を超えています。');
  if (slots) {
    const row = catalog.time_rules?.find(row => row.cost === suit.cost);
    const base = suit.reuse_delay_seconds;
    if (!row || row.total_seconds?.length !== 11 || !Number.isFinite(base) || base <= 0
      || !(row.total_seconds[0] > 0) || !(row.total_seconds[slots] > 0)
      || !Number.isFinite(base + row.total_seconds[slots] - row.total_seconds[0]) || row.total_seconds[slots] < row.total_seconds[0])
      fail(422, 'invalid_module_time', 'モジュール装備後の再出撃時間を計算できません。');
  }
  return { moduleIds: [...value.moduleIds], acModuleIds: [...value.acModuleIds] };
}
