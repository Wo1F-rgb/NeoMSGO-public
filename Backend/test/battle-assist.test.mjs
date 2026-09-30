import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AccountStore } from '../src/store.mjs';
import { createServers, listen } from '../src/http.mjs';

const suit = 'MSMobileSuitData:TestSuit';
const ids = ['ReloadI', 'ReloadII', 'ArmorI', 'Restricted', 'Unresolved', 'Conflict'].map(n => 'MSBattleAssistModuleData:' + n);
const assist = moduleIds => ({ moduleIds, acModuleIds: [] });
const deck = () => ({ schema_version: 3, selected_slot: 0, slots: Array.from({ length: 4 }, () =>
  ({ suit_id: suit, weapon_ids: [null, null, null], shield_equipped: false, battleAssist: assist([]) })) });
const recipe = (id, extra = {}) => ({ id, family_id: id, slot_cost: 1, allowed_unit_types: [], conflicting_families: [],
  explicit_conflict_ids: [], support: 'Implemented', evidence: 'Confirmed', recipe_known: true,
  development_cost_gp: 100, development_materials: { circuit: 2 }, effects: [{ kind: 'reload', value: 4, unit: 'percent' }], ...extra });
function catalog() {
  return { schema_version: 3, catalog_hash: 'battle-assist-test-v3', deck_cost_limit: 1720,
    suits: [{ id: suit, cost: 200, unit_type: 'assault', native_shield_id: null, shield_removable: true, reuse_delay_seconds: 50 }], weapons: [],
    modules: [recipe(ids[0], { family_id: 'reload' }), recipe(ids[1], { family_id: 'reload', slot_cost: 2 }),
      recipe(ids[2], { slot_cost: 2 }), recipe(ids[3], { allowed_unit_types: ['support'] }),
      recipe(ids[4], { support: 'UnresolvedValue' }), recipe(ids[5], { conflicting_families: ['reload'] })],
    suit_profiles: [{ suit_id: suit, slot_capacity: 3, ac_slot_capacity: 0, native_module_ids: [] }],
    time_rules: [{ cost: 200, total_seconds: [50, 54, 58, 62, 65, 68, 71, 74, 76, 78, 80] }], rules_revision: 1,
    default_deck: deck() };
}
async function fixture(t) {
  const folder = await mkdtemp(join(tmpdir(), 'neomsgo-battle-assist-'));
  const options = { path: join(folder, 'isolated.sqlite'), catalog: catalog(), server: { server_id: 'home', game_address: '127.0.0.1:7777' } };
  const store = new AccountStore(options);
  const { api, admin } = createServers({ store, serverKey: 'fixture-server', adminKey: 'fixture-admin', rateLimit: false });
  const address = await listen(api, 0, '127.0.0.1');
  const base = 'http://127.0.0.1:' + address.port;
  t.after(async () => { api.closeAllConnections(); await new Promise(r => api.close(r)); store.close(); });
  async function request(path, token, data) {
    const response = await fetch(base + path, { method: data ? 'POST' : 'GET', headers: {
      ...(token ? { Authorization: 'Bearer ' + token } : {}), ...(data ? { 'Content-Type': 'application/json' } : {}) },
      body: data ? JSON.stringify(data) : undefined });
    return { status: response.status, data: await response.json() };
  }
  const guest = store.guest();
  return { store, guest, request, options };
}
const grant = (f, gp = 1000, materials = { circuit: 20 }) => f.store.grantBattleAssistResources(f.guest.account_id, { expected_revision: 0, gp, materials });
const develops = (f, id, revision) => f.request('/v1/me/battle-assist/develop', f.guest.token, { module_id: id, expected_revision: revision });

test('shared UE/Node wire fixtures reject unknown fields, downgrade smuggling and malformed arrays', async t => {
  const f = await fixture(t);
  const cases = JSON.parse(await readFile(new URL('./fixtures/battle-assist-wire.json', import.meta.url), 'utf8'));
  for (const c of cases) {
    if (c.valid) assert.equal(f.store.validateDeck(c.deck).schema_version, 3, c.name);
    else assert.throws(() => f.store.validateDeck(c.deck), undefined, c.name);
  }
});

