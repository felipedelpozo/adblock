const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function dashboard(responses = []) {
  const source = fs.readFileSync(path.join(__dirname, '../src/page.h'), 'utf8');
  let script = source.split('<script>')[1].split('</script>')[0];
  script = script.replace(/setTimeout\(loadLists,0\);loadStats\(\);requestLog\(\);setInterval[\s\S]*$/, 'globalThis.api={state,applyLanguage,saveLanguage,renderMetrics,renderStatus,renderClients,renderCustom,renderLists,renderLog,renderWifi,setOffline,updateLanguageControl,loadStats};');
  const nodes = new Map();
  const requests = [];
  const makeNode = id => {
    const children = [];
    return {
      id, children, textContent: '', value: '', disabled: false, hidden: false,
      dataset: {}, colSpan: 1, placeholder: '', title: '',
      classList: {toggle() {}, add() {}, remove() {}},
      append(...items) { children.push(...items); },
      appendChild(item) { children.push(item); return item; },
      removeChild(item) { const index = children.indexOf(item); if (index >= 0) children.splice(index, 1); return item; },
      get firstChild() { return children[0]; },
      addEventListener() {},
      querySelector() { return makeNode('dot'); },
      closest() { return null; },
    };
  };
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, makeNode(id));
    return nodes.get(id);
  };
  const response = item => {
    if (typeof item === 'function') return item();
    const value = item || {status: 200, body: {}};
    const status = value.status === undefined ? 200 : value.status;
    return {status, ok: status >= 200 && status < 300, json: async () => value.body || {}, text: async () => value.text || ''};
  };
  const context = vm.createContext({
    document: {
      getElementById: node,
      createElement: tag => makeNode(tag),
      querySelectorAll: () => [],
      documentElement: {lang: 'es'},
      title: '',
      activeElement: null,
    },
    fetch: async (url, options) => {
      requests.push({url, options});
      return response(responses.shift());
    },
    setTimeout() {}, clearTimeout() {}, setInterval() {}, clearInterval() {},
    confirm: () => true, console,
    Intl,
  });
  vm.runInContext(script, context);
  return {api: context.api, node, requests};
}

function stats(values = {}) {
  return {
    language: 'es', ip: '192.168.1.2', blocking: true, blocked: 1234, allowed: 56,
    domains: 789, clients: [], custom: [], rssi: -42, temp: 31, heap: 2048,
    uptime: '1 h', githubNonce: 'csrf-token', githubBusy: false,
    githubCanInstall: false, githubStatus: 'Nunca comprobado', wifiSsid: 'Casa',
    wifiSetupAp: 'C3-AdBlock-1234', ...values,
  };
}

function lists(values = {}) {
  return {
    selectedProfile: 'custom', appliedProfile: 'custom', busy: false,
    status: 'Lista preparada', progress: 0, nonce: 'lists-token',
    allowed: [], domains: 10, ...values,
  };
}

test('renders dashboard content and relative times in English', () => {
  const instance = dashboard();
  instance.api.applyLanguage('en');
  instance.api.renderStatus({blocking: false, resumeIn: 90});
  instance.api.renderMetrics(stats({language: 'en', clients: [{blocked: 2, allowed: 1, ip: '1.2.3.4', mac: 'AA'}]}));
  instance.api.renderLists(lists({selectedProfile: 'balanced', appliedProfile: 'custom', busy: true, progress: 47}));
  instance.api.renderLog({entries: [{domain: 'ads.example', client: '1.2.3.4', type: 1, reason: 'blocklist', ageSeconds: 65}], count: 1, total: 1, offset: 0, more: false});
  assert.equal(instance.api.state.language, 'en');
  assert.equal(instance.node('blockstate').textContent, 'Blocking paused');
  assert.match(instance.node('blocknote').textContent, /Resumes in 90 s/);
  assert.match(instance.node('listsActive').textContent, /Custom/);
  assert.match(instance.node('listsProgressText').textContent, /Progress: 47%/);
  assert.match(instance.node('logMeta').textContent, /1 match/);
  assert.match(instance.node('logBody').children[0].children[4].textContent, /1 min ago/);
});

