#!/bin/sh
# Kept for existing callers (ci-build.yml invokes it by name). The real script
# takes the architecture as an argument now; this is the arm64 spelling of it.
# Through sh: the mode bit on update_version.sh is not guaranteed on a fresh
# checkout, and the CI step only chmods this file.
exec sh "$(dirname "$0")/update_version.sh" arm64 "$@"
