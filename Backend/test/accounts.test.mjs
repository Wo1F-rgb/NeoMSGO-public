import test from 'node:test';
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { AccountStore, ApiError } from '../src/store.mjs';
import { createServers, listen } from '../src/http.mjs';

const suit = 'MSMobileSuitData:DA_Test';
const gun = 'MSWeaponData:DA_TestGun', sword = 'MSWeaponData:DA_TestSword', shield = 'MSWeaponData:DA_TestShield';
const deck = { schema_version: 1, selected_slot: 0, slots: [
  { suit_id: suit, weapon_ids: [gun, null, sword] },
  { suit_id: suit, weapon_ids: [null, sword, null] },
  { suit_id: null, weapon_ids: [null, null, null] },
  { suit_id: null, weapon_ids: [null, null, null] }] };
const catalog = { schema_version: 2, catalog_hash: 'fixture-v2', deck_cost_limit: 1720,
  suits: [{ id: suit, cost: 240, capacity: 5, native_shield_id: shield, shield_removable: true }],
  weapons: [{ id: gun, weight: 8, is_shield: false }, { id: sword, weight: 3, is_shield: false }, { id: shield, weight: 0, is_shield: true }],
  default_deck: deck };
const server = { server_id: 'home', game_address: '127.0.0.1:7777', max_players: 12 };
const serverKey = 'test-server-key-not-a-production-credential';
const adminKey = 'test-admin-key-not-a-production-credential';
async function fixture(t) {
  const folder = await mkdtemp(join(tmpdir(), 'neomsgo-online-test-'));
  const path = join(folder, 'accounts.sqlite');
  let clock = 100000;
  const store = new AccountStore({ path, catalog, server, debugVaultKey: Buffer.alloc(32, 7), now: () => clock });
  const { api, admin } = createServers({ store, serverKey, adminKey, rateLimit: false });
  const apiAddress = await listen(api, 0, '127.0.0.1'), adminAddress = await listen(admin, 0, '127.0.0.1');
  const apiUrl = `http://127.0.0.1:${apiAddress.port}`, adminUrl = `http://127.0.0.1:${adminAddress.port}`;
  t.after(async () => {
    api.closeAllConnections(); admin.closeAllConnections();
    await Promise.all([api, admin].map(s => new Promise(done => s.close(done)))); store.close();
  });
  async function request(pathname, { method = 'GET', data, token, headers = {}, adminRequest = false } = {}) {
    const base = adminRequest ? adminUrl : apiUrl;
    const response = await fetch(base + pathname, { method, headers: { ...(data ? { 'Content-Type': 'application/json' } : {}),
      ...(token ? { Authorization: `Bearer ${token}` } : {}), ...headers }, body: data ? JSON.stringify(data) : undefined });
    const result = await response.json(); return { status: response.status, data: result, headers: response.headers };
  }
  return { store, path, request, adminUrl, advance: n => { clock += n; } };
}
const register = (f, username) => f.request('/v1/auth/register', { method: 'POST', data: { username, password: 'fixture-password-2026' } });
const heartbeat = f => f.request('/v1/server/heartbeat', { method: 'POST', headers: { 'X-NeoMSGO-Server-Key': serverKey },
  data: { server_id: 'home', catalog_hash: catalog.catalog_hash, players: 0 } });

test('disabling a guest permanently revokes resume credentials even after re-enabling', async t => {
  const f = await fixture(t);
  const guest = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  const auth = await f.request('/admin/login', { method: 'POST', adminRequest: true, headers: { Origin: f.adminUrl }, data: { key: adminKey } });
  const headers = { Origin: f.adminUrl, Cookie: auth.headers.get('set-cookie').split(';')[0] };
  for (const disabled of [true, false]) {
    const result = await f.request(`/admin/accounts/${guest.data.account_id}/disable`, { method: 'POST', adminRequest: true, headers, data: { disabled } });
    assert.equal(result.status, 200);
  }
  const resumed = await f.request('/v1/auth/resume', { method: 'POST', data: { resume_token: guest.data.guest_resume_token } });
  assert.equal(resumed.status, 401);
  assert.equal((await f.request('/v1/me', { token: guest.data.token })).status, 401);
});

