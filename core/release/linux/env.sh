# Objeck environment. Source it, do not run it:
#
#     source ./env.sh
#
# The Windows and macOS launchers have a button that opens a shell with PATH and
# OBJECK_LIB_PATH already set, because on those platforms the installer finishes
# and leaves you without a terminal. On Linux you are already in one, so the
# button is unnecessary and these two exports are the whole of what it did.
# Until now the tarball shipped no equivalent at all and the README asked people
# to export both by hand.
#
# Safe to source twice: the PATH entry is added only if it is not already there.

_objeck_self=
_objeck_sourced=1

# bash and zsh each expose the path of a sourced file differently, and plain
# POSIX sh does not expose it at all -- `.` leaves $0 as the shell's own name.
# So the fallback is an explicit OBJECK_HOME, named in the error rather than
# left for the reader to work out.
if [ -n "${BASH_SOURCE:-}" ]; then
  _objeck_self="${BASH_SOURCE}"
  [ "${BASH_SOURCE}" = "$0" ] && _objeck_sourced=0
elif [ -n "${ZSH_VERSION:-}" ]; then
  _objeck_self="$0"                      # zsh sets $0 to the sourced file
  case "${ZSH_EVAL_CONTEXT:-}" in
    *:file*) ;;
    *) _objeck_sourced=0 ;;
  esac
else
  _objeck_self="${OBJECK_HOME:-}"
fi

# Executed rather than sourced: the exports would land in a child process and
# vanish with it. Say so, instead of appearing to work.
if [ "$_objeck_sourced" = "0" ]; then
  echo "env.sh must be SOURCED, not run -- otherwise the settings die with this process:" >&2
  echo "    source $0" >&2
  exit 1
fi

if [ -z "$_objeck_self" ]; then
  echo "env.sh: this shell does not reveal the path of a sourced file." >&2
  echo "        Use bash or zsh, or name the install root yourself:" >&2
  echo "          OBJECK_HOME=/path/to/objeck-lang . /path/to/objeck-lang/env.sh" >&2
  unset _objeck_self _objeck_sourced
  return 1
fi

if [ -d "$_objeck_self" ]; then
  _objeck_home="$_objeck_self"                       # OBJECK_HOME was used
else
  _objeck_home="$(cd "$(dirname "$_objeck_self")" && pwd)"
fi

# Checked before it is exported. A PATH pointing at a tree that is not an Objeck
# install is worse than no PATH at all: the failure then surfaces later, as
# something else.
if [ ! -x "$_objeck_home/bin/obc" ] || [ ! -d "$_objeck_home/lib" ]; then
  echo "env.sh: $_objeck_home does not look like an Objeck install" >&2
  echo "        (expected bin/obc and lib/ beneath it)" >&2
  unset _objeck_self _objeck_sourced _objeck_home
  return 1
fi

case ":${PATH}:" in
  *":$_objeck_home/bin:"*) ;;                        # already present, do not stack it up
  *) PATH="$_objeck_home/bin:$PATH" ;;
esac
export PATH

OBJECK_LIB_PATH="$_objeck_home/lib"
export OBJECK_LIB_PATH

echo "$("$_objeck_home/bin/obc" --version 2>/dev/null | head -1) ready"
echo "  $_objeck_home"

unset _objeck_self _objeck_sourced _objeck_home
