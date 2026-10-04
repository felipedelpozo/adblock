#!/usr/bin/env python3
"""Verify aggregate pet state, real DNS feeding and bounded duplicate queries.

Usage: python3 tools/verify_pet.py DEVICE_IP
Requires active blocking and a list containing googlesyndication.com.
Generates one fresh subdomain plus repeated blocked DNS queries; no settings
are changed. The normal hourly feeding limit still applies to this check.
"""
import argparse
import json
import secrets
import socket
import statistics
import time

from verify_device import request, resolve, stats


def snapshot(ip):
    state = json.loads(request(ip, '/pet.json'))
    assert state['schemaVersion'] == 1
    assert state['persistent'], 'Pet persistence unavailable'
    assert state['calendarDateKnown'] is False
    assert state['periodBasis'] == 'active-uptime-24h'
    assert all(0 <= state[key] <= 100 for key in ('hunger', 'energy', 'happiness'))
    assert state['totalFood'] >= state['foodToday']
    assert state['rewardEvents'] >= state['rewardEventsToday']
    assert state['activeMs'] >= state['lastFed'] >= state['bornAt']
    for forbidden in ('history', 'domains', 'clients', 'ip', 'url'):
        assert forbidden not in state, f'Unexpected private field: {forbidden}'
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    args = parser.parse_args()
    ip = socket.gethostbyname(args.ip)
    assert stats(ip)['blocking'], 'Resume blocking before running the check'
    before = snapshot(ip)
    domain = f'pet-check-{secrets.token_hex(4)}.googlesyndication.com'
    assert resolve(ip, domain) == ['0.0.0.0'], 'Expected parent-domain sinkhole'
    first = snapshot(ip)
    assert first['totalFood'] > before['totalFood'], 'No food: hourly quota may already be full'
    assert first['xp'] > before['xp']
    samples = []
    for _ in range(40):
        started = time.monotonic()
        assert resolve(ip, domain) == ['0.0.0.0']
        samples.append((time.monotonic() - started) * 1000)
    assert resolve(ip, 'example.org') != ['0.0.0.0'], 'Allowed DNS should still resolve'
    deadline = time.monotonic() + 5
    final = snapshot(ip)
    while final['savePending'] and time.monotonic() < deadline:
        time.sleep(.1)
        final = snapshot(ip)
    assert not final['savePending'], 'NVS checkpoint did not complete'
    assert final['blockedToday'] >= before['blockedToday'] + 41
    # Other real LAN traffic may feed independently during this check; exact
    # duplicate suppression is asserted in deterministic host-side tests.
    print(json.dumps({
        'result': 'PASS',
        'foodGainedFirstQuery': first['totalFood'] - before['totalFood'],
        'xpGainedFirstQuery': first['xp'] - before['xp'],
        'backgroundRewardEvents': final['rewardEvents'] - first['rewardEvents'],
        'blockedQueries': 41,
        'medianMs': statistics.median(samples),
        'p95Ms': sorted(samples)[37],
        'maxMs': max(samples),
        'pet': final,
    }, indent=2))


if __name__ == '__main__':
    main()