test('real HTTP account isolation, sparse slots, revision conflict and database reopening', async t => {
  const f = await fixture(t), a = await register(f, 'Test_A'), b = await register(f, 'Test_B');
  assert.equal(a.status, 200); assert.equal(b.status, 200);
  assert.notEqual(a.data.account_id, b.data.account_id);
  const changed = structuredClone(deck); changed.selected_slot = 1; changed.slots[0].weapon_ids = [null, gun, null];
  const saved = await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token, data: { expected_revision: 0, deck: changed } });
  assert.equal(saved.status, 200); assert.equal(saved.data.deck.revision, 1);
  assert.deepEqual(saved.data.deck.slots[0].weapon_ids, [null, gun, null]);
  const readB = await f.request('/v1/me/deck', { token: b.data.token });
  assert.deepEqual(readB.data.deck.slots[0].weapon_ids, [gun, null, sword]);
  const collision = await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token, data: { expected_revision: 0, deck } });
  assert.equal(collision.status, 409); assert.equal(collision.data.current_revision, 1);
  assert.equal((await f.request('/v1/me/deck')).status, 401);
  const secondStore = new AccountStore({ path: f.path, catalog, server, now: () => 100000 });
  try { assert.equal(secondStore.realmId, a.data.realm_id); assert.equal(secondStore.getDeck(a.data.token).deck.selected_slot, 1); }
  finally { secondStore.close(); }
  const row = f.store.db.prepare('SELECT password_hash FROM accounts WHERE id=?').get(a.data.account_id);
  assert.match(row.password_hash, /^scrypt\$131072\$8\$1\$/);
  assert.equal(row.password_hash.includes('fixture-password'), false);
});

test('guest resume and promotion keep one account and loadout; stale guest token is revoked', async t => {
  const f = await fixture(t), guest = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  assert.equal(guest.status, 200);
  const changed = structuredClone(deck); changed.selected_slot = 1;
  assert.equal((await f.request('/v1/me/deck', { method: 'PUT', token: guest.data.token, data: { expected_revision: 0, deck: changed } })).status, 200);
  const resumed = await f.request('/v1/auth/resume', { method: 'POST', data: { resume_token: guest.data.guest_resume_token } });
  assert.equal(resumed.data.account_id, guest.data.account_id); assert.equal(resumed.data.deck.selected_slot, 1);
  const upgraded = await f.request('/v1/auth/upgrade', { method: 'POST', token: resumed.data.token, data: { username: 'GuestPromoted', password: 'fixture-password-2026' } });
  assert.equal(upgraded.status, 200); assert.equal(upgraded.data.is_guest, false);
  assert.equal(upgraded.data.account_id, guest.data.account_id); assert.equal(upgraded.data.deck.selected_slot, 1);
  assert.equal((await f.request('/v1/me', { token: guest.data.token })).status, 401);
  assert.equal((await f.request('/v1/auth/resume', { method: 'POST', data: { resume_token: guest.data.guest_resume_token } })).status, 401);
  const login = await f.request('/v1/auth/login', { method: 'POST', data: { username: 'GUESTPROMOTED', password: 'fixture-password-2026' } });
  assert.equal(login.data.account_id, guest.data.account_id);
});

test('admin guest recovery rejects missing admin proof, mismatches, regular accounts and disabled guests', async t => {
  const f = await fixture(t), guest = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  const normal = await register(f, 'RecoveryNormal');
  const auth = await f.request('/admin/login', { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl }, data: { key: adminKey } });
  assert.equal(auth.status, 200);
  const cookie = auth.headers.get('set-cookie').split(';')[0];
  const path = `/admin/accounts/${guest.data.account_id}/recover-guest`;
  const recovery = { expected_display_name: guest.data.display_name, expected_revision: guest.data.deck.revision };
  assert.equal((await f.request(path, { method: 'POST', data: recovery })).status, 404);
  assert.equal((await f.request(path, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl }, data: recovery })).status, 401);
  assert.equal((await f.request(path, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: {} })).status, 422);
  assert.equal((await f.request(path, { method: 'POST', adminRequest: true,
    headers: { Origin: 'https://untrusted.invalid', Cookie: cookie }, data: recovery })).status, 403);
  const normalRecovery = await f.request(`/admin/accounts/${normal.data.account_id}/recover-guest`, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { expected_display_name: normal.data.display_name, expected_revision: normal.data.deck.revision } });
  assert.equal(normalRecovery.status, 404);
  const wrongName = await f.request(path, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { ...recovery, expected_display_name: `${recovery.expected_display_name}-wrong` } });
  assert.equal(wrongName.status, 409);
  const wrongRevision = await f.request(path, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { ...recovery, expected_revision: recovery.expected_revision + 1 } });
  assert.equal(wrongRevision.status, 409);
  const disabled = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  assert.equal((await f.request(`/admin/accounts/${disabled.data.account_id}/disable`, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { disabled: true } })).status, 200);
  const disabledRecovery = await f.request(`/admin/accounts/${disabled.data.account_id}/recover-guest`, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { expected_display_name: disabled.data.display_name, expected_revision: disabled.data.deck.revision } });
  assert.equal(disabledRecovery.status, 404);
});

