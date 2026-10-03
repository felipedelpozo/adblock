const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

function dashboard(confirm = () => true) {
  const source = fs.readFileSync(path.join(__dirname, '../src/page.h'), 'utf8');
  let script = source.split('<script>')[1].split('</script>')[0];
  script = script.replace(/loadStats\(\);requestLog\(\);setInterval[\s\S]*$/, 'globalThis.api={state,renderFirmware,githubAction,setOffline};');
  const nodes = new Map();
  const requests = [];
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, {
      textContent: '', disabled: false, dataset: {},
      classList: {toggle() {}, add() {}, remove() {}},
      addEventListener() {}, querySelector() {return node('dot');}
    });
    return nodes.get(id);
  };
  const context = vm.createContext({document: {getElementById: node}, confirm,
    fetch: async (url, options) => {requests.push({url, options}); return {ok: true};},
    setTimeout() {}, clearTimeout() {}, setInterval() {}});
  vm.runInContext(script, context);
  return {api: context.api, node, requests};
}

function ready(api, values = {}) {
  api.renderFirmware({fwVersion: '0.1.0', fwProfile: 'jc3636w518c', githubNonce: 'test-token',
    githubBusy: false, githubCanInstall: true, githubVersion: '0.1.1',
    githubStatus: 'Release lista para instalar: 0.1.1', ...values});
}

test('no release, busy and offline states disable installation', () => {
  const {api, node} = dashboard();
  ready(api, {githubCanInstall: false, githubVersion: '', githubStatus: 'No hay releases publicadas'});
  assert.equal(node('githubInstall').disabled, true);
  assert.equal(node('githubCheck').disabled, false);
  assert.equal(node('githubStatus').textContent, 'No hay releases publicadas');
  ready(api, {githubBusy: true, githubProgress: 47});
  assert.equal(node('githubInstall').disabled, true);
  assert.equal(node('githubCheck').disabled, true);
  assert.match(node('githubStatus').textContent, /47%/);
  ready(api);
  assert.equal(node('githubInstall').disabled, false);
  api.setOffline(true);
  assert.equal(node('githubInstall').disabled, true);
  assert.equal(node('githubCheck').disabled, true);
});

test('check uses a CSRF POST and never installs or asks for confirmation', async () => {
  const {api, requests} = dashboard(() => {throw new Error('check must not ask to install');});
  ready(api);
  await api.githubAction(false);
  assert.equal(requests.length, 1);
  assert.equal(requests[0].url, '/github/check');
  assert.equal(requests[0].options.method, 'POST');
  assert.equal(requests[0].options.headers['X-CSRF-Token'], 'test-token');
});

test('install requires a checked candidate and explicit confirmation', async () => {
  const rejected = dashboard(() => false);
  ready(rejected.api);
  await rejected.api.githubAction(true);
  assert.equal(rejected.requests.length, 0);
  const accepted = dashboard(message => {assert.match(message, /0\.1\.1/); return true;});
  ready(accepted.api, {githubCanInstall: false});
  await accepted.api.githubAction(true);
  assert.equal(accepted.requests.length, 0);
  ready(accepted.api);
  await accepted.api.githubAction(true);
  assert.equal(accepted.requests.length, 1);
  assert.equal(accepted.requests[0].url, '/github/install?v=0.1.1');
  assert.equal(accepted.requests[0].options.method, 'POST');
});
