"""Set a validated release version before compiling any firmware profile."""
import os
import re

Import("env")

version = os.environ.get("ADBLOCK_FW_VERSION")
if version:
    if not re.fullmatch(r"(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})\.(?:0|[1-9][0-9]{0,5})", version):
        raise ValueError("ADBLOCK_FW_VERSION must be a stable major.minor.patch version")
    env.Append(CPPDEFINES=[("ADBLOCK_FW_VERSION", env.StringifyMacro(version))])
