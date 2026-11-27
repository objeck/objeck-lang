#!/bin/sh
#
# Runs INSIDE a distribution's base image, started by
# tools/packaging/test_portable_linux.sh: /pkg is the unpacked release tree
# (read-only), /fixture holds this script, tls_smoke.obs, cert.pem and key.pem.
# The container has no network and nothing installed. POSIX sh only: the base
# images have no bash in common, no python, and not always tar.

set -u
fails=0
ok()  { echo "  ok: $*"; }
bad() { echo "  FAIL: $*"; fails=$((fails + 1)); }
step() { echo; echo "--- $*"; }

. /etc/os-release 2>/dev/null
echo "${PRETTY_NAME:-unknown system}, $(uname -m), $(ldd --version 2>&1 | head -n 1)"

# The way a user installs a tarball: unpack, set the two variables env.sh sets.
cp -R /pkg /opt/objeck || { echo "FAIL: cannot copy the tree"; exit 1; }
OBJECK=/opt/objeck
PATH="$OBJECK/bin:$PATH"
OBJECK_LIB_PATH="$OBJECK/lib"
export PATH OBJECK_LIB_PATH
unset LD_LIBRARY_PATH
mkdir -p /tmp/smoke && cd /tmp/smoke || exit 1

step "1. every tool starts"
# Each tool must be a dynamic executable of this machine (ldd exits 0), resolve every
# library, and RUN: a text file named bin/obi has the right name, and ldd says "not a
# dynamic executable" for it, which a grep for "not found" does not see. So the exit
# status of ldd is read, and each tool is started with an argument that makes it print
# and exit without doing anything: the status it exits with, and the text it prints, are
# what is held to. obc, obu and obi have a version or help option; obr, obd and obb
# print their usage and exit 1 when named no program.
for tool in obc obr obd obi obu obb; do
  if [ ! -f "$OBJECK/bin/$tool" ] || [ ! -x "$OBJECK/bin/$tool" ]; then
    bad "bin/$tool is missing or not executable"; continue
  fi
  linked=$(ldd "$OBJECK/bin/$tool" 2>&1); rc=$?
  missing=$(echo "$linked" | grep -E "not found|No such file|not a dynamic executable")
  if [ $rc -ne 0 ] || [ -n "$missing" ]; then
    bad "bin/$tool is not a dynamic executable that resolves its libraries (ldd exit $rc): $(echo "$linked" | tr -s ' \t\n' ' ')"
  else
    ok "bin/$tool resolves every library it links"
  fi
done
# tool, arguments, the exit status of the run, and what it prints
tool_runs() {
  echo "obc --version 0 [0-9]{4}\.[0-9]+\.[0-9]+"
  echo "obr - 1 Usage: obr"
  echo "obu version 0 [0-9]{4}\.[0-9]+\.[0-9]+"
  echo "obi --help 0 Usage: obi"
  echo "obd - 1 Usage: obd"
  echo "obb - 1 Usage: obb"
}
tool_runs | while read -r tool arg want_rc pattern; do
  if [ "$arg" = "-" ]; then out=$(timeout 30 "$tool" 2>&1 </dev/null); else out=$(timeout 30 "$tool" "$arg" 2>&1 </dev/null); fi
  rc=$?
  # kept for the version check below: asking a tool that may hang a second time,
  # without the timeout, is how a hung obc once stopped this script before its verdict
  echo "$out" > "/tmp/smoke/run_$tool.out"
  if [ "$rc" -eq "$want_rc" ] && echo "$out" | grep -qE "$pattern"; then
    echo "  ok: $tool ${arg#-} exits $rc and prints: $(echo "$out" | head -n 1 | cut -c 1-60)"
  else
    echo "  FAIL: $tool ${arg#-} exited $rc (wanted $want_rc) and printed: $(echo "$out" | head -n 2 | tr '\n' ' ' | cut -c 1-120)"
    echo "$tool" >> /tmp/smoke/tool_failed
  fi
done
if [ -e /tmp/smoke/tool_failed ]; then
  bad "a tool did not start and answer as it should: $(tr '\n' ' ' < /tmp/smoke/tool_failed)"
  # Nothing below can run without a compiler and a VM that answer, and a hung one would
  # hold every later step to its own timeout (or, with none, forever): the verdict is here.
  if grep -qxE 'obc|obr' /tmp/smoke/tool_failed; then
    echo
    echo "$fails check(s) failed on ${PRETTY_NAME:-this system}; the toolchain does not start, so the other steps were not run"
    exit 1
  fi
fi
# the release the tools report, from the bounded run above
ver=$(grep -oE '[0-9]{4}\.[0-9]+\.[0-9]+' /tmp/smoke/run_obc.out 2>/dev/null | head -n 1)
[ -n "$ver" ] && ok "this is release $ver" || bad "obc reported no version"

step "2. compile and run"
cat > hello.obs <<'EOF'
class Hello {
  function : Main(args : String[]) ~ Nil {
    names := ["portable", "linux"];
    each(i : names) {
      ("Hello from " + names[i])->PrintLine();
    };
  }
}
EOF
obc -src hello.obs -dest hello.obe >hello.log 2>&1 || { cat hello.log; bad "obc could not compile hello.obs"; }
out=$(obr hello.obe 2>&1 | tr '\n' ' ')
if [ "$out" = "Hello from portable Hello from linux " ]; then ok "hello: $out"; else bad "hello printed: $out"; fi

