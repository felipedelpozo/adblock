const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function dashboard({confirm = () => true, responses = []} = {}) {
  const source = fs.readFileSync(path.join(__dirname, '../src/page.h'), 'utf8');
  let script = source.split('<script>')[1].split('</script>')[0];
  script = script.replace(/loadStats\(\);requestLog\(\);setInterval[\s\S]*$/,
    'globalThis.api={state,renderWifi,renderFirmware,changeWifi,loadStats,loadLists,requestLog,setOffline};');
  const nodes = new Map();
  const requests = [];
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, {
      textContent: '', disabled: false, hidden: id === 'wifiInstructions', dataset: {},
      classList: {toggle() {}, add() {}, remove() {}},
      addEventListener() {}, querySelector() {return node('dot');}
    });
    return nodes.get(id);
  };
  const context = vm.createContext({document: {getElementById: node}, confirm,
    fetch: async (url, options) => {
      requests.push({url, options});
      const value = responses.shift() || {};
      if (typeof value === 'function') return value();
      const status = value.status || 202;
      return {ok: status < 400, status, json: async () => value.body || {ap: 'C3-AdBlock-5678'}};
    }, setTimeout() {}, clearTimeout() {}, setInterval() {}});
  vm.runInContext(script, context);
  context.api.renderFirmware({githubNonce: 'wifi-token', githubBusy: false});
  context.api.renderWifi({wifiSsid: 'Home network', wifiSetupAp: 'C3-AdBlock-1234'});
  return {api: context.api, node, requests};
}

test('network names are rendered as text, not HTML', () => {
  const {api, node} = dashboard();
  api.renderWifi({wifiSsid: '<img src=x onerror=alert(1)>', wifiSetupAp: 'C3-AdBlock-1234'});
  assert.equal(node('wifiNetwork').textContent, 'Red actual: <img src=x onerror=alert(1)>');
  assert.equal(node('wifiNetwork').innerHTML, undefined);
});

test('offline, update, missing nonce and old firmware states cannot change Wi-Fi', async () => {
  for (const scenario of ['offline', 'update', 'nonce', 'legacy']) {
    const {api, node, requests} = dashboard();
    if (scenario === 'offline') api.setOffline(true);
    if (scenario === 'update') api.renderFirmware({githubNonce: 'wifi-token', githubBusy: true});
    if (scenario === 'nonce') api.renderFirmware({githubNonce: ''});
    if (scenario === 'legacy') api.renderWifi({});
    assert.equal(node('wifiChange').disabled, true, scenario);
    await api.changeWifi();
    assert.equal(requests.length, 0);
  }
});

test('canceling confirmation leaves the running network and polling unchanged', async () => {
  const {api, node, requests} = dashboard({confirm: () => false});
  await api.changeWifi();
  assert.equal(requests.length, 0);
  assert.equal(node('wifiInstructions').hidden, true);
  assert.equal(api.state.wifiReconfiguring, false);
});

test('confirmed setup uses CSRF POST, shows recovery instructions and suspends polling', async () => {
  const {api, node, requests} = dashboard({confirm: message => {
    assert.match(message, /bloqueo se interrumpirá/);
    assert.match(message, /conservan/);
    return true;
  }});
  await api.changeWifi();
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, '/wifi/setup');
  assert.equal(requests[0].options.method, 'POST');
  assert.equal(requests[0].options.headers['X-CSRF-Token'], 'wifi-token');
  assert.equal(api.state.wifiReconfiguring, true);
  assert.equal(node('wifiInstructions').hidden, false);
  assert.equal(node('wifiAp').textContent, 'C3-AdBlock-5678');
  assert.equal(node('wifiChange').disabled, true);
  assert.equal(node('pausebtn').disabled, true);
  await api.loadStats();
  await api.loadLists();
  api.requestLog();
  assert.equal(requests.length, 1);
});

test('a conflicting update can be retried and does not claim setup has started', async () => {
  const {api, node, requests} = dashboard({responses: [{status: 409}, {status: 202}]});
  await api.changeWifi();
  assert.equal(api.state.wifiReconfiguring, false);
  assert.equal(node('wifiChange').disabled, false);
  assert.match(node('wifiStatus').textContent, /Espera/);
  await api.changeWifi();
  assert.equal(requests.length, 2);
  assert.equal(api.state.wifiReconfiguring, true);
});

test('duplicate clicks do not schedule two device restarts', async () => {
  let release;
  const pending = new Promise(resolve => {release = resolve;});
  const {api, requests} = dashboard({responses: [() => pending]});
  const first = api.changeWifi();
  await api.changeWifi();
  assert.equal(requests.length, 1);
  release({ok: true, status: 202, json: async () => ({ap: 'C3-AdBlock-1234'})});
  await first;
});

test('an in-flight stats response cannot restore Active after setup is accepted', async () => {
  let release;
  const pending = new Promise(resolve => {release = resolve;});
  const {api, node} = dashboard({responses: [() => pending, {status: 202}]});
  const stats = api.loadStats();
  await api.changeWifi();
  assert.equal(node('blockstate').textContent, 'Configurando Wi-Fi');
  release({ok: true, status: 200, json: async () => ({blocking: true})});
  await stats;
  assert.equal(node('blockstate').textContent, 'Configurando Wi-Fi');
  assert.equal(node('pausebtn').disabled, true);
});
