const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function dashboard(responses = []) {
  const source = fs.readFileSync(path.join(__dirname, '../src/page.h'), 'utf8');
  let script = source.split('<script>')[1].split('</script>')[0];
  script = script.replace(/loadStats\(\);requestLog\(\);setInterval[\s\S]*$/, 'globalThis.api={state,renderLists,loadLists,listsAction,applyProfile,checkLists,changeAllowlist,renderAllowed,renderLog,uploadOptions};');
  const nodes = new Map();
  const requests = [];
  const makeNode = id => {
    const children = [];
    return {
      id, children, textContent: '', value: '', disabled: false, hidden: false,
      dataset: {}, colSpan: 1,
      classList: {toggle() {}, add() {}, remove() {}},
      append(...items) { children.push(...items); },
      appendChild(item) { children.push(item); return item; },
      removeChild(item) { const index = children.indexOf(item); if (index >= 0) children.splice(index, 1); return item; },
      get firstChild() { return children[0]; },
      addEventListener() {},
      querySelector() { return getNode('dot'); },
      closest() { return null; },
    };
  };
  const getNode = id => {
    if (!nodes.has(id)) nodes.set(id, makeNode(id));
    return nodes.get(id);
  };
  const response = item => {
    const value = item || {status: 200, body: {}};
    const status = value.status === undefined ? 200 : value.status;
    return {status, ok: status >= 200 && status < 300, json: async () => value.body || {}, text: async () => value.text || ''};
  };
  const context = vm.createContext({
    document: {getElementById: getNode, createElement: tag => makeNode(tag), activeElement: null},
    fetch: async (url, options) => { requests.push({url, options}); return response(responses.shift()); },
    setTimeout() {}, clearTimeout() {}, setInterval() {}, clearInterval() {},
    confirm: () => true, console,
  });
  vm.runInContext(script, context);
  return {api: context.api, node: getNode, requests};
}

function listsPayload(values = {}) {
  return {selectedProfile: 'custom', appliedProfile: 'custom', busy: false,
    status: 'Lista preparada', progress: 0, nonce: 'lists-token', allowed: [], domains: 123, ...values};
}

function ready(dashboardInstance, values = {}) {
  dashboardInstance.api.state.listsReady = true;
  dashboardInstance.api.state.listsNonce = 'lists-token';
  dashboardInstance.api.renderLists(listsPayload(values));
}

test('keeps a migrated custom list explicit and separates selected from active while pending', async () => {
  const instance = dashboard([{status: 202}]);
  ready(instance);
  assert.equal(instance.node('profileSelect').value, 'custom');
  assert.match(instance.node('listsActive').textContent, /Personalizada/);
  instance.api.state.listsProfileDraft = 'balanced';
  instance.api.state.listsProfileDirty = true;
  instance.node('profileSelect').value = 'balanced';
  await instance.api.listsAction('/lists/profile?p=balanced', 'Solicitud aceptada; esperando aplicación…');
  assert.equal(instance.requests.at(-1).url, '/lists/profile?p=balanced');
  assert.equal(instance.requests.at(-1).options.headers['X-CSRF-Token'], 'lists-token');
  assert.match(instance.node('listsStatus').textContent, /esperando aplicación/);
  assert.match(instance.node('listsActive').textContent, /Personalizada/);
  assert.equal(instance.api.state.listsBusy, true);
  assert.equal(instance.node('listsCheck').disabled, true);
});

test('does not call refresh for the migrated custom profile', async () => {
  const instance = dashboard();
  ready(instance);
  instance.api.checkLists();
  await Promise.resolve();
  assert.equal(instance.requests.length, 0);
  assert.equal(instance.node('listsCheck').disabled, true);
});

