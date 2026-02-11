from pydantic import BaseModel, Field


class RunRequest(BaseModel):
    code: str = Field(..., min_length=1, max_length=65536)
    libs: list[str] = Field(default_factory=list, max_length=10)
    timeout: int = Field(default=10, ge=1, le=10)


class RunResponse(BaseModel):
    success: bool
    output: str
    error: str = ""
    compile_error: bool = False
    execution_time_ms: int = 0
    truncated: bool = False


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