test('admin guest recovery appends a resume credential and preserves session, deck and revision', async t => {
  const f = await fixture(t), guest = await f.request('/v1/auth/guest', { method: 'POST', data: {} });
  const changed = structuredClone(deck); changed.selected_slot = 1;
  const saved = await f.request('/v1/me/deck', { method: 'PUT', token: guest.data.token,
    data: { expected_revision: 0, deck: changed } });
  assert.equal(saved.status, 200); assert.equal(saved.data.deck.revision, 1);
  const before = f.store.db.prepare('SELECT deck_json,revision,display_name,disabled FROM accounts WHERE id=?').get(guest.data.account_id);
  const beforeCredentialCount = f.store.db.prepare('SELECT COUNT(*) AS n FROM guest_credentials WHERE account_id=?').get(guest.data.account_id).n;
  const beforeSessionCount = f.store.db.prepare('SELECT COUNT(*) AS n FROM sessions WHERE account_id=?').get(guest.data.account_id).n;
  const auth = await f.request('/admin/login', { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl }, data: { key: adminKey } });
  const cookie = auth.headers.get('set-cookie').split(';')[0];
  const recovered = await f.request(`/admin/accounts/${guest.data.account_id}/recover-guest`, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: { expected_display_name: guest.data.display_name, expected_revision: 1 } });
  assert.equal(recovered.status, 200);
  assert.equal(recovered.data.account_id, guest.data.account_id);
  assert.equal(recovered.data.display_name, guest.data.display_name);
  assert.equal(typeof recovered.data.guest_resume_token, 'string');
  assert.notEqual(recovered.data.guest_resume_token, guest.data.guest_resume_token);
  const after = f.store.db.prepare('SELECT deck_json,revision,display_name,disabled FROM accounts WHERE id=?').get(guest.data.account_id);
  assert.deepEqual(after, before);
  assert.equal(f.store.db.prepare('SELECT COUNT(*) AS n FROM guest_credentials WHERE account_id=?').get(guest.data.account_id).n, beforeCredentialCount + 1);
  assert.equal(f.store.db.prepare('SELECT COUNT(*) AS n FROM sessions WHERE account_id=?').get(guest.data.account_id).n, beforeSessionCount);
  const hashes = f.store.db.prepare('SELECT hash FROM guest_credentials WHERE account_id=?').all(guest.data.account_id).map(row => row.hash);
  assert.equal(hashes.includes(createHash('sha256').update(guest.data.guest_resume_token).digest('hex')), true);
  assert.equal(hashes.includes(createHash('sha256').update(recovered.data.guest_resume_token).digest('hex')), true);
  const oldSession = await f.request('/v1/me', { token: guest.data.token });
  assert.equal(oldSession.status, 200); assert.equal(oldSession.data.account_id, guest.data.account_id);
  const resumed = await f.request('/v1/auth/resume', { method: 'POST', data: { resume_token: recovered.data.guest_resume_token } });
  assert.equal(resumed.status, 200); assert.equal(resumed.data.account_id, guest.data.account_id);
  assert.equal(resumed.data.deck.selected_slot, 1); assert.equal(resumed.data.deck.revision, 1);
  const oldResumed = await f.request('/v1/auth/resume', { method: 'POST', data: { resume_token: guest.data.guest_resume_token } });
  assert.equal(oldResumed.status, 200); assert.equal(oldResumed.data.account_id, guest.data.account_id);
  const events = f.store.db.prepare("SELECT action,detail FROM audit WHERE action='guest_recovery'").all();
  assert.equal(events.length, 1); assert.equal(events[0].detail, '{}');
  assert.equal(JSON.stringify(events).includes(recovered.data.guest_resume_token), false);
});

