import test from 'node:test';
import assert from 'node:assert/strict';
import { DatabaseSync } from 'node:sqlite';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AccountStore } from '../src/store.mjs';
import { createServers, listen } from '../src/http.mjs';

const suit = 'MSMobileSuitData:DA_Test';
const gun = 'MSWeaponData:DA_TestGun', sword = 'MSWeaponData:DA_TestSword';
const deck = { schema_version: 1, selected_slot: 0, slots: [
  { suit_id: suit, weapon_ids: [gun, null, sword] },
  { suit_id: suit, weapon_ids: [null, sword, null] },
  { suit_id: null, weapon_ids: [null, null, null] },
  { suit_id: null, weapon_ids: [null, null, null] }] };
const catalog = { schema_version: 1, catalog_hash: 'fixture-v1', deck_cost_limit: 1720,
  suits: [{ id: suit, cost: 240, capacity: 5 }], weapons: [{ id: gun, weight: 8 }, { id: sword, weight: 3 }], default_deck: deck };
const server = { server_id: 'home', game_address: '127.0.0.1:7777', max_players: 12 };
const buildId = 'neomsgo-rooms-5';

async function fixture(t) {
  const folder = await mkdtemp(join(tmpdir(), 'neomsgo-room-test-'));
  let clock = 100000;
  const store = new AccountStore({ path: join(folder, 'accounts.sqlite'), catalog, server,
    debugVaultKey: Buffer.alloc(32, 7), now: () => clock });
  const { api, admin } = createServers({ store, serverKey: 'server-key-for-fixture', adminKey: 'admin-key-for-fixture', rateLimit: false });
  const apiAddress = await listen(api, 0, '127.0.0.1'), adminAddress = await listen(admin, 0, '127.0.0.1');
  const base = `http://127.0.0.1:${apiAddress.port}`;
  t.after(async () => {
    api.closeAllConnections(); admin.closeAllConnections();
    await Promise.all([api, admin].map(s => new Promise(done => s.close(done))));
    store.close();
  });
  async function request(pathname, { method = 'GET', data, token, lease, headers = {} } = {}) {
    // Normal ticket requests identify the running client; explicit undefined tests omission.
    if (method === 'POST' && /^\/v1\/rooms\/[^/]+\/tickets$/.test(pathname)) data = { build_id: buildId, ...data };
    const response = await fetch(base + pathname, { method, headers: {
      ...(data ? { 'Content-Type': 'application/json' } : {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}), ...(lease ? { 'X-MS-Room-Lease': lease } : {}), ...headers
    }, body: data ? JSON.stringify(data) : undefined });
    return { status: response.status, data: await response.json() };
  }
  return { store, request, advance: n => { clock += n; } };
}

const register = (f, username) => f.request('/v1/auth/register', { method: 'POST', data: { username, password: 'fixture-password-2026' } });
const room = (key, overrides = {}) => ({ name: 'Friend Room', map_id: 'Daibao', rule_id: 0, bot_count: 2, difficulty: 1,
  build_id: buildId, catalog_hash: catalog.catalog_hash, game_port: 17777, idempotency_key: key, ...overrides });
const filledRoom = (key, overrides = {}) => room(key, { fill_teams_with_bots: true, ...overrides });

test('new Taigan room is accepted while archived NewYark remains restorable', async t => {
  const f = await fixture(t);
  const currentOwner = await register(f, 'TaiganOwner');
  const archivedOwner = await register(f, 'OldCityOwner');
  const current = await f.request('/v1/rooms', { method: 'POST', token: currentOwner.data.token,
    data: room('taigan-map', { map_id: 'NewYorkTaigan' }) });
  assert.equal(current.status, 200);
  assert.equal(current.data.map_id, 'NewYorkTaigan');
  assert.equal(current.data.settings.map_id, 'NewYorkTaigan');
  const archived = await f.request('/v1/rooms', { method: 'POST', token: archivedOwner.data.token,
    data: room('archived-map', { map_id: 'NewYark' }) });
  assert.equal(archived.status, 200);
  assert.equal(archived.data.settings.map_id, 'NewYark');
  const unknown = await f.request('/v1/rooms', { method: 'POST', token: currentOwner.data.token,
    data: room('unknown-map', { map_id: 'NewYorkTaiganTypo' }) });
  assert.equal(unknown.status, 422);
  assert.equal(unknown.data.error.code, 'invalid_map_id');
});

