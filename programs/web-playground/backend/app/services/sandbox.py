import uuid
import time
import shutil
from pathlib import Path

import docker

from app.config import settings
from app.models.schemas import RunResponse

client = docker.from_env()


async def run_code(code: str, libs: list[str], timeout: int) -> RunResponse:
    """Execute Objeck code in a sandboxed Docker container."""

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
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False,
            output="",
            error=f"Container error: {e}",
            execution_time_ms=elapsed_ms,
        )
    except Exception as e:
        elapsed_ms = int((time.monotonic() - start_time) * 1000)
        return RunResponse(
            success=False,
            output="",
            error=f"Internal error: {e}",
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