test('clears the pending guard only after a poll confirms the applied profile', async () => {
  const instance = dashboard([{status: 202}, {status: 200, body: listsPayload({selectedProfile: 'balanced', appliedProfile: 'balanced', progress: 100, status: 'Lista verificada'})}]);
  ready(instance);
  instance.api.state.listsProfileDraft = 'balanced';
  instance.api.state.listsProfileDirty = true;
  instance.node('profileSelect').value = 'balanced';
  await instance.api.listsAction('/lists/profile?p=balanced', 'Solicitud aceptada; esperando aplicación…');
  assert.equal(instance.api.state.listsBusy, true);
  await instance.api.loadLists();
  assert.equal(instance.api.state.listsBusy, false);
  assert.match(instance.node('listsActive').textContent, /Equilibrado/);
  assert.equal(instance.node('profileApply').disabled, true);
});

test('renders busy progress without claiming that the new profile is active', () => {
  const instance = dashboard();
  ready(instance, {selectedProfile: 'balanced', appliedProfile: 'custom', busy: true, progress: 47, status: 'Descargando'});
  assert.equal(instance.node('listsProgress').value, 47);
  assert.match(instance.node('listsProgressText').textContent, /47%/);
  assert.match(instance.node('listsProgressText').textContent, /Personalizada/);
  assert.equal(instance.node('profileApply').disabled, true);
});

test('list refresh leaves a manually edited custom update URL untouched', () => {
  const instance = dashboard();
  instance.node('uurl').value = 'https://lists.example/custom.bin';
  ready(instance, {allowed: ['safe.example']});
  assert.equal(instance.node('uurl').value, 'https://lists.example/custom.bin');
});

test('shows API errors and retains the last list data', async () => {
  const instance = dashboard([{status: 409}]);
  ready(instance, {allowed: ['safe.example']});
  await instance.api.listsAction('/lists/check', 'Comprobación aceptada; esperando resultado…');
  assert.match(instance.node('listsError').textContent, /actualizando/);
  assert.deepEqual(instance.api.state.allowed, ['safe.example']);
});

test('uses an allowlist-specific message for a full or duplicate domain', async () => {
  const instance = dashboard([{status: 409}]);
  ready(instance, {allowed: ['safe.example']});
  await instance.api.changeAllowlist('duplicate.example', false);
  assert.match(instance.node('allowStatus').textContent, /ya está permitido|máximo de 200/);
  assert.deepEqual(instance.api.state.allowed, ['safe.example']);
});

test('does not offer allowlist override for client-ban history rows', () => {
  const instance = dashboard();
  ready(instance);
  instance.api.renderLog({entries: [{domain: 'banned.example', client: '192.168.1.20', type: 1, reason: 'client', ageSeconds: 1}], count: 1, total: 1, offset: 0, more: false});
  const action = instance.node('logBody').children[0].children[5].children[0];
  assert.equal(action.disabled, true);
  assert.equal(action.title, 'Desbloquea el cliente en Clientes');
});

test('allowlist requests are encoded and rendered as text within the 200-domain bound', async () => {
  const malicious = '<img src=x onerror=alert(1)>.example';
  const instance = dashboard([{status: 200}, {status: 200, body: listsPayload({allowed: Array.from({length: 205}, (_, i) => i === 0 ? malicious : `d${i}.example`)})}]);
  ready(instance);
  instance.node('allowDomain').value = malicious;
  await instance.api.changeAllowlist(malicious, false);
  assert.match(instance.requests[0].url, /allowlist\/add\?d=%3Cimg%20src%3Dx/);
  assert.equal(instance.api.state.allowed.length, 200);
  assert.equal(instance.node('allowBody').children[0].children[0].textContent, malicious);
  assert.equal(instance.node('allowBody').children[0].children[0].innerHTML, undefined);
});


test('manual blocklist uploads send the boot CSRF token without altering firmware upload headers', () => {
  const instance = dashboard();
  ready(instance);
  const body = {};
  const options = instance.api.uploadOptions('/upload', body);
  assert.equal(options.method, 'POST');
  assert.equal(options.body, body);
  assert.equal(options.headers['X-CSRF-Token'], 'lists-token');
  assert.equal(instance.api.uploadOptions('/update', body).headers, undefined);
});
