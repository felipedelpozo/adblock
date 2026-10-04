const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const childProcess = require('node:child_process');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'web/pet.html'), 'utf8');
const script = html.split('<script>')[1].split('</script>')[0];
function api() { const context = vm.createContext({console, setTimeout, clearTimeout, Uint8Array, Uint8ClampedArray, DataView, ArrayBuffer, Map}); vm.runInContext(script, context); return context.PetPage; }

test('validates names and settings', () => {
  const pet = api();
  assert.equal(pet.validName('Mi Pet_2'), true);
  assert.equal(pet.validName('á'), false);
  assert.equal(pet.validName('x'.repeat(17)), false);
  assert.equal(pet.validSettings({cols: 8, rows: 11, rowsByState: [0, 3, 4, 6], counts: [6, 4, 5, 6]}), true);
  assert.equal(pet.validSettings({cols: 17, rows: 11, rowsByState: [0, 3, 4, 6], counts: [6, 4, 5, 6]}), false);
});

test('encodes exact BPT1 length, header, palette, and CRC', () => {
  const pet = api();
  const frames = Array.from({length: 24}, (_, frame) => { const rgba = new Uint8ClampedArray(48 * 48 * 4); for (let pixel = 0; pixel < 48 * 48; pixel += 1) { const at = pixel * 4; rgba[at] = (frame * 11) & 255; rgba[at + 1] = pixel & 255; rgba[at + 2] = 220; rgba[at + 3] = pixel === 0 ? 0 : 255; } return rgba; });
  const bpt = pet.encodeBpt1(frames);
  assert.equal(bpt.length, 27696);
  assert.deepEqual([...bpt.subarray(0, 9)], [66, 80, 84, 49, 48, 48, 4, 4, 6]);
  assert.deepEqual([...bpt.subarray(16, 18)], [0, 0]);
  assert.notDeepEqual([...bpt.subarray(18, 20)], [0, 0]);
  assert.equal(new DataView(bpt.buffer).getUint32(12, true), pet.crc32(bpt.subarray(16)));
  assert.equal(pet.decodeBpt1(bpt).frames.length, 24);
  const corrupt = new Uint8Array(bpt); corrupt[48] ^= 1; assert.throws(() => pet.decodeBpt1(corrupt), /CRC/);
});

test('extracts four states with six frames and validates fixed geometry', () => {
  const pet = api(); const width = 8 * 11; const height = 11 * 11; const pixels = new Uint8ClampedArray(width * height * 4);
  for (let row = 0; row < 11; row += 1) for (let col = 0; col < 8; col += 1) { const at = (row * 11 * width + col * 11) * 4; pixels[at] = row * 20; pixels[at + 3] = 255; }
  const frames = pet.extractFrames(pixels, width, height, {cols: 8, rows: 11, rowsByState: [0, 3, 4, 6], counts: [6, 4, 5, 6]});
  assert.equal(frames.length, 24); assert.equal(frames[0].length, 48 * 48 * 4); assert.equal(frames[6][0], 60); assert.equal(frames[12][0], 80); assert.equal(frames[18][0], 120);
});

test('build output is deterministic and uses gzip PROGMEM symbols', () => {
  const command = process.platform === 'win32' ? 'python' : 'python3'; const output = path.join(root, 'src/pet_page.test.h');
  childProcess.execFileSync(command, ['tools/build_pet_page.py', '--source', 'web/pet.html', '--output', output], {cwd: root}); const before = fs.readFileSync(output);
  childProcess.execFileSync(command, ['tools/build_pet_page.py', '--source', 'web/pet.html', '--output', output], {cwd: root}); const after = fs.readFileSync(output);
  assert.deepEqual(after, before); assert.match(after.toString(), /PET_PAGE_GZIP/); fs.rmSync(output);
});

test('declares local APIs and CSRF transport without runtime CDN dependencies', () => {
  assert.match(html, /\/pet\.json/); assert.match(html, /\/pet\/appearance\.json/); assert.match(html, /\/pet\/appearance\?name=/); assert.match(html, /\/pet\/sprite/); assert.match(html, /X-CSRF-Token/); assert.match(html, /codex-pets\.net/); assert.doesNotMatch(html, /unpkg\.com|jsdelivr\.net/);
});


test('browser name pattern and conversion bounds reject invalid input', () => {
  const pet = api();
  const pattern = html.match(/pattern="([^"]+)"/)[1];
  const expression = new RegExp('^(?:' + pattern + ')$', 'v');
  assert.ok(expression.test('A-b_c 12'));
  assert.ok(!expression.test('<script>'));
  assert.equal(pet.validName('   '), false);
  const settings = {cols: 8, rows: 11, rowsByState: [0, 3, 4, 6], counts: [6, 4, 5, 6]};
  assert.equal(pet.validSettings({...settings, cols: 1.5}), false);
  assert.equal(pet.validSettings({...settings, counts: [9, 4, 5, 6]}), false);
  assert.equal(pet.validSettings({...settings, rowsByState: [0, 3]}), false);
  assert.throws(() => pet.encodeBpt1(Array.from({length: 24}, () => new Uint8Array(1))), /frames/);
  const frames = Array.from({length: 24}, () => new Uint8Array(48*48*4));
  const bpt = pet.encodeBpt1(frames);
  for (const offset of [7,9,10,11]) {
    const bad = new Uint8Array(bpt); bad[offset] ^= 1;
    assert.throws(() => pet.decodeBpt1(bad), /BPT1/);
  }
});


test('playback skips shrink poses, preserves translated poses and empty states', () => {
  const frames = Array.from({length:24}, (_, index) => {
    const rgba = new Uint8ClampedArray(48*48*4), f = index%6;
    if (index>=18) return rgba;
    const width=f===4?10:20, height=f===0||f===1?15:30;
    for(let y=f;y<f+height;y++)for(let x=5;x<5+width;x++)rgba[(y*48+x)*4+3]=255;
    return rgba;
  });
  const before = frames.map(frame=>new Uint8ClampedArray(frame));
  const map = api().playbackMap(frames);
  assert.deepEqual([...map], [5,5,2,3,3,5,11,11,8,9,9,11,17,17,14,15,15,17,18,19,20,21,22,23]);
  frames.forEach((frame,i)=>assert.deepEqual(frame,before[i]));
});
