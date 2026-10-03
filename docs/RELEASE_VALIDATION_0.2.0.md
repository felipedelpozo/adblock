# AdBlock 0.2.0 candidate validation — rejected

The candidate passed local regression tests and four-target compilation, and
its GitHub tag workflow passed. Some live list updates also completed.
These checks were insufficient: subsequent physical tests reproduced device
restarts and `lfs_file_read_` assertions while looking up DNS names during
profile downloads. The backtrace included `VFSFileImpl::seek`,
`BlocklistManager::lookup` and `handleDns`.

This was a real filesystem-reader failure, not a proven radio or polling
latency issue. Serializing filesystem operations alone did not eliminate it.
The long-lived `File` reader was removed in the final 0.2.1 implementation.
No claim is made about the exact internal cause of the stale descriptor.

No 0.2.0 firmware release was published. The existing tag and CI artifact
remain historical evidence, not recommended installation assets.
The final checks are recorded in
[0.2.1 validation](RELEASE_VALIDATION_0.2.1.md).
