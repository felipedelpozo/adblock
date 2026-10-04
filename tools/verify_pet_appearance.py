#!/usr/bin/env python3
"""Validate local pet settings/upload safety on a provisioned device.

Pass a converted BPT1 asset. Restores the original name/skin by default; the
validated uploaded asset remains available locally. Does not change progression,
Wi-Fi, blocklists or the pause state. Use --keep-custom to retain the demo skin.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import secrets
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('asset', type=Path)
    parser.add_argument('--keep-custom', action='store_true')
    args = parser.parse_args()
    base = 'http://' + args.ip
    data = args.asset.read_bytes()
    assert len(data) == 27696 and data[:4] == b'BPT1'

    def request(path, body=None, headers=None):
        req = urllib.request.Request(base + path, data=body, headers=headers or {})
        try:
            response = urllib.request.urlopen(req, timeout=20)
        except urllib.error.HTTPError as error:
            return error.code, error.read()
        with response:
            content = response.read()
            if response.headers.get('Content-Encoding') == 'gzip':
                content = gzip.decompress(content)
            return response.status, content

    def get(path):
        code, body = request(path)
        assert code == 200, (path, code)
        return json.loads(body)

    stats = get('/stats.json')
    nonce = stats['githubNonce']
    headers = {'Origin': base, 'X-CSRF-Token': nonce}
    before = get('/pet.json')
    original = get('/pet/appearance.json')
    assert original['storageReady']
    page_code, page = request('/pet')
    assert page_code == 200 and b'appearanceForm' in page
    assert b'petLink' in request('/')[1]

    def selection(name, skin, auth=headers):
        path = '/pet/appearance?' + urllib.parse.urlencode({'name': name, 'skin': skin})
        return request(path, b'', auth)

    def multipart(content, second=False):
        boundary = 'pet-check-' + secrets.token_hex(8)
        head = (f'--{boundary}\r\nContent-Disposition: form-data; name="sprite"; filename="pet.bpt"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode()
        body = head + content + b'\r\n'
        if second:
            body += head + content + b'\r\n'
        body += f'--{boundary}--\r\n'.encode()
        return body, {**headers, 'Content-Type': f'multipart/form-data; boundary={boundary}'}

    def upload(content, second=False, authorized=True):
        body, auth = multipart(content, second)
        if not authorized:
            auth.pop('X-CSRF-Token')
        return request('/pet/sprite', body, auth)

    checks = []
    try:
        assert selection('Adagotchi', 'amber', {})[0] == 403
        assert selection('Adagotchi', 'amber', {'Origin': 'https://invalid.example', 'X-CSRF-Token': nonce})[0] == 403
        for name, skin in (('', 'classic'), ('A' * 17, 'classic'), ('<script>', 'classic'), ('Adagotchi', 'invalid')):
            assert selection(name, skin)[0] == 400
        assert get('/pet/appearance.json') == original
        checks.append('CSRF and input rejection preserve settings')
        assert selection('Blocky Test', 'violet')[0] == 200
        saved = get('/pet/appearance.json')
        assert saved['name'] == 'Blocky Test' and saved['skin'] == 'violet'
        checks.append('name and built-in skin saved')
        assert upload(data, authorized=False)[0] == 403
        assert upload(data)[0] == 200
        custom = get('/pet/appearance.json')
        assert custom['skin'] == 'custom' and custom['assetBytes'] == len(data)
        assert request('/pet/sprite')[1] == data
        checks.append('valid upload and exact binary download')
        bad = bytearray(data)
        bad[-1] ^= 1
        for content, second in ((bad, False), (data[:-1], False), (data + b'\0', False), (data, True)):
            assert upload(content, second)[0] == 400
            assert get('/pet/appearance.json') == custom
            assert request('/pet/sprite')[1] == data
        checks.append('CRC, truncated, oversized and multipart uploads preserve committed asset')
        final_name = original['name']
        final_skin = 'custom' if args.keep_custom else original['skin']
        assert selection(final_name, final_skin)[0] == 200
        after = get('/pet.json')
        for field in ('xp', 'totalFood', 'rewardEvents', 'uniqueRewardedDomains', 'clientCount', 'activeMs'):
            assert after[field] >= before[field], field
        assert after['bornAt'] == before['bornAt']
        assert after['name'] == final_name and after['skin'] == final_skin
        final_stats = get('/stats.json')
        for field in ('language', 'domains', 'wifiSsid', 'blocking'):
            assert final_stats[field] == stats[field], field
        checks.append('progress, Wi-Fi, language, blocklist and pause state preserved')
        print(json.dumps({'result': 'PASS', 'checks': checks, 'appearance': get('/pet/appearance.json'),
                          'assetSha256': hashlib.sha256(data).hexdigest(),
                          'progressBefore': {'xp': before['xp'], 'food': before['totalFood']},
                          'progressAfter': {'xp': after['xp'], 'food': after['totalFood']},
                          'diagnostics': after.get('diagnostics', {})}, indent=2))
    finally:
        # No cleanup of user data or uploaded artwork. Restore only the settings
        # deliberately exercised above when keeping the demo was not requested.
        if not args.keep_custom:
            selection(original['name'], original['skin'])


if __name__ == '__main__':
    main()
