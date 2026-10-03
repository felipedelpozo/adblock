# AdBlock 0.2.0 discarded candidate

The `v0.2.0` tag is retained for traceability; no public firmware release was
created at this tag. Do not distribute its CI application images.

Continued physical testing found a LittleFS reader assertion and device
restarts during profile downloads. Earlier DNS timeouts were not established
as a network-only limitation. The candidate was superseded by 0.2.1, which
removes the long-lived list reader and uses one owned reader per lookup.

See [0.2.1 release notes](RELEASE_V0.2.1.md) and
[final validation](RELEASE_VALIDATION_0.2.1.md).
