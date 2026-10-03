#!/usr/bin/env python3
"""Verify the bounded live blocked-query history without changing persistent settings.

Usage: python tools/verify_history.py DEVICE_IP
Generates 80 blocked queries; these deliberately replace the volatile history.
"""
import argparse
import json
import socket
import struct
import urllib.parse

from verify_device import request, resolve, stats


def history(ip, **params):
    return json.loads(request(ip, '/blocked.json?' + urllib.parse.urlencode(params)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    args = parser.parse_args()
    ip = socket.gethostbyname(args.ip)
    initial = stats(ip)
    assert initial['blocking'], 'Resume blocking before this check'
    assert initial['domains'] > 0
    marker = 'round-history-validation'
    for index in range(80):
        domain = f'query{index}.{marker}.doubleclick.net'
        assert resolve(ip, domain) == ['0.0.0.0'], domain
    newest = history(ip, q=marker, limit=16)
    assert newest['capacity'] == newest['total'] == 64, newest
    assert 0 < newest['count'] <= 64
    assert len(newest['entries']) == min(16, newest['count'])
    entry = newest['entries'][0]
    assert entry['domain'] == f'query79.{marker}.doubleclick.net', entry
    assert entry['reason'] == 'blocklist' and entry['type'] == 1, entry
    assert entry['ageSeconds'] >= 0 and entry['client']
    if newest['more']:
        page = history(ip, q=marker, offset=16, limit=16)
        assert len(page['entries']) == 16
        assert newest['entries'][-1]['id'] > page['entries'][0]['id']
    assert not history(ip, q='not-present-in-any-domain.invalid')['entries']
    assert history(ip, q='ROUND-HISTORY-VALIDATION')['count'] == newest['count']
    assert len(history(ip, limit=999)['entries']) <= 16
    assert not history(ip, offset=999)['entries']

    # IPv6 sinkhole replies have no answer, but still appear as type AAAA.
    domain = f'aaaa.{marker}.doubleclick.net'
    question = b''.join(bytes([len(label)]) + label.encode() for label in domain.split('.'))
    packet = struct.pack('!HHHHHH', 12345, 0x0100, 1, 0, 0, 0) + question + b'\0\0\x1c\0\1'
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(3)
        sock.sendto(packet, (ip, 53))
        reply, _ = sock.recvfrom(2048)
    assert struct.unpack_from('!HHHHHH', reply)[3] == 0
    entry = history(ip, q=domain)['entries'][0]
    assert entry['type'] == 28 and entry['reason'] == 'blocklist'
    print('PASS: bounded history, newest order, pagination, domain/client data, search, limits and AAAA')
    print(json.dumps({'capacity': newest['capacity'], 'retained': newest['total'], 'entry': entry}, indent=2))


if __name__ == '__main__':
    main()