test('Taklamakan Ruins prototype room is accepted separately from the unplayable desert entry', async t => {
  const f = await fixture(t);
  const owner = await register(f, 'RuinsOwner');
  const prototype = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('ruins-prototype', { map_id: 'TaklamakanRuinsPrototype' }) });
  assert.equal(prototype.status, 200);
  assert.equal(prototype.data.map_id, 'TaklamakanRuinsPrototype');
  assert.equal(prototype.data.settings.map_id, 'TaklamakanRuinsPrototype');
  const desert = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('unplayable-desert', { map_id: 'Taklamakan' }) });
  assert.equal(desert.status, 422);
  assert.equal(desert.data.error.code, 'invalid_map_id');
});

test('Mixed room difficulty persists as value 3 without changing rule validation', async t => {
  const f = await fixture(t);
  const owner = await register(f, 'MixedOwner');
  const mixed = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('mixed-room', { difficulty: 3 }) });
  assert.equal(mixed.status, 200);
  assert.equal(mixed.data.settings.difficulty, 3);
  assert.equal(f.store.db.prepare('SELECT difficulty FROM rooms WHERE room_id=?')
    .get(mixed.data.room_id).difficulty, 3);
  const invalidDifficulty = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('bad-difficulty', { difficulty: 4 }) });
  assert.equal(invalidDifficulty.status, 422);
  assert.equal(invalidDifficulty.data.error.code, 'invalid_difficulty');
  const invalidRule = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('bad-rule', { rule_id: 3 }) });
  assert.equal(invalidRule.status, 422);
  assert.equal(invalidRule.data.error.code, 'invalid_rule_id');
});

test('reaction-state build rejects old clients even after login gives them the new catalog hash', async t => {
  const f = await fixture(t), owner = await register(f, 'WireOwner'), guest = await register(f, 'WireGuest');
  assert.equal(owner.data.catalog_hash, catalog.catalog_hash);
  for (const clientBuild of ['neomsgo-rooms-4', 'neomsgo-rooms-1', undefined]) {
    const rejected = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
      data: room('old-create', { build_id: clientBuild }) });
    assert.equal(rejected.status, 409); assert.equal(rejected.data.error.code, 'build_mismatch');
  }
  const created = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token, data: room('wire-current') });
  assert.equal(created.status, 200); assert.equal(created.data.settings.build_id, buildId);
  const { room_id: id, instance_id: instance, room_lease: lease } = created.data;
  for (const clientBuild of ['neomsgo-rooms-4', 'neomsgo-rooms-1', undefined]) {
    const rejected = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: owner.data.token,
      data: { instance_id: instance, build_id: clientBuild } });
    assert.equal(rejected.status, 409); assert.equal(rejected.data.error.code, 'build_mismatch');
  }
  const hostTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: owner.data.token, data: { instance_id: instance } });
  assert.equal(hostTicket.status, 200);
  const oldConsume = await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease,
    data: { instance_id: instance, ticket: hostTicket.data.ticket, connection_id: 'old-wire-host',
      catalog_hash: owner.data.catalog_hash, build_id: 'neomsgo-rooms-4' } });
  assert.equal(oldConsume.status, 409); assert.equal(oldConsume.data.error.code, 'build_mismatch');
  assert.equal((await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease,
    data: { instance_id: instance, ticket: hostTicket.data.ticket, connection_id: 'wire-host', build_id: buildId } })).status, 200);
  assert.equal((await f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', lease,
    data: { instance_id: instance, state: 'Ready', players: 1, build_id: buildId } })).status, 200);
  const guestTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: guest.data.token, data: { instance_id: instance } });
  assert.equal(guestTicket.status, 200);
  assert.equal((await f.request('/v1/rooms')).data.length, 1);
  // Existing leases survive a process restart; simulate a room persisted by the previous build.
  f.store.db.prepare('UPDATE rooms SET build_id=? WHERE room_id=?').run('neomsgo-rooms-4', id);
  assert.deepEqual((await f.request('/v1/rooms')).data, []);
  for (const [endpoint, options] of [
    ['tickets', { token: guest.data.token, data: { instance_id: instance } }],
    ['heartbeat', { lease, data: { instance_id: instance, state: 'Ready', players: 1, build_id: 'neomsgo-rooms-4' } }],
    ['consume-ticket', { lease, data: { instance_id: instance, ticket: guestTicket.data.ticket, connection_id: 'wire-guest' } }]
  ]) {
    const rejected = await f.request(`/v1/rooms/${id}/${endpoint}`, { method: 'POST', ...options });
    assert.equal(rejected.status, 409); assert.equal(rejected.data.error.code, 'build_mismatch');
  }
  assert.equal((await f.request(`/v1/rooms/${id}/close`, { method: 'POST', lease, data: { instance_id: instance } })).status, 200);
});