test('legacy read and empty progress GET leave account bytes, deck revision and ledger row count unchanged', async t => {
  const f = await fixture(t), old = deck(); old.schema_version = 2;
  for (const s of old.slots) delete s.battleAssist;
  f.store.db.prepare('UPDATE accounts SET deck_json=? WHERE id=?').run(JSON.stringify(old), f.guest.account_id);
  const before = f.store.db.prepare('SELECT * FROM accounts WHERE id=?').get(f.guest.account_id);
  assert.equal((await f.request('/v1/me/battle-assist', f.guest.token)).status, 200);
  assert.equal(f.store.getDeck(f.guest.token).deck.schema_version, 3);
  assert.deepEqual(f.store.db.prepare('SELECT * FROM accounts WHERE id=?').get(f.guest.account_id), before);
  assert.equal(f.store.db.prepare('SELECT count(*) AS n FROM battle_assist_progress').get().n, 0);
  assert.equal((await f.request('/v1/me/battle-assist')).status, 401);
});

test('parallel development charges once; all slots reuse one unlock and equip is free; disk reopen preserves order', async t => {
  const f = await fixture(t); grant(f);
  const results = await Promise.all(Array.from({ length: 6 }, () => develops(f, ids[0], 1)));
  for (const result of results) assert.equal(result.status, 200);
  const progress = f.store.getBattleAssistProgress(f.guest.token);
  assert.deepEqual(progress, { gp: 900, revision: 2, materials: { circuit: 18 }, developed_module_ids: [ids[0]] });
  const d = deck(); d.slots[0].battleAssist = assist([ids[0]]); d.slots[1].battleAssist = assist([ids[0]]);
  f.store.saveDeck(f.guest.token, { expected_revision: 0, deck: d });
  assert.deepEqual(f.store.getBattleAssistProgress(f.guest.token), progress);
  const other = new AccountStore(f.options);
  try {
    assert.deepEqual(other.getDeck(f.guest.token).deck.slots[0].battleAssist, assist([ids[0]]));
    assert.deepEqual(other.getBattleAssistProgress(f.guest.token), progress);
  } finally { other.close(); }
  assert.equal(f.store.db.prepare("SELECT count(*) AS n FROM audit WHERE action='battle_assist_develop'").get().n, 1);
});

test('insufficient funds/materials, stale revision and injected fields preserve GP/material/unlock state', async t => {
  const f = await fixture(t);
  assert.equal((await develops(f, ids[0], 0)).status, 422);
  grant(f, 1000, { circuit: 1 });
  const before = f.store.getBattleAssistProgress(f.guest.token);
  assert.equal((await develops(f, ids[0], 1)).status, 422);
  assert.equal((await develops(f, ids[0], 0)).status, 409);
  assert.equal((await f.request('/v1/me/battle-assist/develop', f.guest.token, { module_id: ids[0], expected_revision: 1, gp: 999 })).status, 422);
  assert.deepEqual(f.store.getBattleAssistProgress(f.guest.token), before);
  assert.equal((await f.request('/admin/accounts/' + f.guest.account_id + '/grant-battle-assist', f.guest.token, { gp: 1 })).status, 404);
});

test('different concurrent purchases serialize revision and database failure rolls back complete transaction', async t => {
  const f = await fixture(t); grant(f);
  const results = await Promise.all([develops(f, ids[0], 1), develops(f, ids[2], 1)]);
  assert.deepEqual(results.map(r => r.status).sort(), [200, 409]);
  const before = f.store.getBattleAssistProgress(f.guest.token), nextId = before.developed_module_ids.includes(ids[0]) ? ids[2] : ids[0];
  f.store.db.exec("CREATE TRIGGER reject_develop BEFORE INSERT ON audit WHEN NEW.action='battle_assist_develop' BEGIN SELECT RAISE(ABORT,'test failure after ledger write'); END");
  assert.equal((await develops(f, nextId, before.revision)).status, 500);
  assert.deepEqual(f.store.getBattleAssistProgress(f.guest.token), before);
});

