#!/usr/bin/env python3
"""Verify Wi-Fi setup without supplying or reading real network passwords.

The default checks do not restart the device. --exercise-portal temporarily
stops DNS blocking, tries a nonexistent SSID, then cancels and verifies recovery.
The host stays on its existing network; requires a provisioned idle device.
"""
import argparse
import json
import re
import secrets
import time
import urllib.error
import urllib.parse
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('--exercise-portal', action='store_true')
    args = parser.parse_args()
    base = f'http://{args.ip}'
    opener = urllib.request.build_opener(NoRedirect)

    def request(path, data=None, headers=None):
        req = urllib.request.Request(base + path, data=data, headers=headers or {})
        try:
            with opener.open(req, timeout=5) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    def get_json(path):
        code, body = request(path)
        assert code == 200, (path, code)
        return json.loads(body)

    def wait_json(path, predicate, seconds=65):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                value = get_json(path)
                if predicate(value):
                    return value
            except (OSError, ValueError):
                pass
            time.sleep(1)
        raise AssertionError(f'Timed out waiting for {path}')

    before = get_json('/stats.json')
    lists = get_json('/lists.json')
    assert not before['githubBusy'] and not lists['busy'], 'Device is busy'
    nonce = before['githubNonce']
    for token, origin in ((None, base), ('wrong', base),
                          (nonce, 'http://untrusted.example'), (nonce, None)):
        headers = {}
        if token is not None:
            headers['X-CSRF-Token'] = token
        if origin is not None:
            headers['Origin'] = origin
        assert request('/wifi/setup', b'', headers)[0] == 403
    assert request('/forgetwifi')[0] == 410
    assert get_json('/stats.json')['githubNonce'] == nonce
    print('PASS rejected missing/invalid/cross-origin CSRF and disabled credential deletion', flush=True)
    if not args.exercise_portal:
        return

    request('/wifi/setup', b'', {'Origin': base, 'X-CSRF-Token': nonce})
    portal_entered = False
    try:
        wait_json('/wifi/status', lambda status: status['state'] == 'idle')
        portal_entered = True
        code, page = request('/')
        english = before.get('language', 'es') == 'en'
        assert code == 200 and ('Try and save' if english else 'Probar y guardar') in page
        token = re.search(r'name=csrf value=\'([0-9a-f]{32})\'', page).group(1)

        def submit(path, fields, origin=base):
            body = urllib.parse.urlencode(fields).encode()
            return request(path, body, {'Origin': origin,
                                       'Content-Type': 'application/x-www-form-urlencoded'})

        assert submit('/wifisave', {'s': 'Test', 'p': ''})[0] == 403
        assert submit('/wifisave', {'csrf': token, 's': 'Test', 'p': ''},
                      'http://untrusted.example')[0] == 403
        assert submit('/wifi/cancel', {'csrf': 'wrong'})[0] == 403
        for ssid, password in (('', ''), ('x' * 33, ''), ('Test', 'short'), ('Bad\x00SSID', '')):
            assert submit('/wifisave', {'csrf': token, 's': ssid, 'p': password})[0] == 400
        print('PASS portal form, CSRF/Origin and malformed credential rejection', flush=True)
        try:
            code, _ = submit('/wifisave', {'csrf': token,
                                          's': 'AdBlock-Missing-' + secrets.token_hex(6),
                                          'p': ''})
            assert code == 303
        except OSError:
            # Association drops the LAN socket while the setup AP remains up.
            pass
        wait_json('/wifi/status', lambda status: status['state'] == 'failed')
        code, page = request('/')
        assert code == 200 and ('Could not connect' if english else 'No se pudo conectar') in page
        print('PASS nonexistent network rejected; previous LAN connection recovered', flush=True)
    finally:
        if portal_entered:
            _, page = request('/')
            token = re.search(r'name=csrf value=\'([0-9a-f]{32})\'', page).group(1)
            body = urllib.parse.urlencode({'csrf': token}).encode()
            assert request('/wifi/cancel', body, {'Origin': base,
                           'Content-Type': 'application/x-www-form-urlencoded'})[0] == 303
            after = wait_json('/stats.json', lambda value: value['githubNonce'] != nonce)
            for field in ('fwVersion', 'fwProfile', 'domains', 'custom', 'upurl', 'upiv', 'wifiSsid'):
                assert after[field] == before[field], f'{field} changed'
            assert after.get('language', 'es') == before.get('language', 'es'), 'language changed'
            recovered_lists = get_json('/lists.json')
            for field in ('selectedProfile', 'appliedProfile', 'allowed', 'domains'):
                assert recovered_lists[field] == lists[field], f'{field} changed'
            print('PASS cancel/reboot retained Wi-Fi, firmware, lists, settings and exceptions', flush=True)


if __name__ == '__main__':
    main()