test('rooms validate settings, derive the source address, and are idempotent per owner', async t => {
  const f = await fixture(t), a = await register(f, 'RoomOwner'), b = await register(f, 'RoomOwnerB');
  const created = await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('room-1') });
  assert.equal(created.status, 200);
  assert.match(created.data.room_id, /^[0-9a-f-]{36}$/);
  assert.match(created.data.instance_id, /^[0-9a-f-]{36}$/);
  assert.equal(created.data.game_address, '127.0.0.1:17777');
  assert.equal(created.data.settings.map_id, 'Daibao');
  assert.equal(created.data.settings.bot_count, 2);
  assert.equal(created.data.fill_teams_with_bots, false);
  assert.equal(created.data.settings.fill_teams_with_bots, false);
  assert.equal(created.data.max_players, 12);
  assert.equal(created.data.state, 'Starting');
  assert.equal(typeof created.data.room_lease, 'string');
  const retry = await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('room-1') });
  assert.equal(retry.status, 200);
  assert.equal(retry.data.room_id, created.data.room_id);
  assert.equal(retry.data.room_lease, created.data.room_lease);
  assert.equal((await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('room-1', { game_port: 17778 }) })).status, 409);
  assert.equal((await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('room-2') })).status, 409);
  const other = await f.request('/v1/rooms', { method: 'POST', token: b.data.token, data: room('room-1') });
  assert.equal(other.status, 200);
  assert.notEqual(other.data.room_id, created.data.room_id);
  for (const overrides of [{ map_id: 'Unknown' }, { bot_count: -1 }, { bot_count: 12 }, { bot_count: 1.5 },
    { fill_teams_with_bots: 'true' }, { fill_teams_with_bots: 1 }, { fill_teams_with_bots: null }]) {
    assert.equal((await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room(`bad-${JSON.stringify(overrides)}`, overrides) })).status, 422);
  }
  assert.equal((await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('bad-address', { address: '8.8.8.8:7777' }) })).status, 422);
  assert.equal((await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('bad-build', { build_id: 'old-build' }) })).status, 409);
  assert.equal((await f.request('/v1/rooms', { method: 'POST', data: room('no-auth') })).status, 401);
});

test('filled rooms use 50 human seats, expose dynamic bots, and keep idempotency mode-bound', async t => {
  const f = await fixture(t), owner = await register(f, 'FillOwner'), guest = await register(f, 'FillGuest'), late = await register(f, 'FillLate');
  const created = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: filledRoom('filled-room', { bot_count: 11 }) });
  assert.equal(created.status, 200);
  assert.equal(created.data.fill_teams_with_bots, true);
  assert.equal(created.data.settings.fill_teams_with_bots, true);
  assert.equal(created.data.settings.bot_count, 0);
  assert.equal(created.data.max_players, 50);
  assert.equal(created.data.bot_players, 0);

  const retry = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: filledRoom('filled-room', { bot_count: 2 }) });
  assert.equal(retry.status, 200);
  assert.equal(retry.data.room_id, created.data.room_id);
  const legacyRetry = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token,
    data: room('filled-room') });
  assert.equal(legacyRetry.status, 409);
  assert.equal(legacyRetry.data.error.code, 'idempotency_conflict');

  const { room_id: id, instance_id: instance, room_lease: lease } = created.data;
  const hostTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: owner.data.token,
    data: { instance_id: instance } });
  assert.equal(hostTicket.status, 200);
  assert.equal((await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease,
    data: { instance_id: instance, ticket: hostTicket.data.ticket, connection_id: 'fill-host' } })).status, 200);
  const heartbeat = data => f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', lease, data });
  assert.equal((await heartbeat({ instance_id: instance, state: 'Ready', players: { humans: 1, bots: 49 } })).status, 200);
  let listed = await f.request('/v1/rooms');
  assert.equal(listed.data[0].players, 1);
  assert.equal(listed.data[0].bot_players, 49);
  assert.equal(listed.data[0].bots, 49);
  assert.equal(listed.data[0].max_players, 50);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Playing', players: { humans: 2, bots: 48 } })).status, 200);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Playing', players: { humans: 1, bots: 49 } })).status, 200);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Playing', players: { humans: 26, bots: 25 } })).status, 422);

  // Forty-nine current humans leaves the 50th human seat available.
  assert.equal((await heartbeat({ instance_id: instance, state: 'Playing', players: { humans: 49, bots: 1 } })).status, 200);
  const fiftieth = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: guest.data.token,
    data: { instance_id: instance } });
  assert.equal(fiftieth.status, 200);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Playing', players: { humans: 50, bots: 0 } })).status, 200);
  const fiftyFirst = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: late.data.token,
    data: { instance_id: instance } });
  assert.equal(fiftyFirst.status, 409);
  assert.equal(fiftyFirst.data.error.code, 'room_full');
  listed = await f.request('/v1/rooms');
  assert.equal(listed.data[0].players, 50);
  assert.equal(listed.data[0].bot_players, 0);
});