test('ticket consume is server-only, expires, binds one connection and snapshots account deck', async t => {
  const f = await fixture(t), a = await register(f, 'TicketUser');
  assert.equal((await f.request('/v1/game/tickets', { method: 'POST', token: a.data.token, data: { server_id: 'home' } })).status, 503);
  assert.equal((await heartbeat(f)).status, 200);
  const issued = await f.request('/v1/game/tickets', { method: 'POST', token: a.data.token, data: { server_id: 'home' } });
  assert.equal(issued.status, 200);
  const data = { ticket: issued.data.ticket, connection_id: 'server-generated-connection-1', server_id: 'home', catalog_hash: catalog.catalog_hash };
  assert.equal((await f.request('/v1/server/tickets/consume', { method: 'POST', data })).status, 401);
  const options = { method: 'POST', data, headers: { 'X-NeoMSGO-Server-Key': serverKey } };
  const accepted = await f.request('/v1/server/tickets/consume', options);
  assert.equal(accepted.status, 200); assert.equal(accepted.data.account_id, a.data.account_id);
  assert.deepEqual(accepted.data.deck.slots[0].weapon_ids, [gun, null, sword]);
  assert.equal((await f.request('/v1/server/tickets/consume', options)).status, 200);
  assert.equal((await f.request('/v1/server/tickets/consume', { ...options, data: { ...data, connection_id: 'server-generated-connection-2' } })).status, 409);
  f.advance(60001);
  assert.equal((await f.request('/v1/server/tickets/consume', options)).status, 401);
});

test('malformed and duplicate IDs fail without replacing deck; capacity warning remains saveable', async t => {
  const f = await fixture(t), a = await register(f, 'ValidationUser');
  // Fixture is deliberately overweight: its pre-existing rule is warning, not rejection.
  assert.equal(a.status, 200);
  const bad = structuredClone(deck); bad.slots[0].weapon_ids = [gun, gun, null];
  const failed = await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token, data: { expected_revision: 0, deck: bad } });
  assert.equal(failed.status, 422); assert.equal(failed.data.error.code, 'duplicate_weapon');
  bad.slots[0].suit_id = '/Game/Arbitrary/Asset'; bad.slots[0].weapon_ids = [gun, null, sword];
  assert.equal((await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token, data: { expected_revision: 0, deck: bad } })).status, 422);
  assert.equal((await f.request('/v1/me/deck', { token: a.data.token })).data.deck.revision, 0);
});

test('shield migration preserves weapon indices and canonical writes keep shield independent', async t => {
  const f = await fixture(t), a = await register(f, 'ShieldMigrationUser');
  f.store.db.prepare('UPDATE accounts SET deck_json=? WHERE id=?').run(JSON.stringify(deck), a.data.account_id);
  const legacyBytes = f.store.db.prepare('SELECT deck_json FROM accounts WHERE id=?').get(a.data.account_id).deck_json;
  const legacyRead = f.store.getDeck(a.data.token);
  assert.equal(legacyRead.deck.schema_version, 3);
  assert.equal(legacyRead.deck.slots[0].shield_equipped, false);
  assert.equal(f.store.db.prepare('SELECT deck_json FROM accounts WHERE id=?').get(a.data.account_id).deck_json, legacyBytes);
  const legacy = structuredClone(deck);
  legacy.slots[0].weapon_ids = [gun, shield, sword];
  const migrated = await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token,
    data: { expected_revision: 0, deck: legacy } });
  assert.equal(migrated.status, 200);
  assert.equal(migrated.data.deck.schema_version, 3);
  assert.deepEqual(migrated.data.deck.slots[0].weapon_ids, [gun, null, sword]);
  assert.equal(migrated.data.deck.slots[0].shield_equipped, true);
  assert.deepEqual(migrated.data.deck.slots[1].weapon_ids, [null, sword, null]);

  const exploit = structuredClone(migrated.data.deck);
  exploit.slots[0].shield_equipped = false;
  exploit.slots[0].weapon_ids[1] = shield;
  const rejected = await f.request('/v1/me/deck', { method: 'PUT', token: a.data.token,
    data: { expected_revision: 1, deck: exploit } });
  assert.equal(rejected.status, 422);
  assert.equal((await f.request('/v1/me/deck', { token: a.data.token })).data.deck.revision, 1);

  const duplicateLegacy = structuredClone(legacy);
  duplicateLegacy.slots[0].weapon_ids = [shield, null, shield];
  assert.throws(() => f.store.validateDeck(duplicateLegacy), error => error instanceof ApiError && error.code === 'legacy_shield_mismatch');
});

