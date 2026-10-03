## Summary

Describe the user-visible change and why it is needed.

## Scope

- Affected profile(s):
- Hardware tested, if any:
- Firmware version or commit used for testing:

## Validation

- [ ] `pio test -e native`
- [ ] `python -m unittest discover -s tests -v`
- [ ] `node --test tests/test_dashboard_firmware.cjs`
- [ ] Relevant PlatformIO profile build(s)
- [ ] Physical hardware checks listed separately from compile/test results

## Checklist

- [ ] Documentation updated when behavior or release procedures changed
- [ ] No credentials, flash dumps, or private device data included
- [ ] Diff reviewed for unrelated changes