test('old room databases migrate the fill column as legacy false', async t => {
  const folder = await mkdtemp(join(tmpdir(), 'neomsgo-room-migration-'));
  const path = join(folder, 'accounts.sqlite');
  const options = { path, catalog, server, debugVaultKey: Buffer.alloc(32, 7) };
  const initial = new AccountStore(options);
  initial.close();
  const legacy = new DatabaseSync(path);
  legacy.exec('ALTER TABLE rooms DROP COLUMN fill_teams_with_bots; PRAGMA user_version=3');
  assert.equal(legacy.prepare('PRAGMA table_info(rooms)').all().some(column => column.name === 'fill_teams_with_bots'), false);
  legacy.close();

  const migrated = new AccountStore(options);
  const columns = migrated.db.prepare('PRAGMA table_info(rooms)').all();
  const fillColumn = columns.find(column => column.name === 'fill_teams_with_bots');
  assert.ok(fillColumn);
  assert.equal(fillColumn.dflt_value, '0');
  assert.equal(migrated.db.prepare('PRAGMA user_version').get().user_version, 5);
  migrated.close();
});

test('starting rooms stay private, heartbeat makes them searchable, and lease/close are terminal', async t => {
  const f = await fixture(t), a = await register(f, 'LifecycleOwner'), b = await register(f, 'LifecycleOwnerB');
  const created = await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('lifecycle') });
  const id = created.data.room_id, instance = created.data.instance_id, lease = created.data.room_lease;
  assert.deepEqual(await f.request('/v1/rooms'), { status: 200, data: [] });
  const heartbeat = body => f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', lease, data: body });
  assert.equal((await heartbeat({ instance_id: instance, state: 'Ready', players: 1 })).status, 409);
  const hostTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: a.data.token,
    data: { instance_id: instance } });
  assert.equal(hostTicket.status, 200);
  assert.equal((await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease,
    data: { ticket: hostTicket.data.ticket, instance_id: instance, connection_id: 'host-bootstrap' } })).status, 200);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Ready', players: 1 })).status, 200);
  const listed = await f.request('/v1/rooms');
  assert.equal(listed.status, 200); assert.equal(listed.data.length, 1);
  assert.equal(listed.data[0].players, 1); assert.equal(listed.data[0].bot_players, 2);
  assert.equal((await f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', data: { instance_id: instance, state: 'Playing', players: 1 } })).status, 401);
  assert.equal((await f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', lease, data: { instance_id: '00000000-0000-4000-8000-000000000000', state: 'Playing', players: 1 } })).status, 409);
  assert.equal((await f.request(`/v1/rooms/${id}/close`, { method: 'POST', lease, data: { instance_id: instance } })).status, 200);
  assert.deepEqual((await f.request('/v1/rooms')).data, []);
  assert.equal((await heartbeat({ instance_id: instance, state: 'Ready', players: 1 })).status, 410);

  const starting = await f.request('/v1/rooms', { method: 'POST', token: b.data.token, data: room('starting-expiry') });
  const startingHeartbeat = () => f.request(`/v1/rooms/${starting.data.room_id}/heartbeat`, { method: 'POST', lease: starting.data.room_lease,
    data: { instance_id: starting.data.instance_id, state: 'Starting', players: 1 } });
  f.advance(120001);
  assert.equal((await startingHeartbeat()).status, 410);

  const active = await f.request('/v1/rooms', { method: 'POST', token: a.data.token, data: room('active-expiry') });
  const activeHeartbeat = body => f.request(`/v1/rooms/${active.data.room_id}/heartbeat`, { method: 'POST', lease: active.data.room_lease, data: body });
  const activeHostTicket = await f.request(`/v1/rooms/${active.data.room_id}/tickets`, { method: 'POST', token: a.data.token,
    data: { instance_id: active.data.instance_id } });
  assert.equal((await f.request(`/v1/rooms/${active.data.room_id}/consume-ticket`, { method: 'POST', lease: active.data.room_lease,
    data: { ticket: activeHostTicket.data.ticket, instance_id: active.data.instance_id, connection_id: 'active-host' } })).status, 200);
  assert.equal((await activeHeartbeat({ instance_id: active.data.instance_id, state: 'Ready', players: 1 })).status, 200);
  f.advance(10000);
  assert.equal((await activeHeartbeat({ instance_id: active.data.instance_id, state: 'Playing', players: 1 })).status, 200);
  f.advance(20001);
  assert.deepEqual((await f.request('/v1/rooms')).data, []);
  assert.equal((await activeHeartbeat({ instance_id: active.data.instance_id, state: 'Playing', players: { humans: 1, bots: 2 } })).status, 410);
});

test('room tickets bind lease, instance, session and deck revision, with one connection replay', async t => {
  const f = await fixture(t), owner = await register(f, 'TicketOwner');
  const guest = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  const created = await f.request('/v1/rooms', { method: 'POST', token: owner.data.token, data: room('tickets') });
  const id = created.data.room_id, instance = created.data.instance_id, lease = created.data.room_lease;
  const hostTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: owner.data.token, data: { instance_id: instance } });
  assert.equal(hostTicket.status, 200); assert.equal(hostTicket.data.server_id, id); assert.equal(hostTicket.data.instance_id, instance);
  assert.equal((await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: guest.data.token, data: { instance_id: instance } })).status, 409);
  assert.equal((await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease,
    data: { ticket: hostTicket.data.ticket, instance_id: instance, connection_id: 'host-connection' } })).status, 200);
  assert.equal((await f.request(`/v1/rooms/${id}/heartbeat`, { method: 'POST', lease, data: { instance_id: instance, state: 'Ready', players: 1 } })).status, 200);
  const guestTicket = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: guest.data.token, data: { instance_id: instance } });
  assert.equal(guestTicket.status, 200);
  const consume = data => f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease, data });
  assert.equal((await consume({ ticket: guestTicket.data.ticket, connection_id: 'connection-one' })).status, 200);
  const replay = await consume({ ticket: guestTicket.data.ticket, connection_id: 'connection-one' });
  assert.equal(replay.status, 200); assert.equal(replay.data.account_id, guest.data.account_id);
  assert.equal((await consume({ ticket: guestTicket.data.ticket, connection_id: 'connection-two' })).status, 409);
  const stale = await f.request(`/v1/rooms/${id}/tickets`, { method: 'POST', token: guest.data.token, data: { instance_id: instance } });
  assert.equal(stale.status, 200);
  const changed = structuredClone(deck); changed.selected_slot = 1;
  assert.equal((await f.request('/v1/me/deck', { method: 'PUT', token: guest.data.token, data: { expected_revision: 0, deck: changed } })).status, 200);
  assert.equal((await consume({ ticket: guestTicket.data.ticket, connection_id: 'connection-one' })).status, 200);
  assert.equal((await consume({ ticket: stale.data.ticket, connection_id: 'connection-three' })).status, 409);
  assert.equal((await consume({ ticket: hostTicket.data.ticket, connection_id: 'host-connection' })).status, 200);
  assert.equal((await f.request(`/v1/rooms/${id}/consume-ticket`, { method: 'POST', lease: created.data.room_lease, data: { ticket: hostTicket.data.ticket, connection_id: 'host-connection', instance_id: '00000000-0000-4000-8000-000000000000' } })).status, 409);
});
