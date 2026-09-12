import asyncio
import uuid
import time
import shutil
import subprocess
from pathlib import Path

from app.config import settings
from app.models.schemas import RunResponse

# Every Docker SDK call below is synchronous, requests-based I/O. Awaiting them
# from the event loop froze the whole worker for the length of a run: with
# --workers 2, two users running slow programs made the site unresponsive to
# everyone, /api/health included. They run on worker threads now.
#
# The per-IP rate limit does not bound total concurrency, so this caps how many
# containers can be in flight at once across all callers. Threads are bounded
# with it, since each in-flight run holds one.
_RUN_SEMAPHORE = asyncio.Semaphore(settings.max_concurrent_runs)

# Bounds on what a container's log can cost this process, independent of the
# daemon-side log_config cap. Generous against max_output_size (64 KB) so the
# user still sees the truncation notice rather than a silently short read.
_LOG_TAIL_LINES = 5000
_LOG_TAIL_BYTES = 1 << 20


async def run_code(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code — uses local dev mode or Docker sandbox."""
    async with _RUN_SEMAPHORE:
        if settings.local_dev:
            return await _run_local(code, libs, timeout)
        return await _run_docker(code, libs, timeout)


async def _run_local(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code directly with obc/obr (local dev, no Docker)."""
    return await asyncio.to_thread(_run_local_blocking, code, libs, timeout)


def _run_local_blocking(code: str, libs: list[str], timeout: int) -> RunResponse:
    """subprocess.run is blocking too — same treatment as the Docker path."""

    run_id = str(uuid.uuid4())[:12]
    work_dir = Path(settings.host_tmp_dir) / run_id
    start_time = time.monotonic()

    try:
        work_dir.mkdir(parents=True, exist_ok=True)
        src_file = work_dir / "program.obs"
        obe_file = work_dir / "program.obe"
        src_file.write_text(code, encoding="utf-8")

        env = {"OBJECK_LIB_PATH": settings.objeck_lib_path}

        # Build compile command
        cmd_compile = [settings.obc_path, "-src", str(src_file)]
        if libs:
            cmd_compile += ["-lib", ",".join(libs)]
        cmd_compile += ["-dest", str(obe_file)]

        # Compile
        result = subprocess.run(
            cmd_compile, capture_output=True, text=True,
            timeout=30, env=env,
        )

        if result.returncode != 0:
            elapsed_ms = int((time.monotonic() - start_time) * 1000)
            output = (result.stdout + result.stderr).strip()
            return RunResponse(
                success=False, output=output, error="",
                compile_error=True, execution_time_ms=elapsed_ms,
            )

        # Run
        result = subprocess.run(
            [settings.obr_path, str(obe_file)],
            capture_output=True, text=True,
            timeout=timeout, env=env,
        )

        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        output = (result.stdout + result.stderr).strip()

        truncated = False
        if len(output) > settings.max_output_size:
            output = output[:settings.max_output_size]
            output += "\n\n--- Output truncated (exceeded 64KB) ---"
            truncated = True

        return RunResponse(
            success=(result.returncode == 0),
            output=output,
            error="" if result.returncode == 0 else f"Process exited with code {result.returncode}",
            execution_time_ms=elapsed_ms,
            truncated=truncated,
        )

    except subprocess.TimeoutExpired:
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False, output="",
            error=f"Execution timed out after {timeout} seconds",
            execution_time_ms=elapsed_ms,
        )
    except Exception as e:
        import logging
        logging.getLogger(__name__).error("Local sandbox error: %s", e)
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False, output="",
            error="Internal error: code execution failed",
            execution_time_ms=elapsed_ms,
        )
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)



def _tail_logs(container) -> str:
    """Read at most the last _LOG_TAIL_BYTES of a container's output.

    container.logs() with no bound pulls the whole json-file log into this
    process and decodes it before anything truncates -- two copies in RSS for a
    program that printed for its whole timeout. The log_config cap above bounds
    it on disk; this bounds what crosses into the backend.
    """
    try:
        raw = container.logs(stdout=True, stderr=True, tail=_LOG_TAIL_LINES)
    except Exception:
        return ""
    if len(raw) > _LOG_TAIL_BYTES:
        raw = raw[-_LOG_TAIL_BYTES:]
    return raw.decode("utf-8", errors="replace")