test('logout revokes tickets and password reset invalidates sessions; admin is isolated from game API', async t => {
  const f = await fixture(t), a = await register(f, 'AdminTestUser');
  await heartbeat(f);
  const issued = await f.request('/v1/game/tickets', { method: 'POST', token: a.data.token, data: { server_id: 'home' } });
  assert.equal((await f.request('/v1/auth/logout', { method: 'POST', token: a.data.token })).status, 200);
  assert.equal((await f.request('/v1/server/tickets/consume', { method: 'POST', headers: { 'X-NeoMSGO-Server-Key': serverKey },
    data: { ticket: issued.data.ticket, connection_id: 'server-connection-logout', server_id: 'home', catalog_hash: catalog.catalog_hash } })).status, 401);
  assert.equal((await f.request('/admin/accounts')).status, 404);
  const auth = await f.request('/admin/login', { method: 'POST', adminRequest: true, headers: { Origin: f.adminUrl }, data: { key: adminKey } });
  assert.equal(auth.status, 200);
  const cookie = auth.headers.get('set-cookie').split(';')[0];
  const listing = await f.request('/admin/accounts', { adminRequest: true, headers: { Cookie: cookie } });
  assert.equal(listing.status, 200);
  assert.equal(JSON.stringify(listing.data).includes('password_hash'), false);
  const reset = await f.request(`/admin/accounts/${a.data.account_id}/reset-password`, { method: 'POST', adminRequest: true,
    headers: { Origin: f.adminUrl, Cookie: cookie }, data: {} });
  assert.equal(reset.status, 200);
  const old = await f.request('/v1/auth/login', { method: 'POST', data: { username: 'AdminTestUser', password: 'fixture-password-2026' } });
  assert.equal(old.status, 401);
  const replacement = await f.request('/v1/auth/login', { method: 'POST', data: { username: 'AdminTestUser', password: reset.data.temporary_password } });
  assert.equal(replacement.status, 200);
  const foreign = await f.request(`/admin/accounts/${a.data.account_id}/disable`, { method: 'POST', adminRequest: true,
    headers: { Origin: 'https://untrusted.invalid', Cookie: cookie }, data: { disabled: true } });
  assert.equal(foreign.status, 403);
  const events = f.store.db.prepare('SELECT detail FROM audit').all();
  assert.equal(JSON.stringify(events).includes(reset.data.temporary_password), false);
});

test('session expiration and wrong catalog are rejected at real HTTP boundaries', async t => {
  const f = await fixture(t), a = await register(f, 'ExpiryUser');
  assert.equal((await f.request('/v1/server/heartbeat', { method: 'POST', headers: { 'X-NeoMSGO-Server-Key': serverKey },
    data: { server_id: 'home', catalog_hash: 'other-build', players: 0 } })).status, 409);
  f.advance(24 * 3600000 + 1);
  assert.equal((await f.request('/v1/me', { token: a.data.token })).status, 401);
  assert.throws(() => f.store.validateDeck({ schema_version: 1, selected_slot: 4, slots: deck.slots }), ApiError);
});

test('only explicitly created debug accounts have recoverable credentials; reset updates encrypted vault', async t => {
  const f = await fixture(t);
  const normal = await register(f, 'NormalAccount'); assert.equal(normal.status, 200);
  const debug = await f.store.createTestAccount({ username: 'debug_player1' });
  const credentials = f.store.listTestCredentials();
  assert.equal(credentials.length, 1); assert.equal(credentials[0].account_id, debug.account_id);
  assert.equal(credentials[0].password, debug.password);
  const sealed = f.store.db.prepare('SELECT sealed FROM test_credentials WHERE account_id=?').get(debug.account_id).sealed;
  assert.equal(sealed.includes(debug.password), false);
  assert.equal((await f.request('/v1/auth/login', { method: 'POST', data: { username: debug.username, password: debug.password } })).status, 200);
  const reset = await f.store.resetPassword(debug.account_id);
  assert.equal(f.store.listTestCredentials()[0].password, reset.temporary_password);
  assert.equal((await f.request('/admin/test-accounts')).status, 404);
  assert.equal((await f.request('/v1/me', { token: normal.data.token })).data.password, undefined);
});
