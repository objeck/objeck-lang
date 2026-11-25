# Objeck on the Alderaan cluster (Slurm, Singularity, no root)

How to run Objeck on the CU Denver Alderaan HPC cluster, run a parameter sweep as a Slurm job array, and measure how well it scales. Everything is in [`tools/hpc/alderaan/`](../tools/hpc/alderaan/). It was written and tested without cluster access; the section [What I could not verify](#what-i-could-not-verify-without-cluster-access) lists exactly what is still an assumption. Read that section before you trust any number from the cluster.

## The problem

The cluster, as described in the maintainer's notes, runs CentOS 8 (glibc 2.28) with Slurm, offers Singularity or Apptainer and no Docker, gives users no root, and allows user-level installs only.

The official Linux release does not run there. It is built on Ubuntu 24.04 and needs glibc 2.38 ([README, Platform support](../README.md)), and it links system libraries (mbedTLS `.so.14`, OpenCV `.so.406`) that CentOS 8 does not package. Two ways around that, both provided here:

| Route | What you get | Needs on the cluster |
|---|---|---|
| **Container image** (recommended) | Either the *portable* flavor (small) or the *release* flavor (the official tarball on Ubuntu 24.04, with ONNX Runtime and OpenCV). A container runs on the host kernel with its own glibc, so glibc 2.28 does not matter. | `apptainer`/`singularity` (to run), no root |
| **Portable tarball into `$HOME`** | `obr`, `obc`, every `.obl` library and `libobjk_ml` (matrix/ML), built on AlmaLinux 8 so the binaries need only glibc 2.28 or newer. No ONNX, OpenCV, SDL or ODBC natives. | nothing but a shell and `tar` |

The portable build is the recipe from the 2026-11 portable-Linux spike: `obr` needs glibc 2.26 or older symbols, `obc` 2.25, and only `libdl`, `libz`, `libm`, `libpthread`, `libc` at run time (libstdc++, libgcc, mbedTLS 3.6.4, AWS-LC, ngtcp2, nghttp3 and nghttp2 are linked in statically). It was rebuilt for this page from this tree and run in `rockylinux:8` (a CentOS 8 userland), `ubuntu 24.04` and a Docker image; see [Tested here](#tested-here-and-only-reasoned).

## Build the artifacts (on your laptop or any machine with Docker)

```
cd tools/hpc/alderaan
./build_image.sh portable-tgz                       # ~15 min: dist/objeck-portable-linux-x64.tgz
./build_image.sh docker-image --flavor portable     # dist/objeck-portable.tar (a docker-archive)
```

The full flavor wraps the official tarball, which you download yourself from the GitHub release page (it is not fetched for you):

```
./build_image.sh docker-image --flavor release --release-tgz ~/Downloads/objeck-linux-x64_2026.11.3.tgz
```

`build_image.sh` modes:

| Mode | Where it runs | Result |
|---|---|---|
| `portable-tgz` | Docker | `dist/objeck-portable-linux-x64.tgz`, built from this checkout on `almalinux:8` |
| `docker-image` | Docker | `dist/objeck-<flavor>.tar`; also the `.sif` if apptainer is installed on the same machine |
| `sif` | a machine with apptainer and `--fakeroot` (or root) | `dist/objeck-<flavor>.sif` straight from `container/objeck-<flavor>.def` |

## Get Objeck onto the cluster

### A. Container (no root, no Docker on the cluster)

Copy the Docker archive and convert it on a login node. Converting an archive needs no root:

```
scp dist/objeck-portable.tar <user>@<alderaan-login>:~/
ssh <user>@<alderaan-login>
apptainer build ~/objeck-portable.sif docker-archive:~/objeck-portable.tar     # or: singularity build ...
singularity exec ~/objeck-portable.sif obc -version
```

Or build the `.sif` where `--fakeroot` is allowed, from the definition files (`container/objeck-portable.def`, `container/objeck-release.def`; the comments in each give the exact command). Which one to use:

- `objeck-portable`: about 150 MB. obr, obc, all `.obl` libraries, `libobjk_ml`. Enough for the CPU-bound workloads and sweeps.
- `objeck-release`: about 1.2 GB. Everything the release ships, including `libobjk_onnx` and ONNX Runtime 1.19 (CPU). Needed for the ONNX benchmark. Add `--nv` to `singularity exec` on GPU nodes; the shipped ONNX Runtime is CPU-only ([ONNX_CUDA_PLAN.md](ONNX_CUDA_PLAN.md)).

Inside either image `obr`, `obc` are on `PATH` and `OBJECK_LIB_PATH` is set.

### B. No container: a tarball in `$HOME`

```
scp dist/objeck-portable-linux-x64.tgz tools/hpc/alderaan/install_tarball.sh <user>@<alderaan-login>:~/
ssh <user>@<alderaan-login>
bash install_tarball.sh objeck-portable-linux-x64.tgz          # installs to ~/objeck, compiles and runs hello
```

Then put the three exports it prints into `~/.bashrc` (or the `env` block of a sweep spec):

```
export OBJECK_HOME=$HOME/objeck
export OBJECK_LIB_PATH=$HOME/objeck/lib
export PATH=$HOME/objeck/bin:$PATH
```

## `objeck-slurm`: a parameter sweep as a job array

`tools/hpc/alderaan/objeck_slurm.py` is a single-file launcher (Python 3.6 or newer, standard library only: that is CentOS 8's system Python). The program that runs inside a task is an ordinary Objeck program; the helper is not an Objeck library, so there is nothing to build or install beyond the file. The only convention is optional: a task may print `RESULT key=value key=value` lines and the helper turns them into CSV columns.

### The spec

```json
{
  "name": "demo",
  "program": "../bench/build/sweep_task.obe",
  "runner": ["obr"],
  "args": ["{n}", "{seed}"],
  "grid": {"n": [100000, 1000000], "seed": [1, 2, 3, 4]},
  "repeats": 1,
  "resources": {"cpus_per_task": 2, "mem": "4G", "time": "00:20:00",
                "partition": "math-alderaan", "throttle": 8},
  "container": {"image": "~/objeck-portable.sif"},
  "task_timeout": 900
}
```

- **Parameters** come from exactly one of `grid` (cartesian product, in key order), `sets` (a list of objects) or `csv` (a header row plus one row per set). `repeats: R` runs every set R times and adds a `rep` parameter.
- **`args`** are templates: `{name}` is replaced by a parameter, `{task_id}` by the task number. An unknown name is an error at plan time, not on the cluster.
- **`program`** is relative to the spec file. **`runner`** is the command that runs it (`["obr"]` by default; for a program that is itself an executable, such as a shell script, use `"runner": []`).
- **`resources`**: `cpus_per_task`, `mem` or `mem_per_cpu`, `time`, `partition`, `account`, `qos`, `constraint`, `gres`, `throttle` (the `%K`), `pack` (see below), `extra_sbatch` (raw `#SBATCH` lines).
- **`container`**: `image`, `cmd` (`singularity` by default; set `apptainer` if that is what the cluster has), `binds`, `nv`. Directories outside `$HOME` and `/tmp` (the output directory, the working directory) are bound automatically.
- **`env`**, **`modules`** (`module load ...` lines), **`python`** (the interpreter the job script calls, `python3` by default), **`task_timeout`** (seconds), **`workdir`**.

### Commands

```
objeck_slurm.py plan    SPEC --dry-run        # print the sbatch script and the task table, write nothing
objeck_slurm.py local   SPEC --workers 4      # run the whole sweep here with a process pool
objeck_slurm.py submit  SPEC                  # write manifest + job.sbatch and run sbatch
objeck_slurm.py status  OUTDIR                # how many tasks are ok / failed / unfinished
objeck_slurm.py collect OUTDIR                # merge results/*.json into OUTDIR/results.csv
objeck_slurm.py submit  SPEC --only-failed    # resubmit just the array elements without an ok result
```

`--outdir` defaults to `sweep_<name>` beside the spec. Put it on a shared file system (`$HOME` or scratch): the compute nodes read the manifest and write the results there.

### What it generates

`plan --dry-run` on the example prints (abridged):

```
#SBATCH --job-name=demo
#SBATCH --partition=math-alderaan
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=2
#SBATCH --mem=4G
#SBATCH --time=00:20:00
#SBATCH --array=0-11%8
#SBATCH --output=<outdir>/logs/slurm_%A_%a.out
#SBATCH --error=<outdir>/logs/slurm_%A_%a.err
export OBJECK_CPU_COUNT="${SLURM_CPUS_PER_TASK:-2}"
python3 .../objeck_slurm.py run-task --manifest <outdir>/manifest.json
```

Each array element runs `run-task`, which picks its task from `SLURM_ARRAY_TASK_ID`, runs `singularity exec [--bind ...] IMAGE obr PROGRAM ARGS...`, and writes `results/task_NNNNN.json` (atomically, so a half-written record is never seen). The task environment gets:

| Variable | Value |
|---|---|
| `OBJECK_CPU_COUNT` | the CPUs the task was given: `SLURM_CPUS_PER_TASK`, else the spec's `cpus_per_task` (it is always set explicitly, never inherited) |
| `OBJECK_SWEEP_<NAME>` | every parameter, upper-cased, for programs that prefer the environment to `argv` |
| `OBJECK_SWEEP_TASK_ID` | the task number |
| `SLURM_*` | whatever Slurm set; `SLURM_JOB_ID`, `SLURM_ARRAY_JOB_ID` and `SLURM_ARRAY_TASK_ID` are recorded in the CSV |

`OBJECK_CPU_COUNT` matters because `runtime.cpu.count` (the number every Objeck thread pool is sized from) is the smaller of the affinity mask and the cgroup quota, and whether Slurm narrows either depends on how the cluster is configured (`TaskPlugin`, `ProctrackType`), which I could not check. Setting it from `SLURM_CPUS_PER_TASK` makes the answer independent of that. Under Singularity the variable is forwarded unprefixed and also as `SINGULARITYENV_`/`APPTAINERENV_`.

**`pack`**: a cluster's `MaxArraySize` (often 1001) can be smaller than the sweep. `"pack": 4` makes each array element run four tasks back to back, so 3,000 tasks need 750 elements.

### The result CSV

`collect` (and `local`, which collects when it finishes) writes one row per task, in task order:

```
task_id,status,exit_code,wall_s,start_utc,end_utc,host,slurm_job_id,slurm_array_job_id,slurm_array_task_id,cpus_per_task,<parameters...>,<RESULT keys...>
```

`status` is `ok`, `failed`, `timeout` or `missing` (no record: the task never ran, the node died, or the time limit hit first). A `RESULT` key that collides with a parameter or a fixed column is written as `result_<key>`. A task that prints no `RESULT` line gets its last output line in `stdout_last`. Per-task stdout and stderr are in `logs/task_NNNNN.{out,err}`; Slurm's own stdout/stderr are in `logs/slurm_*.{out,err}`.

### Local mode is the same code path

`local` runs the same `run-task` function through a pool of subprocesses (workers = usable CPUs divided by `cpus_per_task`, capped by `throttle`), so a sweep that works locally takes the same path on the cluster. Test a sweep on a login node's `--workers 2` or on your laptop first.

## Scaling benchmarks

Harness: [`tools/hpc/alderaan/bench/scaling.py`](../tools/hpc/alderaan/bench/scaling.py). One command builds the workloads, one runs the suites, one prints the tables.

### What each suite answers

| Suite | Workload | Question | Metric |
|---|---|---|---|
| `strong-kernel` | `scale_kernel.obs`, fixed total work (`--units`), threads = cores | How much faster does a fixed job get with more cores? | speedup `S = T1/Tk`, efficiency `S/k` |
| `weak-kernel` | the same, work grows with the cores (`--units-per-core`) | Does the time stay flat when the work scales with the cores? | efficiency `T1/Tk` |
| `clbg-weak` | `nbody`, `fannkuchredux`, `spectralnorm` from `programs/tests/clbg`: k independent copies, one per core | Do k single-threaded programs interfere (memory bandwidth, turbo, GC)? | efficiency `T1/Tk` |
| `mandelbrot` | the CLBG mandelbrot (fixed three threads) in a cpuset of k cores | Does it saturate at 3 cores, as it should? | speedup, efficiency |
| `onnx-sessions` | `onnx_infer.obs`: k concurrent processes, one ONNX CPU session each | How many sessions per node before throughput stops growing? | inferences/s, efficiency |
| `onnx-intra` | one session in a cpuset of k cores (intra-op threads follow `runtime.cpu.count`) | How much does one session gain from more cores? | speedup |
| multi-node | `multinode-specs` + `objeck_slurm.py` | Does an embarrassingly parallel sweep scale across nodes? | makespan speedup, efficiency |

The strong-kernel checksum is verified in the harness: it depends only on `--units` and `--steps`, never on the thread count, so a speedup measured over different work is refused (`CHECKSUM MISMATCH`, exit 1) rather than reported. The same property is pinned by the regression test `programs/regression/hpc_scale_kernel.obs`.

### Measurement method

- **Repetitions:** `--reps 5 --warmup 1` by default. The warm-up run is discarded. Tables show the **median**, the minimum and the coefficient of variation; a CV above 5% is flagged in the report, and the answer is more repetitions or a quieter node, never a longer-looking average.
- **Pinning:** each measurement runs inside a cpuset of exactly k CPUs from the CPUs the job may use (`os.sched_setaffinity`, the mechanism `taskset` uses), and sets `OBJECK_CPU_COUNT=k`. The CPUs are taken in order from the allowed set, starting at `--offset`, `--stride` apart: on a node whose hyper-thread siblings are adjacent (`lscpu -e`) use `--stride 2` to measure physical cores. The effective count is the one the runtime reports (`usable_cpus`), recorded per row; the report warns when it differs from the requested k.
- **Environment capture:** `scaling.py env` writes the host, kernel, `lscpu` model, sockets/cores/threads/NUMA, the affinity set, the **cgroup CPU quota** (v1 and v2), the Slurm variables, memory, and the Objeck runtime's own `runtime.cpu.count`. Run it inside the job, next to the measurements, and keep the file with the CSV.
- **What a run needs from Slurm:** a **whole node, exclusively** (`--exclusive`, or `--cpus-per-task` = all cores and `--mem=0`). Sharing a node with other users' jobs gives noisy and optimistic numbers. Use `--cpu-bind=none` so the harness controls the cpuset itself, and run under `srun`/`sbatch` rather than a login node.
- **Sizing:** pick sizes so one core takes at least 10 s and the largest core count at least 0.5 s: the harness times whole processes, whose start-up is a few milliseconds. The defaults suit a 3 to 5 GHz core.
- **A warm-up inside the kernel:** see [a VM finding](#finding-a-two-thread-jit-compile-race) below; `scale_kernel` warms its method on the main thread before starting threads, so its numbers are not exposed to it. The CLBG programs have no such warm-up.

### Run it

```
# inside an interactive allocation or an sbatch script that owns the whole node:
#   srun --exclusive --nodes=1 --cpus-per-task=<all cores> --mem=0 --time=02:00:00 --pty bash
cd objeck-lang/tools/hpc/alderaan/bench
export OBJECK_LIB_PATH=...                      # or run inside the container (see below)
python3 scaling.py build --obc "$(which obc)" --repo ~/objeck-lang            # compiles workloads to ./build
python3 scaling.py env --out env_$SLURM_JOB_ID.json
python3 scaling.py run strong-kernel weak-kernel clbg-weak mandelbrot \
        --cores 1,2,4,8,16,32 --reps 5 --out scaling_$SLURM_JOB_ID.csv
python3 scaling.py report scaling_$SLURM_JOB_ID.csv --md report_$SLURM_JOB_ID.md
```

In a container: `singularity exec ~/objeck-portable.sif python3 ...` needs Python in the image, which it does not carry; run the harness from the host and let it call a wrapper instead: `--obr` takes any command path, so write `obr_sif.sh` containing `exec singularity exec ~/objeck-portable.sif obr "$@"` and pass `--obr ./obr_sif.sh`, and build with `--obc` the same way. (Not tried on the cluster.)

ONNX suites need the release image or an install with `libobjk_onnx`, a model and its vocabulary:

```
python3 scaling.py build --obc ... --with-onnx     # adds -lib models,opencv,onnx,nlp,nlp_onnx,json
python3 scaling.py run onnx-sessions onnx-intra --cores 1,2,4,8,16,32 \
        --model model.onnx --vocab vocab.txt --iterations 2000 --batch 8 --out onnx_$SLURM_JOB_ID.csv
```

`programs/regression/data/bert/tiny_bert.onnx` is a toy (a few microseconds per call, so process start-up dominates); use a real sentence-embedding model of the size you care about (see [MODELS.md](MODELS.md)). With the 1-core cpuset each session gets one intra-op thread; `onnx-intra` shows the other extreme.

### Multi-node sweep

```
python3 scaling.py multinode-specs --nodes 1,2,4,8 --cores-per-node 32 --tasks 64 --tasks-per-node 8 \
        --partition math-alderaan --container ~/objeck-portable.sif --outdir ms
bash ms/submit_all.sh                       # submits ms_strong_k{1,2,4,8} and ms_weak_k{1,2,4,8}
# when all are done:
for d in ms/sweep_ms_*; do python3 ../objeck_slurm.py collect $d; done
python3 scaling.py multinode-report ms/sweep_ms_strong_k* ms/sweep_ms_weak_k*
```

One array element is one whole node (`--exclusive`, all cores) and `--array=0-N%K` lets K elements run at once, so K is the node count. Strong scaling keeps the task count fixed while K grows; weak scaling grows it as `--tasks-per-node * K`. The report takes the **makespan** (first task start to last task end, from the recorded UTC times, so queue wait is excluded), the number of distinct hosts actually used, the median task time, and the speedup and efficiency against the smallest K. Run the K values one after another if the cluster is shared, otherwise they compete for the same nodes; if the scheduler will not give K nodes at once the `distinct hosts` column shows it.

### Reading the tables

A strong-scaling efficiency below about 80% at k cores means the work is too small for k, a serial part dominates, or the cores are not independent (memory bandwidth, SMT siblings, a shared cache). Compare `strong-kernel` with `clbg-weak`: the kernel touches no memory, so it isolates the VM and thread start-up; the CLBG programs add allocation and memory traffic. A weak-scaling efficiency that falls with k at a fixed per-core size points at shared resources, not at Objeck. The tables are the deliverable; bring back the CSV, the `env_*.json` and the Slurm job script.

### Finding: a two-thread JIT compile race

While validating the harness, `scale_kernel` with two worker threads took about 20 s instead of about 1 s in roughly one run in four, pinned or not, with `OBJECK_CPU_COUNT` set or not (WSL2 on a Ryzen 9 7950X3D, the portable build: 12 of 40 runs across five pinning variants, and 2 of 16 more without the warm-up). The same program with a one-unit warm-up call on the main thread before the threads start took 1.05 to 1.13 s in all 16 runs, and with the warm-up three threads were steady at 0.72 to 0.84 s over 8 runs. (A small `--jit=off` run was fast but proves little: it is too short to tell.) That fits two threads entering the same long-running method together while it is being JIT-compiled, one of them executing the whole call (there is no on-stack replacement) in the interpreter, which is about 19 times slower here. I have not located it in the VM; it is not fixed by this change. It matters for you because **any Objeck program whose worker threads start on one long-running method can lose a factor of about 20 on a run, without warning**: the CLBG mandelbrot showed a 5.3 s versus 2.8 s run pair under a two-core cpuset in the same session. Mitigations until it is fixed: warm the hot method once on the main thread, as `scale_kernel.obs` does, and always use the median of several runs. Do not average runs with a bimodal outlier into a speedup.

## First 30 minutes on Alderaan

Do these in order; each step says what a pass looks like. Stop at the first step that fails and send the output.

1. **Log in and look around (3 min).** `hostname; cat /etc/os-release | head -3; ldd --version | head -1; nproc; free -g | head -2; python3 --version; which apptainer singularity; module avail 2>&1 | head -30`. Expect CentOS 8, glibc 2.28, and one of `apptainer`/`singularity`. Note the Python version (3.6 is fine) and whether a `python3` module is needed.
2. **See your Slurm environment (3 min).** `sinfo -s; sinfo -o "%P %a %l %D %c %m %G" | head; scontrol show config | grep -E "TaskPlugin|ProctrackType|MaxArraySize|SelectType"; sacctmgr -n show assoc user=$USER format=account,partition,qos,maxjobs,maxsubmit 2>&1 | head`. Write down the partition names (the notes say `math-alderaan*`), `MaxArraySize` (decides `pack`), the time limit, cores and memory per node, whether any node lists a GPU (`%G`), and your account.
3. **Get the image (5 min).** From your laptop: `scp objeck-portable.tar <user>@<login>:~/`. On the login node: `apptainer build ~/objeck-portable.sif docker-archive:~/objeck-portable.tar` (or `singularity build ...`). Pass: a `.sif` of about 150 MB and no error. If it fails on the login node, ask the admins whether builds belong on a compute node (`srun --pty bash` first). **No container?** `bash install_tarball.sh objeck-portable-linux-x64.tgz`.
4. **Smoke test, on a login node, one core (2 min).** `singularity exec ~/objeck-portable.sif obc -version` then compile and run any hello program. Pass: the version prints `2026.11.3 Objeck (Linux x86_64)` or newer.
5. **Smoke test through Slurm (5 min).** `srun -p math-alderaan --cpus-per-task=4 --time=00:05:00 singularity exec ~/objeck-portable.sif sh -c 'obc -src cpu_probe.obs -dest /tmp/p.obe >/dev/null && obr /tmp/p.obe'` (compile `bench/workloads/cpu_probe.obs` first; any partition you may use will do). **Pass: `usable_cpus=4`.** If it prints the node's whole core count, Slurm is not narrowing affinity or the cgroup for this job: that is exactly why `objeck-slurm` sets `OBJECK_CPU_COUNT`, and it is worth telling the admins.
6. **Sweep dry run, then a tiny real sweep (7 min).** `python3 objeck_slurm.py plan examples/sweep_example.json --dry-run`; check partition, `--array`, paths. Compile `sweep_task.obs` to `bench/build/sweep_task.obe`, shrink the example to 8 tasks, `submit` it, `squeue -u $USER`, then `collect`. Pass: `8 tasks: 8 ok` and a CSV whose `host` column and `cpus` column look right.
7. **Environment capture (3 min).** In an exclusive allocation: `python3 bench/scaling.py env --out env.json` and keep it. Check `cgroup_cpu_limit` (null means no quota), `affinity_count`, and `usable_cpus_runtime`.
8. **Start the scaling run (2 min).** Submit the `scaling.py run ...` commands above as one exclusive-node `sbatch` job and go and do something else.

Bring back: `env.json`, the scaling CSVs, the `sweep_*/results.csv` files, the `sbatch` scripts, and the exact `sinfo`/`scontrol` lines from step 2.

## Tested here, and only reasoned

**Run, here, with the output seen** (2026-11-24, a Windows host with WSL2 Ubuntu 24.04 and Docker Desktop):

- The portable tarball built in an `almalinux:8` container from this tree: `obr` max `GLIBC_2.26`, `obc` max `GLIBC_2.25`, `NEEDED` = libdl, libz, libm, libpthread, libc, ld-linux; a 7 MB tarball with `libobjk_ml` in `lib/native`.
- `install_tarball.sh` on WSL Ubuntu and in `rockylinux:8` (a CentOS 8 userland, glibc 2.28, Python 3.6): compile, run, `runtime.cpu.count` printed.
- The Docker route: `build_image.sh docker-image --flavor portable`, then the image run as an unprivileged user with `--cpus 2`: `usable_cpus=2`, and `OBJECK_CPU_COUNT=1` gives 1.
- `test_objeck_slurm.py` (22 tests) and `test_scaling.py` (13) on Windows, WSL (Python 3.12) and `rockylinux:8` (Python 3.6, including the test that compiles and runs `sweep_task.obs` through `local` with the real `obr`). They use shims for `sbatch` and `singularity`, run the **generated `job.sbatch` with `bash`** once per array element with `SLURM_ARRAY_TASK_ID`, `SLURM_CPUS_PER_TASK`, `SLURM_JOB_ID` set, and require the collected CSV to equal what `local` produces.
- `programs/regression/hpc_scale_kernel.obs`: passes with the JIT and with `--jit=off`; its expected values were computed with Python.
- The whole scaling cycle (`build`, `env`, `run strong-kernel weak-kernel clbg-weak mandelbrot`, `report`) on WSL and inside `rockylinux:8`: strong kernel 1.00, 1.93, 3.56 speedup at 1, 2, 4 cores (96%, 89%); weak kernel 98% and 94%; these are laptop-class numbers on a busy shared machine and mean nothing for Alderaan.

**Only reasoned (not run):** everything involving a real Slurm (the `#SBATCH` directive spellings follow the `sbatch` manual; the shim only proves the script is consumed and the job id parsed); `apptainer build` of either `.def` file or of a docker-archive (no apptainer here: the definition files are unexecuted); the `release` flavor image (needs the release tarball, which was not downloaded; its package list is copied from `core/release/runtime_deps.json`); GPU use; the ONNX suites on Linux (`onnx_infer.obs` was compiled with `-lib models,opencv,onnx,nlp,nlp_onnx,json` and run once against `tiny_bert.onnx` on the Windows host's own build, 8,933 embeddings/s on 32 CPUs, but no Linux ONNX native was built here, and the `onnx-sessions`/`onnx-intra` suites ran only against a fake `obr`); the multi-node sweep on more than one node.

## What I could not verify without cluster access

Everything below is a premise taken from the maintainer's notes or an assumption of mine, not something checked against Alderaan:

1. **The OS and glibc** (CentOS 8, 2.28). Checked only that the portable binaries need glibc 2.26 or less; if the nodes differ from the login node, check on a compute node.
2. **Singularity vs Apptainer, and which version.** The definition files and `docker-archive:` conversion are standard, but `--fakeroot` may be disabled, and `docker-archive:` import needs a reasonably recent release. The spec's `container.cmd` is `singularity` by default.
3. **Whether image builds are allowed on login nodes**, and where (`$HOME` quota, `SINGULARITY_CACHEDIR`/`APPTAINER_CACHEDIR`, `/tmp` size).
4. **Partition names, time limits, `MaxArraySize`, `--exclusive` and `--mem=0` policy, accounts and QOS.** Only `math-alderaan*` is known from the notes; `account`/`qos` are left out unless you set them.
5. **How Slurm confines a job** (`TaskPlugin`, cgroup constraints). If it does not narrow affinity or cgroup, `OBJECK_CPU_COUNT` still sizes Objeck correctly, but a process could use more cores than it was given.
6. **Python 3 on the compute nodes.** The job script calls `python3` (the `python` spec key and `modules` can change it). CentOS 8 ships Python 3.6 only as `platform-python` unless a package or module is installed.
7. **A shared file system** visible at the same path on login and compute nodes, which the manifest and results rely on.
8. **GPUs.** Unconfirmed. No GPU-specific code is in this change; the shipped ONNX Runtime is CPU-only. `resources.gres` and `container.nv` are passed through untested.
9. **Outbound network from compute nodes.** Nothing here needs it at run time, but `apptainer build` of a `docker://` image would, which is why the archive route is the default.
10. **CPU model and turbo/SMT policy**, which decide what a "core" means in the scaling tables (`--stride` exists for SMT siblings).
11. **ARM64.** The portable build is x86-64 only. If any node is aarch64 this does not apply.

## Files

| Path | What |
|---|---|
| `tools/hpc/alderaan/build_image.sh` | portable tarball, Docker image / archive, `.sif` |
| `tools/hpc/alderaan/container/` | `build_portable.sh` (runs in AlmaLinux 8), `Dockerfile.image`, `objeck-portable.def`, `objeck-release.def` |
| `tools/hpc/alderaan/install_tarball.sh` | the no-container install into `$HOME` |
| `tools/hpc/alderaan/objeck_slurm.py` | the sweep helper |
| `tools/hpc/alderaan/examples/sweep_example.json` | a sweep spec |
| `tools/hpc/alderaan/bench/scaling.py`, `bench/workloads/` | the scaling harness and its Objeck workloads |
| `tools/hpc/alderaan/tests/` | the helper and harness tests (CI: the tools job) |
| `programs/regression/hpc_scale_kernel.obs` | the kernel's thread-count independence |