test('corrupt implemented definitions never charge or unlock an unequippable module', async t => {
  const f = await fixture(t); grant(f);
  const before = f.store.getBattleAssistProgress(f.guest.token);
  const definitions = [
    { effects: [] }, { effects: [{ value: -1, unit: 'percent' }] },
    { effects: [{ value: NaN, unit: 'percent' }] }, { effects: [{ value: Infinity, unit: 'percent' }] },
    { effects: [{ value: 4, unit: '' }] }, { effects: [{ value: 4, unit: 'None' }] },
    { family_id: '' }, { family_id: 'None' }, { slot_cost: 0 }, { slot_cost: -1 }, { slot_cost: 11 }, { slot_cost: 1.5 }
  ];
  for (const corrupt of definitions) {
    f.store.modules.set(ids[0], recipe(ids[0], corrupt));
    const result = await develops(f, ids[0], before.revision);
    assert.equal(result.status, 422);
    assert.equal(result.data.error.code, 'unsupported_module');
    assert.deepEqual(f.store.getBattleAssistProgress(f.guest.token), before);
  }
  assert.equal(f.store.db.prepare("SELECT count(*) AS n FROM audit WHERE action='battle_assist_develop'").get().n, 0);
});

test('catalog restrictions, support, family, capacity, native conflicts and TIME reject forged equipment', async t => {
  const f = await fixture(t), p = { gp: 0, revision: 0, materials: {}, developed_module_ids: ids };
  const reject = (moduleIds, code) => {
    const d = deck(); d.slots[0].battleAssist = assist(moduleIds);
    assert.throws(() => f.store.validateDeck(d, p), e => e.code === code);
  };
  reject([ids[0], ids[0]], 'duplicate_module'); reject([ids[0], ids[1]], 'module_family_conflict');
  reject([ids[1], ids[2]], 'module_slot_overflow'); reject([ids[3]], 'module_class_restricted');
  reject([ids[4]], 'unsupported_module'); reject([ids[0], ids[5]], 'module_conflict');
  reject(['MSWeaponData:ReloadI'], 'unknown_module');
  const d = deck(); d.slots[0].battleAssist = assist([ids[0]]);
  assert.throws(() => f.store.validateDeck(d), e => e.code === 'module_not_developed');
  f.store.moduleProfiles.get(suit).native_module_ids = [ids[5]];
  assert.throws(() => f.store.validateDeck(d, p), e => e.code === 'invalid_module_slots');
  f.store.modules.get(ids[5]).slot_cost = 0;
  assert.throws(() => f.store.validateDeck(d, p), e => e.code === 'module_conflict');
  f.store.moduleProfiles.get(suit).native_module_ids = [];
  f.store.catalog.time_rules[0].total_seconds[1] = 0;
  assert.throws(() => f.store.validateDeck(d, p), e => e.code === 'invalid_module_time');
  d.slots[0].battleAssist.acModuleIds = [ids[2]];
  assert.throws(() => f.store.validateDeck(d, p), e => e.code === 'invalid_ac');
});

test('issued ticket retains admitted modules after account unequip and reconnect replay', async t => {
  const f = await fixture(t); grant(f); await develops(f, ids[0], 1);
  const d = deck(); d.slots[0].battleAssist = assist([ids[0]]);
  f.store.saveDeck(f.guest.token, { expected_revision: 0, deck: d });
  f.store.heartbeat({ server_id: 'home', catalog_hash: f.store.catalog.catalog_hash, players: 0 });
  const ticket = f.store.issueTicket(f.guest.token, { server_id: 'home' });
  f.store.saveDeck(f.guest.token, { expected_revision: 1, deck: deck() });
  const request = { ticket: ticket.ticket, connection_id: 'test-admitted-connection-123', server_id: 'home', catalog_hash: f.store.catalog.catalog_hash };
  const admitted = f.store.consumeTicket(request);
  assert.deepEqual(admitted.deck.slots[0].battleAssist, assist([ids[0]]));
  assert.deepEqual(f.store.consumeTicket(request).deck, admitted.deck);
  assert.deepEqual(f.store.getDeck(f.guest.token).deck.slots[0].battleAssist, assist([]));
});
