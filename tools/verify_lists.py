#!/usr/bin/env python3
"""Verify live domain exceptions, list rejection and optionally a profile update.

Temporary rules and the original pause state are restored. --profile deliberately
keeps the selected profile installed. Run sequentially with other device tests.
"""
import argparse
import json
import statistics
import time
import urllib.error
import urllib.parse
import urllib.request

from verify_device import request, resolve, stats


def lists(ip):
    return json.loads(request(ip, '/lists.json'))


def post(ip, path, token, origin, body=b'', content_type=None):
    headers = {'X-CSRF-Token': token, 'Origin': origin}
    if content_type:
        headers['Content-Type'] = content_type
    req = urllib.request.Request(f'http://{ip}{path}', body, headers, method='POST')
    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('--profile', choices=('light', 'balanced', 'strict'))
    args = parser.parse_args()
    ip = args.ip
    initial = stats(ip)
    current = lists(ip)
    assert not current['busy'], 'Wait for the current list operation'
    nonce, origin = current['nonce'], f'http://{ip}'
    assert current['domains'] == initial['domains'] > 0
    domain = 'example.org'
    encoded = urllib.parse.quote(domain)
    custom_added = domain not in initial['custom']
    allowed_added = domain not in current['allowed']
    assert allowed_added, 'Test domain is already allowed; choose another test domain'
    began = time.monotonic()
    samples = []
    try:
        for token, source in (('', origin), (nonce, 'http://invalid.example')):
            assert post(ip, '/allowlist/add?d=example.org', token, source)[0] == 403
        for invalid in ('https://example.org', '||example.org^', 'example.org/path', '-bad.example'):
            path = '/allowlist/add?d=' + urllib.parse.quote(invalid, safe='')
            assert post(ip, path, nonce, origin)[0] == 400, invalid
        assert post(ip, '/lists/profile?p=unknown', nonce, origin)[0] == 400
        request(ip, '/resume')
        if custom_added:
            request(ip, '/addblock?d=' + encoded)
        assert resolve(ip, domain) == ['0.0.0.0'], 'Custom block failed'
        code, message = post(ip, '/allowlist/add?d=' + encoded, nonce, origin)
        assert code == 200, (code, message)
        assert domain in lists(ip)['allowed']
        answer = resolve(ip, domain)
        assert answer and '0.0.0.0' not in answer, 'Exception did not override custom block'
        code, message = post(ip, '/allowlist/remove?d=' + encoded, nonce, origin)
        assert code == 200, (code, message)
        assert resolve(ip, domain) == ['0.0.0.0'], 'Removing exception did not restore blocking'
        # A complete, aligned but unsorted blob must not replace the current list.
        before = lists(ip)
        boundary = 'AdBlockInvalidListTest'
        blob = (2).to_bytes(5, 'little') + (1).to_bytes(5, 'little')
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="f"; '
                'filename="invalid.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
        body += blob + f'\r\n--{boundary}--\r\n'.encode()
        code, _ = post(ip, '/upload', nonce, origin, body, f'multipart/form-data; boundary={boundary}')
        assert code >= 400, 'Unsorted hashes accepted'
        # A valid first file followed by an invalid second file must never
        # activate the first part before the whole multipart request ends.
        valid_part = (f'--{boundary}\r\nContent-Disposition: form-data; name="f"; '
                      'filename="first.bin"\r\nContent-Type: application/octet-stream\r\n\r\n').encode()
        valid_part += (1).to_bytes(5, 'little') + b'\r\n'
        invalid_part = (f'--{boundary}\r\nContent-Disposition: form-data; name="f"; '
                        'filename="second.bin"\r\nContent-Type: application/octet-stream\r\n\r\ninvalid\r\n'
                        f'--{boundary}--\r\n').encode()
        code, _ = post(ip, '/upload', nonce, origin, valid_part + invalid_part,
                       f'multipart/form-data; boundary={boundary}')
        assert code == 400, 'Multiple file parts accepted'
        assert post(ip, '/upload', nonce, origin)[0] == 400, 'Empty upload accepted'
        after = lists(ip)
        assert after['domains'] == before['domains'] and after['appliedProfile'] == before['appliedProfile']
        assert resolve(ip, 'googlesyndication.com') == ['0.0.0.0'], 'Old list lost on invalid upload'
        print('PASS: CSRF, domain validation, allowlist precedence/removal, invalid list preserves active list')
        if args.profile:
            code, message = post(ip, '/lists/profile?p=' + args.profile, nonce, origin)
            assert code == 202, (code, message)
            deadline = time.monotonic() + 180
            last_poll = 0.0
            after = lists(ip)
            while time.monotonic() < deadline:
                tick = time.monotonic()
                try:
                    answer = resolve(ip, 'googlesyndication.com')
                    assert answer == ['0.0.0.0'], f'DNS blocking lost during download: {answer}'
                except Exception:
                    diagnostic = stats(ip)
                    print('DNS failure state: ' + json.dumps({key: diagnostic[key] for key in
                          ('blocking', 'resumeIn', 'blocked', 'allowed', 'domains', 'heap', 'upstat')}), flush=True)
                    raise
                samples.append((time.monotonic() - tick) * 1000)
                # Match the dashboard's 450 ms busy-state polling separately
                # from DNS samples. Every UDP query still has to succeed.
                if time.monotonic() - last_poll >= .45:
                    after = lists(ip)
                    assert after['nonce'] == nonce, 'Device rebooted during profile update'
                    last_poll = time.monotonic()
                    if not after['busy']:
                        break
                time.sleep(.1)
            assert not after['busy'], 'Profile update timed out'
            assert after['appliedProfile'] == args.profile, after['status']
            assert after['domains'] > 0
            print('PASS: published HTTPS profile download, activation and uninterrupted DNS blocking')
            ordered = sorted(samples)
            print(json.dumps({'profile': after['appliedProfile'], 'domains': after['domains'],
                              'dns_samples': len(samples), 'dns_median_ms': round(statistics.median(samples), 2),
                              'dns_p95_ms': round(ordered[min(len(ordered)-1, int(len(ordered)*.95))], 2)}, indent=2))
    finally:
        if allowed_added and domain in lists(ip)['allowed']:
            post(ip, '/allowlist/remove?d=' + encoded, nonce, origin)
        if custom_added:
            request(ip, '/unblock?d=' + encoded)
        if initial['blocking']:
            request(ip, '/resume')
        elif initial['resumeIn'] == 0:
            request(ip, '/pause?s=0')
        else:
            remaining = initial['resumeIn'] - int(time.monotonic() - began)
            request(ip, f'/pause?s={remaining}' if remaining > 0 else '/resume')


if __name__ == '__main__':
    main()
