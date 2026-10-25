from pydantic import BaseModel, Field


class RunRequest(BaseModel):
    code: str = Field(..., min_length=1, max_length=65536)
    # Optional. Libraries are worked out from the program's `use` lines, so a
    # client normally sends nothing. Anything listed is added on top (the UI's
    # collapsed 'advanced' override, and links from before libraries were inferred).
    libs: list[str] = Field(default_factory=list, max_length=10)
    timeout: int = Field(default=10, ge=1, le=10)


class Diagnostic(BaseModel):
    line: int
    column: int = 0
    severity: str = "error"  # error | warning
    message: str


class RunResponse(BaseModel):
    success: bool
    output: str
    error: str = ""
    compile_error: bool = False
    execution_time_ms: int = 0
    truncated: bool = False
    # What the server worked out (additive fields; older clients ignore them)
    libs: list[str] = Field(default_factory=list)   # libraries the program uses
    blocked: bool = False                            # refused before running: see `error`
    hint: str = ""                                   # advice for a compile error
    diagnostics: list[Diagnostic] = Field(default_factory=list)  # compiler errors with positions


class DemoInfo(BaseModel):
    id: str
    title: str
    description: str
    category: str
    libs: list[str] = []


class DemoDetail(DemoInfo):
    code: str


class ShareRequest(BaseModel):
    code: str = Field(..., min_length=1, max_length=65536)
    libs: list[str] = Field(default_factory=list, max_length=10)


class ShareResponse(BaseModel):
    id: str
    url: str


class ShareGetResponse(BaseModel):
    code: str
    libs: list[str]