async def _run_docker(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code in a sandboxed Docker container."""
    return await asyncio.to_thread(_run_docker_blocking, code, libs, timeout)


def _run_docker_blocking(code: str, libs: list[str], timeout: int) -> RunResponse:
    """The Docker work itself. Synchronous on purpose — run on a worker thread."""

    import docker
    client = docker.from_env()

    run_id = str(uuid.uuid4())[:12]
    container_name = f"playground-{run_id}"
    host_tmp = Path(settings.host_tmp_dir) / run_id
    start_time = time.monotonic()
    container = None

    try:
        # Write code to host temp file
        host_tmp.mkdir(parents=True, exist_ok=True)
        code_file = host_tmp / "program.obs"
        code_file.write_text(code, encoding="utf-8")

        # Build library flag
        lib_flag = f"-lib {','.join(libs)}" if libs else ""

        # Shell command inside the container:
        # 1. Copy from read-only mount to writable /tmp
        # 2. Compile
        # 3. Run with timeout
        cmd = (
            f"cp /input/program.obs /tmp/program.obs && "
            # Both steps are bounded. obc used to run unbounded, so a source that
            # made the compiler spin ran past container.wait() below and surfaced
            # as "Internal error" rather than a timeout.
            f"timeout {settings.compile_timeout} obc -src /tmp/program.obs {lib_flag} -dest /tmp/program.obe 2>&1 && "
            f"timeout {timeout} obr /tmp/program.obe 2>&1"
        )

        container = client.containers.run(
            image=settings.sandbox_image,
            command=["/bin/sh", "-c", cmd],
            name=container_name,
            detach=True,
            network_mode="none",
            read_only=True,
            # noexec: /tmp is the only writable path in an otherwise read-only
            # container, so without it a program can emit a binary and run it.
            tmpfs={"/tmp": f"rw,nosuid,nodev,noexec,size={settings.tmpfs_size}"},
            mem_limit=settings.container_memory,
            memswap_limit=settings.container_memory,
            cpu_period=100000,
            cpu_quota=int(settings.container_cpus * 100000),
            pids_limit=settings.container_pids_limit,
            # The daemon writes the container log on the HOST, so read_only and
            # the tmpfs cap do not bound it: an unbounded print loop filled the
            # host disk and was then read whole into this process before the
            # max_output_size truncation below could apply.
            log_config={"type": "json-file",
                        "config": {"max-size": "8m", "max-file": "1"}},
            cap_drop=["ALL"],
            security_opt=["no-new-privileges"],
            volumes={
                str(code_file.absolute()): {
                    "bind": "/input/program.obs",
                    "mode": "ro",
                }
            },
            user="sandbox",
        )

        # Wait for completion (add buffer for Docker overhead). A wait timeout is
        # a timeout, not an internal error -- it used to fall through to the bare
        # except below and be reported as "Internal error: code execution failed".
        try:
            result = container.wait(timeout=timeout + settings.compile_timeout + 5)
            exit_code = result["StatusCode"]
        except Exception:
            elapsed_ms = int((time.monotonic() - start_time) * 1000)
            return RunResponse(
                success=False,
                output=_tail_logs(container),
                error=f"Execution timed out after {timeout} seconds",
                execution_time_ms=elapsed_ms,
                truncated=True,
            )

        # Capture output
        stdout = _tail_logs(container)

        elapsed_ms = int((time.monotonic() - start_time) * 1000)

        # Truncate if too large
        truncated = False
        if len(stdout) > settings.max_output_size:
            stdout = stdout[: settings.max_output_size]
            stdout += "\n\n--- Output truncated (exceeded 64KB) ---"
            truncated = True

        # Detect compile error vs runtime error. This used to grep the output for
        # "error:", which is user-controlled: a program printing that string and
        # exiting non-zero was reported to its author as a compile error. obc runs
        # first in the && chain, so if it fails obr never runs and no .obe exists;
        # 124 from the compile step is its own timeout.
        compile_error = exit_code != 0 and "/tmp/program.obe" not in stdout and (
            "Expected" in stdout or "Undefined" in stdout or "obc:" in stdout
        )

        # timeout exit code is 124
        if exit_code == 124:
            return RunResponse(
                success=False,
                output=stdout,
                error=f"Execution timed out after {timeout} seconds",
                execution_time_ms=elapsed_ms,
                truncated=truncated,
            )

        return RunResponse(
            success=(exit_code == 0),
            output=stdout,
            error="" if exit_code == 0 else f"Process exited with code {exit_code}",
            compile_error=compile_error,
            execution_time_ms=elapsed_ms,
            truncated=truncated,
        )

    except docker.errors.ContainerError as e:
        import logging
        logging.getLogger(__name__).error("Container error: %s", e)
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False,
            output="",
            error="Execution failed in sandbox",
            execution_time_ms=elapsed_ms,
        )
    except Exception as e:
        import logging
        logging.getLogger(__name__).error("Docker sandbox error: %s", e)
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False,
            output="",
            error="Internal error: code execution failed",
            execution_time_ms=elapsed_ms,
        )
    finally:
        # Clean up container
        if container is not None:
            try:
                container.remove(force=True)
            except Exception:
                pass
        # Clean up host temp dir
        shutil.rmtree(host_tmp, ignore_errors=True)