step "3. HTTPS to a TLS server on the loopback"
cp /fixture/tls_smoke.obs /fixture/cert.pem /fixture/key.pem .
if obc -src tls_smoke.obs -lib cipher,collect,xml,json,net,net_server -dest tls_smoke.obe >tls.log 2>&1; then
  out=$(timeout 120 obr tls_smoke.obe cert.pem key.pem 2>&1); rc=$?
  echo "$out" | sed 's/^/  /'
  if [ $rc -eq 0 ] && echo "$out" | grep -q TLS_SMOKE_OK; then ok "TLS client and server"; else bad "tls_smoke exited $rc"; fi
else
  cat tls.log; bad "obc could not compile tls_smoke.obs"
fi

step "4. the first 20 shipped examples"
cd "$OBJECK/examples" || exit 1
ran=0
grep '^| \[`[A-Za-z0-9_]*\.obs`\]' README.md | head -n 20 > /tmp/smoke/rows.txt
while IFS= read -r row; do
  name=$(echo "$row" | sed 's/^| \[`\([A-Za-z0-9_]*\)\.obs`\].*/\1/')
  libs=$(echo "$row" | sed -n 's/.*`-lib \([A-Za-z0-9_,@]*\)`.*/\1/p')
  if [ -n "$libs" ]; then
    obc -src "$name.obs" -lib "$libs" -dest "/tmp/smoke/$name.obe" >"/tmp/smoke/$name.log" 2>&1
  else
    obc -src "$name.obs" -dest "/tmp/smoke/$name.obe" >"/tmp/smoke/$name.log" 2>&1
  fi
  if [ $? -ne 0 ]; then
    tail -n 5 "/tmp/smoke/$name.log"; bad "$name.obs did not compile (-lib ${libs:-none})"; continue
  fi
  timeout 180 obr "/tmp/smoke/$name.obe" >"/tmp/smoke/$name.out" 2>&1 </dev/null; rc=$?
  if [ $rc -eq 0 ] && [ -s "/tmp/smoke/$name.out" ]; then
    ran=$((ran + 1))
  else
    tail -n 5 "/tmp/smoke/$name.out"; bad "$name exited $rc"
  fi
done < /tmp/smoke/rows.txt
[ "$ran" -eq 20 ] && ok "20 of 20 compiled, ran and exited 0 (JSON, XML, CSV, regex, SQL, threads, files, SHA-256)" \
  || bad "only $ran of 20 examples ran"
cd /tmp/smoke || exit 1

step "5. native libraries"
for lib in crypto diags ml onnx; do
  f="$OBJECK/lib/native/libobjk_$lib.so"
  [ -f "$f" ] || { bad "libobjk_$lib.so is not in the tree"; continue; }
  missing=$(LD_LIBRARY_PATH="$OBJECK/lib/native" ldd "$f" 2>&1 | grep "not found")
  if [ -n "$missing" ]; then
    bad "libobjk_$lib.so must be self-contained but cannot resolve: $(echo "$missing" | tr -s ' \t\n' ' ')"
  else
    ok "libobjk_$lib.so is self-contained"
  fi
done
for lib in odbc lame sdl opencv; do
  f="$OBJECK/lib/native/libobjk_$lib.so"
  [ -f "$f" ] || { bad "libobjk_$lib.so is not in the tree"; continue; }
  missing=$(ldd "$f" 2>&1 | grep "not found" | sed 's/ =>.*//' | tr -s ' \t\n' ' ')
  version=$(ldd "$f" 2>&1 | grep "version .* not found")
  [ -z "$version" ] || bad "libobjk_$lib.so needs a newer system library: $version"
  echo "  libobjk_$lib.so (optional) waits on system libraries:${missing:- none, it would load}"
done
# A native library whose system library is absent must be refused by name, with
# obr exiting 1: not a crash, and not a failure of anything that does not use it.
if ldd "$OBJECK/lib/native/libobjk_odbc.so" 2>&1 | grep -q "libodbc.so.2 => not found"; then
  if obc -src "$OBJECK/examples/odbc_21.obs" -lib odbc -dest odbc.obe >odbc.log 2>&1; then
    out=$(timeout 60 obr odbc.obe 2>&1 </dev/null); rc=$?
    if [ $rc -eq 1 ] && echo "$out" | grep -q "Runtime error loading shared library.*libodbc.so.2"; then
      ok "without unixODBC, an ODBC program stops with: $(echo "$out" | grep "Runtime error" | head -n 1)"
    else
      echo "$out" | tail -n 5; bad "ODBC without unixODBC exited $rc, not 1 with a load error"
    fi
  else
    cat odbc.log; bad "odbc_21.obs did not compile"
  fi
else
  echo "  unixODBC is installed here; the missing-library refusal is not exercised"
fi

step "6. obd without readline"
obc -src hello.obs --debug -dest hello_dbg.obe >dbg.log 2>&1 || { cat dbg.log; bad "obc --debug failed"; }
out=$(printf 'r\nq\n' | timeout 60 obd -bin hello_dbg.obe -src_dir . 2>&1); rc=$?
if [ $rc -eq 0 ] && echo "$out" | grep -q "Hello from linux"; then
  ok "obd ran the program and quit (readline: $(ls /lib*/libreadline.so.8 /usr/lib*/libreadline.so.8 /lib/*/libreadline.so.8 /usr/lib/*/libreadline.so.8 2>/dev/null | head -n 1 | grep -q . && echo present || echo absent))"
else
  echo "$out" | tail -n 8; bad "obd exited $rc"
fi

echo
if [ "$fails" -ne 0 ]; then
  echo "$fails check(s) failed on ${PRETTY_NAME:-this system}"
  exit 1
fi
echo "all checks passed on ${PRETTY_NAME:-this system}"
