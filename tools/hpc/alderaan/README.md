# tools/hpc/alderaan

Objeck on the CU Denver Alderaan HPC cluster (Slurm, Singularity/Apptainer, CentOS 8, no root). The full guide, including what is unverified and a first-30-minutes checklist, is [docs/hpc_alderaan.md](../../../docs/hpc_alderaan.md).

| | |
|---|---|
| `build_image.sh` | `portable-tgz` (Docker, AlmaLinux 8), `docker-image` (a docker-archive to convert on the cluster), `sif` (apptainer `--fakeroot`) |
| `container/` | `build_portable.sh`, `Dockerfile.image`, `objeck-portable.def`, `objeck-release.def` |
| `install_tarball.sh` | no container: install the portable tarball into `$HOME` |
| `objeck_slurm.py` | parameter sweep to a Slurm job array (`plan`, `submit`, `local`, `collect`, `status`); Python 3.6+, no dependencies |
| `bench/scaling.py`, `bench/workloads/` | strong, weak, throughput, ONNX and multi-node scaling harness and report |
| `examples/sweep_example.json` | a sweep spec |
| `tests/` | `python3 tests/test_objeck_slurm.py`, `python3 tests/test_scaling.py` (no Slurm, container or Objeck needed) |
