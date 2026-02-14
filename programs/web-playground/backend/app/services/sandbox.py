import uuid
import time
import shutil
import subprocess
from pathlib import Path

from app.config import settings
from app.models.schemas import RunResponse


async def run_code(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code — uses local dev mode or Docker sandbox."""
    if settings.local_dev:
        return await _run_local(code, libs, timeout)
    return await _run_docker(code, libs, timeout)


async def _run_local(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code directly with obc/obr (local dev, no Docker)."""

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


async def _run_docker(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code in a sandboxed Docker container."""

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
            f"obc -src /tmp/program.obs {lib_flag} -dest /tmp/program.obe 2>&1 && "
            f"timeout {timeout} obr /tmp/program.obe 2>&1"
        )

        container = client.containers.run(
            image=settings.sandbox_image,
            command=["/bin/sh", "-c", cmd],
            name=container_name,
            detach=True,
            network_mode="none",
            read_only=True,
            tmpfs={"/tmp": f"rw,nosuid,size={settings.tmpfs_size}"},
            mem_limit=settings.container_memory,
            memswap_limit=settings.container_memory,
            cpu_period=100000,
            cpu_quota=int(settings.container_cpus * 100000),
            pids_limit=settings.container_pids_limit,
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

        # Wait for completion (add buffer for Docker overhead)
        result = container.wait(timeout=timeout + 5)
        exit_code = result["StatusCode"]

        # Capture output
        stdout = container.logs(stdout=True, stderr=True).decode(
            "utf-8", errors="replace"
        )

        elapsed_ms = int((time.monotonic() - start_time) * 1000)

        # Truncate if too large
        truncated = False
        if len(stdout) > settings.max_output_size:
            stdout = stdout[: settings.max_output_size]
            stdout += "\n\n--- Output truncated (exceeded 64KB) ---"
            truncated = True

        # Detect compile error vs runtime error
        compile_error = exit_code != 0 and ("Expected" in stdout or "error:" in stdout.lower())

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
