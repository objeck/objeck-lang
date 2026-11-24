# Platform dependence of compiled libraries: x64 reference outputs

Question: why do json, misc, net_server, nlp, odbc, query, regex and xml come out a few bytes different
when `obc` is built and run on Apple silicon (arm64, clang, libc++), while lang.obl and the rest match the
committed x64 set? The differing bytes are small integers (jump operands, which `JumpToLocation` makes
instruction indices) off by 2.

`x64/<lib>/` holds the reference side for xml, json and regex, made by `tools/cicd/obl_platform_diff.sh <lib>`:

- `SUMMARY`: `cksum` (checksum, size) of the `.obl` for each of `-opt s0 s1 s2 s3`.
- `<lib>.x64.tar.gz`: for each level, `<lib>.sN.obl`, `<lib>.sN.obm` (the compiler's assembly listing, `-asm`) and
  `<lib>.sN.log`.

## Build identity of the x64 side

| | |
|---|---|
| source | compiler sources of master `fa736be0ed` (the compiler tree is unchanged since `5588cf7e40`); stage 1 `obl_platform_diff.sh` from `b0ec756440` |
| machine | x86_64, WSL2 Ubuntu 24.04.4 LTS |
| compiler | g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, libstdc++ of that release |
| libc | glibc 2.39 (Ubuntu GLIBC 2.39-0ubuntu8.12) |
| flags | `core/compiler/make/Makefile.amd64`: `-O3 -flto=auto -Wall -D_X64 -std=c++20 -mavx2` |
| obc | `md5sum core/compiler/obc` = `101c7de171f0946d7433a9a98e744052` (2,135,416 bytes) |
| libraries linked | the committed `core/lib/*.obl` (equal to a from-source rebuild: 52 of 52, see #1763) |

For this build the s3 output of each library is byte-identical to the committed `core/lib/<lib>.obl`, and the output did not
change under three heap layouts (normal, every allocation mmap'd so addresses descend, `MALLOC_PERTURB_=85`), so
pointer-address ordering is not what differs on glibc.

## Reproduce on the other machine and compare

From the repository root, with `core/compiler/obc` built for that machine and `core/lib/*.obl` present:

    bash tools/cicd/obl_platform_diff.sh xml            # also json, regex
    mkdir /tmp/x64 && tar xzf tools/research/obl_platform_diff/x64/xml/xml.x64.tar.gz -C /tmp/x64
    diff tools/research/obl_platform_diff/x64/xml/SUMMARY /tmp/obl_platform_diff/xml/SUMMARY     # which -opt level first differs
    diff /tmp/x64/xml.s0.obm /tmp/obl_platform_diff/xml/xml.s0.obm | head -50                    # then the first level that differs

The first opt level whose SUMMARY differs names the pass to inspect. If s0 already differs the cause is before optimization
(parser/analyzer/emitter); if only s2/s3 differ it is an optimizer pass.
