# Phase 4A: Ollama Provider — Implementation Plan

## Global Constraints

- Error construction: use `Error` aggregate or factory methods (`Error::net()`, `Error::parse()`, `Error::not_found()`)
- Namespace: `ea::provider`
- Object library: `ea-provider` (CMake OBJECT library)
- Test tag: `[provider]`
- Ollama uses NDJSON streaming (NOT SSE) — must NOT use `SseParser`
- Ollama tool results use `tool_name` field (NOT `tool_call_id`)
- No API key needed for Ollama (local service)
- `ProviderCapabilities` for Ollama: `{native_tool_calling=true, streaming=true, vision=false, prompt_caching=false, extended_thinking=false}`

## Task 1: OllamaProvider Header — Config Extension + HttpClient

**Files:** `src/provider/OllamaProvider.h`

**Changes:**
- Expand `Config` struct: add `default_model = "llama3"`, `tls` (TlsConfig), `retry` (RetryPolicy)
- Add `net::HttpClient client_` private member
- Add public helper methods (for testing):
  - `json build_request_body(messages, tools, model, opts, stream) const`
  - `Result<LLMResponse> parse_response(const json& body) const`
- Update `capabilities()` to return `{true, true, false, false, false}`

**Verification:** Header compiles, existing tests still pass.

## Task 2: chat() + build_request_body() + parse_response()

**Files:** `src/provider/OllamaProvider.cpp`

**Changes:**

### build_request_body()
- `model` field from config or parameter
- `messages` array: convert each Message to Ollama format
  - System → `{"role": "system", "content": ...}`
  - User → `{"role": "user", "content": ...}`
  - Assistant (no tool_calls) → `{"role": "assistant", "content": ...}`
  - Assistant (with tool_calls) → `{"role": "assistant", "content": ..., "tool_calls": [...]}`
  - Tool → `{"role": "tool", "tool_name": msg.name, "content": msg.content}` (use `msg.name` for tool_name, fallback to `msg.tool_call_id`)
- `tools` array: same OpenAI format (`type: function, function: {name, description, parameters}`)
- `stream` boolean
- `options` object: map `ChatOptions` fields (`temperature`, `top_p`, `max_tokens` → `num_predict`)

### chat()
- Build request body with `stream: false`
- POST to `{base_url}/api/chat`
- No auth headers
- Parse response with `parse_response()`

### parse_response()
- `message.content` → `resp.content`
- `message.tool_calls` → parse each: `function.name`, `function.arguments` (may be string or object)
- `done_reason` → `resp.stop_reason` (map: "stop" → "stop", "tool_calls" → "tool_calls")
- `prompt_eval_count` → `resp.usage.input_tokens`
- `eval_count` → `resp.usage.output_tokens`

**Verification:** Unit test build_request_body output, parse_response with mock JSON.

## Task 3: stream_chat() + NDJSON Parsing

**Files:** `src/provider/OllamaProvider.cpp`

**Changes:**

### stream_chat()
- Build request body with `stream: true`
- POST to `{base_url}/api/chat` via `client_.stream_post()`
- NDJSON parsing: buffer incoming data, split on `\n`, parse each line as JSON
- For each NDJSON chunk:
  - `message.content` non-empty → emit `StreamChunk::Content` with `chunk.data = message.content`
  - `message.tool_calls` present → accumulate tool calls, emit `StreamChunk::ToolCallEnd` per call
  - `done: true` → emit `StreamChunk::Done` with usage from `prompt_eval_count`/`eval_count`
- Error handling: HTTP errors, JSON parse errors (log warning, skip malformed lines)

**Key:** Do NOT use `SseParser` — Ollama uses NDJSON, not SSE.

**Verification:** Test with mock NDJSON data.

## Task 4: list_models() + ProviderFactory Update

**Files:** `src/provider/OllamaProvider.cpp`, `src/provider/ProviderFactory.cpp`

**Changes:**

### list_models()
- GET `{base_url}/api/tags`
- Parse response: extract `models[].name`
- Return vector of model name strings
- On error: return empty vector (graceful degradation)

### ProviderFactory
- Update `ollama` branch to pass `cfg.default_model`, `cfg.tls`, `cfg.retry` to `OllamaProvider::Config`

**Verification:** list_models test with mock response, ProviderFactory creates OllamaProvider with full config.

## Task 5: Ollama Provider Tests

**Files:** `tests/test_provider_ollama.cpp` (new)

**Test cases (7-9 tests):**
1. `build_request_body produces correct Ollama format` — verify model, messages, tools, stream fields
2. `build_request_body converts tool result messages` — verify `tool_name` field
3. `parse_response extracts content and tool_calls` — mock JSON response
4. `parse_response extracts usage from eval counts` — prompt_eval_count/eval_count
5. `stream_chat parses NDJSON correctly` — mock NDJSON lines, verify StreamChunk sequence
6. `stream_chat handles tool_calls in stream` — mock stream with tool_calls
7. `list_models parses /api/tags response` — mock JSON
8. `chat handles HTTP errors gracefully` — mock error response
9. `ProviderFactory creates OllamaProvider with full config` — verify config propagation

**Tag:** `[provider]`

**Verification:** All tests pass, no regressions in existing test suite.

## Task 6: Final Verification

- Clean build: `cmake --build . --clean-first -j$(nproc)`
- Full test suite: `./tests/ea-tests`
- Zero project warnings
- All existing tests still pass
