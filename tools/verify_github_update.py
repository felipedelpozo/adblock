#!/usr/bin/env python3
"""Verify GitHub TLS/check UI contract, CSRF rejection and DNS during a check.

This never installs a GitHub release. --reject-invalid-ota additionally verifies
that an invalid manual image and empty upload are rejected without rebooting.
"""
import argparse
import json
import statistics
import time
import urllib.error
import urllib.request

from verify_device import resolve, stats


def post(ip, path, nonce=None, origin=None, body=b'', content_type=None):
    headers = {}
    if nonce is not None:
        headers['X-CSRF-Token'] = nonce
    if origin is not None:
        headers['Origin'] = origin
    if content_type:
        headers['Content-Type'] = content_type
    request = urllib.request.Request(f'http://{ip}{path}', data=body, headers=headers, method='POST')
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('--reject-invalid-ota', action='store_true')
    args = parser.parse_args()
    ip = args.ip
    initial = stats(ip)
    assert not initial['githubBusy'], 'Wait for the existing operation to finish'
    nonce = initial['githubNonce']
    origin = f'http://{ip}'
    for token, source in ((nonce, None), (nonce, 'http://evil.example'), ('wrong', origin)):
        assert post(ip, '/github/check', token, source)[0] == 403
    assert post(ip, '/github/install?v=9.9.9', nonce, origin)[0] == 409
    assert post(ip, '/github/check', nonce, origin)[0] == 202
    samples = []
    deadline = time.monotonic() + 50
    result = None
    while time.monotonic() < deadline:
        start = time.monotonic()
        answer = resolve(ip, 'googlesyndication.com')
        samples.append((time.monotonic() - start) * 1000)
        if initial['blocking']:
            assert answer == ['0.0.0.0'], answer
        result = stats(ip)
        if not result['githubBusy']:
            break
        time.sleep(.05)
    assert result and not result['githubBusy'], 'GitHub check timed out'
    message = result['githubStatus']
    assert message == 'No hay releases publicadas' or message.startswith(('Release lista', 'La versión instalada')), message
    assert result['fwProfile'] == initial['fwProfile'] and result['fwVersion'] == initial['fwVersion']
    if args.reject_invalid_ota:
        assert post(ip, '/update')[0] == 400, 'An empty upload must not reboot'
        boundary = 'AdBlockInvalidImageTest'
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="f"; filename="invalid.bin"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode() + b'invalid firmware' + f'\r\n--{boundary}--\r\n'.encode()
        assert post(ip, '/update', body=body, content_type=f'multipart/form-data; boundary={boundary}')[0] == 400
    after = stats(ip)
    for field in ('domains', 'custom', 'upurl', 'upiv', 'fwProfile', 'fwVersion', 'githubNonce'):
        assert after[field] == initial[field], f'{field} changed or device rebooted'
    ordered = sorted(samples)
    print('PASS: HTTPS GitHub check, missing/invalid/cross-origin CSRF, unconfirmed installation rejection, DNS and persistence')
    if args.reject_invalid_ota:
        print('PASS: empty and invalid manual firmware uploads rejected without reboot')
    print(json.dumps({'status': message, 'profile': after['fwProfile'], 'version': after['fwVersion'],
                      'dns_samples': len(samples), 'dns_median_ms': round(statistics.median(samples), 2),
                      'dns_p95_ms': round(ordered[min(len(ordered) - 1, int(len(ordered) * .95))], 2),
                      'domains': after['domains']}, indent=2))


if __name__ == '__main__':
    main()
