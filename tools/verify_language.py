#!/usr/bin/env python3
"""Verify appliance-wide language changes and restore the initial preference.

Optional --reboot-port performs a controlled USB reset to check persistence.
Optional --exercise-portal temporarily stops blocking to check English setup.
Neither mode changes network credentials, lists, or the host's network.
"""
import argparse
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request

from verify_device import resolve


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('--reboot-port')
    parser.add_argument('--exercise-portal', action='store_true')
    args = parser.parse_args()
    base = f'http://{args.ip}'
    opener = urllib.request.build_opener(NoRedirect)

    def request(path, data=None, headers=None):
        try:
            with opener.open(urllib.request.Request(base + path, data=data,
                                                    headers=headers or {}), timeout=5) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    def get(path):
        code, body = request(path)
        assert code == 200, (path, code)
        return json.loads(body)

    def wait(path, predicate, seconds=60):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            try:
                value = get(path)
                if predicate(value):
                    return value
            except (ValueError, OSError):
                pass
            time.sleep(1)
        raise AssertionError(f'Timed out waiting for {path}')

    def change(language):
        nonce = get('/stats.json')['githubNonce']
        code, body = request('/language?lang=' + urllib.parse.quote(language, safe=''), b'',
                             {'Origin': base, 'X-CSRF-Token': nonce})
        assert code == 200 and json.loads(body)['language'] == language, (code, body)

    before = get('/stats.json')
    lists_before = get('/lists.json')
    assert not before['githubBusy'] and not lists_before['busy'], 'Device is busy'
    initial_language = before['language']
    portal_token = None
    try:
        nonce = before['githubNonce']
        for token, origin in ((None, base), ('wrong', base),
                              (nonce, None), (nonce, 'http://untrusted.example')):
            headers = {}
            if token is not None:
                headers['X-CSRF-Token'] = token
            if origin is not None:
                headers['Origin'] = origin
            assert request('/language?lang=en', b'', headers)[0] == 403
        for code in ('', 'fr', 'en-US', 'EN', 'es<script>'):
            assert request('/language?lang=' + urllib.parse.quote(code, safe=''), b'',
                           {'Origin': base, 'X-CSRF-Token': nonce})[0] == 400
        assert get('/stats.json')['language'] == initial_language
        assert request('/language?lang=en')[0] != 200
        assert get('/stats.json')['language'] == initial_language
        print('PASS language validation and missing/invalid/cross-origin CSRF', flush=True)
        for language in ('en', 'es', 'en'):
            change(language)
            after = get('/stats.json')
            assert after['language'] == language and after['githubNonce'] == nonce
            assert get('/lists.json')['language'] == language
            if before['githubStatus'] == 'Nunca comprobado':
                assert after['githubStatus'] == ('Never checked' if language == 'en' else 'Nunca comprobado')
            if before['blocking']:
                assert resolve(args.ip, 'googlesyndication.com') == ['0.0.0.0']
        print('PASS Spanish/English switch without restart or loss of DNS blocking', flush=True)

        if args.reboot_port:
            import serial
            connection = serial.Serial(port=None, baudrate=115200, timeout=.25)
            connection.dtr = False
            connection.rts = False
            connection.port = args.reboot_port
            connection.open()
            try:
                after = wait('/stats.json', lambda value: value['githubNonce'] != nonce)
                assert after['language'] == 'en'
                nonce = after['githubNonce']
            finally:
                connection.close()
            print('PASS English preference persisted through controlled USB restart', flush=True)

        if args.exercise_portal:
            assert request('/wifi/setup', b'', {'Origin': base, 'X-CSRF-Token': nonce})[0] == 202
            wait('/wifi/status', lambda value: value['state'] == 'idle')
            code, page = request('/')
            portal_token = re.search(r'name=csrf value=\'([0-9a-f]{32})\'', page).group(1)
            assert code == 200 and 'Try and save' in page and 'Cancel' in page
            assert 'lang="en"' in page or "lang='en'" in page
            body = urllib.parse.urlencode({'csrf': 'wrong', 'lang': 'es'}).encode()
            assert request('/wifi/language', body, {'Origin': base,
                           'Content-Type': 'application/x-www-form-urlencoded'})[0] == 403
            for language, expected, status_code in (('fr', None, 400), ('es', 'Probar y guardar', 303),
                                                     ('en', 'Try and save', 303)):
                body = urllib.parse.urlencode({'csrf': portal_token, 'lang': language}).encode()
                assert request('/wifi/language', body, {'Origin': base,
                               'Content-Type': 'application/x-www-form-urlencoded'})[0] == status_code
                if expected:
                    assert expected in request('/')[1]
            body = urllib.parse.urlencode({'csrf': portal_token}).encode()
            assert request('/wifi/cancel', body, {'Origin': base,
                           'Content-Type': 'application/x-www-form-urlencoded'})[0] == 303
            portal_token = None
            after = wait('/stats.json', lambda value: value['githubNonce'] != nonce)
            assert after['language'] == 'en'
            print('PASS Wi-Fi portal inherited language, switched Spanish/English and canceled safely', flush=True)
    finally:
        if portal_token:
            body = urllib.parse.urlencode({'csrf': portal_token}).encode()
            request('/wifi/cancel', body, {'Origin': base,
                    'Content-Type': 'application/x-www-form-urlencoded'})
            wait('/stats.json', lambda value: 'language' in value)
        change(initial_language)
        after = get('/stats.json')
        for field in ('fwVersion', 'fwProfile', 'domains', 'custom', 'upurl', 'upiv', 'wifiSsid', 'language'):
            assert after[field] == before[field], f'{field} changed'
        lists_after = get('/lists.json')
        for field in ('selectedProfile', 'appliedProfile', 'allowed', 'domains'):
            assert lists_after[field] == lists_before[field], f'{field} changed'
        print('PASS original language restored; Wi-Fi, lists, settings and exceptions retained', flush=True)


if __name__ == '__main__':
    main()