test('language save uses the existing CSRF nonce and preserves drafts on success', async () => {
  const instance = dashboard([{status: 200, body: {language: 'en'}}]);
  instance.api.state.githubNonce = 'csrf-token';
  instance.api.renderMetrics(stats());
  instance.node('uurl').value = 'https://example.test/list.bin';
  instance.node('uiv').value = '12';
  instance.api.state.updateDirty = true;
  await instance.api.saveLanguage('en');
  assert.equal(instance.requests[0].url, '/language?lang=en');
  assert.equal(instance.requests[0].options.method, 'POST');
  assert.equal(instance.requests[0].options.headers['X-CSRF-Token'], 'csrf-token');
  assert.equal(instance.api.state.language, 'en');
  assert.equal(instance.node('uurl').value, 'https://example.test/list.bin');
  assert.equal(instance.node('uiv').value, '12');
});

test('failed language save restores the previous locale and drafts', async () => {
  const instance = dashboard([{status: 500}]);
  instance.api.state.githubNonce = 'csrf-token';
  instance.api.renderMetrics(stats());
  instance.node('uurl').value = 'https://draft.test/list.bin';
  instance.api.state.updateDirty = true;
  await instance.api.saveLanguage('en');
  assert.equal(instance.api.state.language, 'es');
  assert.equal(instance.node('uurl').value, 'https://draft.test/list.bin');
  assert.match(instance.node('languageStatus').textContent, /No se pudo guardar/);
});

test('stale Spanish stats cannot overwrite a successful English choice', async () => {
  let release;
  const pending = new Promise(resolve => { release = resolve; });
  const instance = dashboard([() => pending, {status: 200, body: {language: 'en'}}]);
  instance.api.state.githubNonce = 'csrf-token';
  const statsRequest = instance.api.loadStats();
  await instance.api.saveLanguage('en');
  release({status: 200, body: stats()});
  await statsRequest;
  assert.equal(instance.api.state.language, 'en');
});

test('language selector is disabled while offline, saving, or Wi-Fi setup is active', async () => {
  const pending = new Promise(() => {});
  const instance = dashboard([() => pending]);
  instance.api.state.githubNonce = 'csrf-token';
  instance.api.renderMetrics(stats());
  instance.api.setOffline(true);
  assert.equal(instance.node('languageSelect').disabled, true);
  instance.api.setOffline(false);
  instance.api.state.wifiReconfiguring = true;
  instance.api.updateLanguageControl();
  assert.equal(instance.node('languageSelect').disabled, true);
  instance.api.state.wifiReconfiguring = false;
  const save = instance.api.saveLanguage('en');
  assert.equal(instance.node('languageSelect').disabled, true);
  await Promise.resolve();
  assert.equal(instance.api.state.languagePending, true);
  void save;
});

test('literal dollar replacement in a translated label stays text', () => {
  const instance = dashboard();
  instance.api.applyLanguage('en');
  instance.api.renderWifi({wifiSsid: '$&', wifiSetupAp: 'C3-AdBlock-1234'});
  assert.equal(instance.node('wifiNetwork').textContent, 'Current network: $&');
});

test('language changes wait for the dashboard boot nonce', async () => {
  const instance = dashboard();
  instance.api.updateLanguageControl();
  assert.equal(instance.node('languageSelect').disabled, true);
  await instance.api.saveLanguage('en');
  assert.equal(instance.requests.length, 0);
  assert.equal(instance.api.state.language, 'es');
});

test('unexpected successful language reply restores the previous preference', async () => {
  const instance = dashboard([{status: 200, body: {language: 'fr'}}]);
  instance.api.state.githubNonce = 'csrf-token';
  await instance.api.saveLanguage('en');
  assert.equal(instance.api.state.language, 'es');
  assert.match(instance.node('languageStatus').textContent, /No se pudo guardar/);
});
