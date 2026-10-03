# DNS blocklist profiles and allowed domains

Firmware 0.2.0 adds curated DNS profiles and persistent domain exceptions.
Existing devices keep their installed list as **Custom**; upgrading firmware
does not silently replace a user's blocklist or update settings.

## Choose a profile

| Dashboard label | Source | Intended use |
| --- | --- | --- |
| Ligero | HaGeZi Multi Light | Conservative blocking with fewer application restrictions |
| Equilibrado | HaGeZi Multi PRO | Recommended balance of ads, tracking and application compatibility |
| Estricto | HaGeZi Multi PRO++ | Stronger tracking protection; may restrict legitimate application features |
| Personalizada | Existing/manual binary list | Keep the user's own compiled sources and update URL |

Choose a profile in the dashboard and apply it. The selected profile and
actually installed profile are displayed separately while a download is in
progress or after a failure. A successful update changes the active profile;
a failed update keeps the previously active list. Refreshing a curated profile
downloads the currently published snapshot. Scheduled refreshes use the saved
interval; firmware installation still requires a separate explicit action.

The profiles are alternatives, not three lists to stack together. HaGeZi
already includes native tracking coverage at different strengths. DNS filters
can block advertising SDKs with separate domains; they cannot reliably separate
advertisements from content hosted at the same domain. Android Private DNS,
app-owned encrypted DNS, alternate resolvers and IPv6 DNS can bypass this box.

## Allowed domains

Add an ASCII domain such as `ads.example.org` in the allowed-domain section,
or use the history action for a blocked domain. An exception covers that domain
and all its subdomains, overriding curated and custom domain rules. Client bans
still take precedence. Remove an exception to restore ordinary filtering.
There are at most 200 exceptions, persisted across reboot and firmware OTA.

Inputs are domain names, not URLs or browser filter expressions. Supply
internationalized domains as ASCII Punycode. Broad exceptions such as an entire
service's parent domain may allow its trackers as well as its content.

## Distribution and update safety

The separate [blocklists prerelease](https://github.com/felipedelpozo/adblock/releases/tag/blocklists)
contains a schema-1 `manifest.json`, content-addressed binary assets and source
license notices. It is not a firmware release and is excluded from GitHub's
latest stable release endpoint. The scheduled workflow builds lists from a
pinned upstream revision before publishing the manifest. Firmware verifies
HTTPS certificates, canonical repository asset URLs, declared byte count,
domain count, SHA-256 and strictly sorted unique 40-bit hashes.
The distribution retains currently referenced assets, at least the latest
three versions per profile/source archive, and every asset under seven days
old. Pruning happens after publication; an in-flight download has a bounded
timeout much shorter than this grace period.

Each entry occupies five bytes; the device searches the file in flash rather
than loading all domain strings into RAM. Downloads and validation run outside
the DNS loop, and the old file remains available until activation. Replacement
requires enough LittleFS space for both old and new files plus filesystem
headroom. If a C3 cannot stage a larger list, the request fails and leaves the
old list intact. Dual OTA partitions are unchanged.

Manual binary uploads remain supported and are validated before activation.
An optional custom update URL points to an already compiled `blocklist.bin`,
not a `.txt` source or an AdBlockLists catalogue page. Curated profiles add
manifest SHA-256 checks; a custom URL alone does not provide an independent
trusted hash. Administration is intended for a trusted LAN. CSRF tokens are not
user authentication, and hashes are not independent artifact signatures.

## Build and verify

```sh
# Existing hosts/domain sources, converted locally:
python tools/build_blocklist.py blocklist.bin SOURCE_URL_OR_FILE

# Offline regression suites:
pio test -e native
python -m unittest discover -s tests -v
node --test tests/test_dashboard_*.cjs

# Live checks restore temporary rules and the initial pause state:
python tools/verify_lists.py DEVICE_IP
# This deliberately installs and retains the selected curated profile:
python tools/verify_lists.py DEVICE_IP --profile balanced
```

Browser cosmetic filters, URL paths and script rules are not DNS domains.
Use a domain/hosts source when compiling custom lists; unsupported browser
syntax must not be converted into broad domain blocks.

## List licenses

Firmware source remains under its existing MIT license. HaGeZi publishes its
lists under GPL-3.0, independently of the firmware. The list distribution
includes attribution, the pinned source revision, the source form and GPL
license text. Preserve those materials when redistributing compiled lists.
See [HaGeZi DNS blocklists](https://github.com/hagezi/dns-blocklists) for current
source terms and upstream third-party notices.
