#!/bin/bash
# No-container install: unpack the portable tarball into $HOME (or a prefix you name),
# check that it runs on THIS machine, and print the lines for your ~/.bashrc.
# No root, no modules, no packages.
#
#   install_tarball.sh objeck-portable-linux-x64.tgz [PREFIX]     # PREFIX defaults to $HOME/objeck
#
# The tarball comes from `build_image.sh portable-tgz` (built on AlmaLinux 8, glibc 2.28,
# C++ runtime and TLS/QUIC linked statically). The official release tarball will NOT run on
# CentOS 8: it needs glibc 2.38.
set -euo pipefail

TGZ=${1:-}
PREFIX=${2:-$HOME/objeck}
[ -f "$TGZ" ] || { echo "usage: $0 objeck-portable-linux-x64.tgz [PREFIX]" >&2; exit 2; }

mkdir -p "$PREFIX"
tar xzf "$TGZ" -C "$PREFIX" --strip-components=1
export OBJECK_HOME=$PREFIX
export OBJECK_LIB_PATH=$PREFIX/lib
export PATH=$PREFIX/bin:$PATH

cat "$PREFIX/PORTABLE_INFO" 2>/dev/null || true
echo "glibc here: $(ldd --version | head -1 | awk '{print $NF}')"
obc -version

# Compile and run a program: this exercises the loader, the .obl libraries and the VM.
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
cat > "$T/hello.obs" <<'EOS'
class Hello {
  function : Main(args : String[]) ~ Nil {
    cpus := Runtime->GetProperty("runtime.cpu.count")->ToInt();
    "objeck ok, usable cpus: {$cpus}"->PrintLine();
  }
}
EOS
obc -src "$T/hello.obs" -dest "$T/hello.obe"
obr "$T/hello.obe"

cat <<EOF

Installed in $PREFIX. Add to ~/.bashrc (and to your sbatch scripts, or use objeck-slurm's
spec "env"/"modules"):

  export OBJECK_HOME=$PREFIX
  export OBJECK_LIB_PATH=$PREFIX/lib
  export PATH=$PREFIX/bin:\$PATH
EOF
