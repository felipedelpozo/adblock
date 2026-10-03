#!/usr/bin/env python3
"""Exercise the live sinkhole, web dashboard and shared volatile pause controls.

Usage: python3 tools/verify_device.py 192.168.1.50
Requires a populated blocklist and upstream Internet. Leaves pause state unchanged.
"""
import argparse
import json
import secrets
import socket
import struct
import time
import urllib.request


def request(ip, path):
    with urllib.request.urlopen(f'http://{ip}{path}', timeout=10) as response:
        return response.read()


def stats(ip):
    return json.loads(request(ip, '/stats.json'))


def resolve(ip, domain):
    ident = secrets.randbelow(65536)
    question = b''.join(bytes([len(label)]) + label.encode('ascii') for label in domain.split('.')) + b'\0\0\1\0\1'
    packet = struct.pack('!HHHHHH', ident, 0x0100, 1, 0, 0, 0) + question
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        sock.sendto(packet, (ip, 53))
        reply, peer = sock.recvfrom(2048)
    assert peer[0] == ip, peer
    rid, flags, questions, answers, _, _ = struct.unpack_from('!HHHHHH', reply)
    assert rid == ident and flags & 0x8000 and not flags & 0x000f and questions == 1
    cursor = 12
    while reply[cursor]:
        cursor += 1 + reply[cursor]
    cursor += 5
    result = []
    for _ in range(answers):
        if reply[cursor] & 0xc0 == 0xc0:
            cursor += 2
        else:
            while reply[cursor]:
                cursor += 1 + reply[cursor]
            cursor += 1
        kind, _, _, length = struct.unpack_from('!HHIH', reply, cursor)
        cursor += 10
        if kind == 1 and length == 4:
            result.append(socket.inet_ntoa(reply[cursor:cursor + 4]))
        cursor += length
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    args = parser.parse_args()
    ip = socket.gethostbyname(args.ip)
    initial = stats(ip)
    started = time.monotonic()
    assert initial['domains'] > 0, 'Install a blocklist before testing'
    assert b'<html' in request(ip, '/').lower(), 'Dashboard unavailable'
    try:
        request(ip, '/resume')
        assert resolve(ip, 'googlesyndication.com') == ['0.0.0.0'], 'Sinkhole failed'
        assert resolve(ip, 'test.googlesyndication.com') == ['0.0.0.0'], 'Suffix matching failed'
        forwarded = resolve(ip, 'example.org')
        assert forwarded and '0.0.0.0' not in forwarded, forwarded
        for duration in (300, 1800):
            request(ip, f'/pause?s={duration}')
            paused = stats(ip)
            assert not paused['blocking'] and duration - 5 <= paused['resumeIn'] <= duration, paused
            assert '0.0.0.0' not in resolve(ip, 'googlesyndication.com'), 'Pause failed to forward'
        request(ip, '/resume')
        assert stats(ip)['blocking'], 'Resume failed'
        assert resolve(ip, 'googlesyndication.com') == ['0.0.0.0'], 'Resume failed to block'
        print('PASS: dashboard, block/suffix, upstream, 5/30 minute pauses and resume')
        print(json.dumps(stats(ip), indent=2))
    finally:
        if initial['blocking']:
            request(ip, '/resume')
        elif initial['resumeIn'] == 0:
            request(ip, '/pause?s=0')
        else:
            remaining = initial['resumeIn'] - int(time.monotonic() - started)
            request(ip, f'/pause?s={remaining}' if remaining > 0 else '/resume')


if __name__ == '__main__':
    main()
