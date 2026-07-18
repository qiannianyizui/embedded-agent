# Phase 4A: Ollama Provider — Design Spec

## Overview

Implement the full OllamaProvider, replacing the current stub that returns "not yet implemented". Ollama's native `/api/chat` endpoint has a format similar to OpenAI but with key differences in streaming (NDJSON vs SSE), authentication (none), and tool result handling.

## Ollama API Reference

### POST /api/chat (non-streaming)

**Request:**
```json
{
  "model": "llama3",
  "messages": [
    {"role": "system", "content": "You are helpful."},
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "tool_calls": [
      {"type": "function", "function": {"index": 0, "name": "get_weather", "arguments": {"city": "NYC"}}}
    ]},
    {"role": "tool", "tool_name": "get_weather", "content": "22°C"}
  ],
  "tools": [
    {"type": "function", "function": {"name": "get_weather", "description": "Get weather", "parameters": {...}}}
  ],
  "stream": false,
  "options": {"temperature": 0.7, "num_predict": 4096}
}
```

**Response:**
```json
{
  "model": "llama3",
  "created_at": "2023-10-27T10:00:00Z",
  "message": {
    "role": "assistant",
    "content": "The weather in NYC is 22°C.",
    "tool_calls": []
  },
  "done": true,
  "done_reason": "stop",
  "prompt_eval_count": 15,
  "eval_count": 8,
  "total_duration": 500000000
}
```

### POST /api/chat (streaming)

**Request:** Same as above with `"stream": true`

**Response:** NDJSON (newline-delimited JSON), one JSON object per line:
```json
{"model":"llama3","created_at":"...","message":{"role":"assistant","content":"The"},"done":false}
{"model":"llama3","created_at":"...","message":{"role":"assistant","content":" weather"},"done":false}
{"model":"llama3","created_at":"...","message":{"role":"assistant","content":" is 22°C."},"done":false}
{"model":"llama3","created_at":"...","message":{"role":"assistant","content":"","tool_calls":[{"type":"function","function":{"index":0,"name":"get_weather","arguments":{"city":"NYC"}}}]},"done":false}
{"model":"llama3","created_at":"...","message":{"role":"assistant"},"done":true,"done_reason":"stop","prompt_eval_count":15,"eval_count":8}
```

### GET /api/tags

**Response:**
```json
{
  "models": [
    {
      "name": "llama3",
      "model": "llama3",
      "modified_at": "2025-10-03T23:34:03Z",
      "size": 4661224676,
      "digest": "...",
      "details": {"format": "gguf", "family": "llama", "parameter_size": "8.0B", "quantization_level": "Q4_0"}
    }
  ]
}
```

## Key Differences from OpenAI

| Aspect | OpenAI | Ollama |
|--------|--------|--------|
| Chat endpoint | `/v1/chat/completions` | `/api/chat` |
| Auth header | `Authorization: Bearer KEY` | None |
| Streaming format | SSE (`data: {...}\n\n`) | NDJSON (`{...}\n`) |
| Stream content path | `choices[0].delta.content` | `message.content` |
| Stream done signal | `data: [DONE]` | `done: true` in last chunk |
| Tool result role | `tool` + `tool_call_id` | `tool` + `tool_name` |
| Model list endpoint | `/v1/models` | `/api/tags` |
| Usage fields | `usage.prompt_tokens` | `prompt_eval_count` / `eval_count` |
| Tool calls in response | `choices[0].message.tool_calls` | `message.tool_calls` |

## Implementation Design

### 1. Config Extension

```cpp
struct Config {
    std::string base_url = "http://localhost:11434";
    std::string default_model = "llama3";
    std::chrono::milliseconds timeout{120000};
    net::TlsConfig tls;
    net::RetryPolicy retry;
};
```

Add `default_model`, `tls`, and `retry` fields. The `api_key` is not needed (Ollama is local).

### 2. Capabilities Update

```cpp
ProviderCapabilities capabilities() const override {
    return {true, true, false, false, false};
    // native_tool_calling=true, streaming=true
}
```

### 3. build_request_body()

Convert internal Message/ToolSpec to Ollama format:
- System messages → `{"role": "system", "content": ...}`
- User/Assistant messages → same as OpenAI
- Tool result messages → `{"role": "tool", "tool_name": ..., "content": ...}` (Ollama uses `tool_name` not `tool_call_id`)
- Assistant with tool_calls → `{"role": "assistant", "tool_calls": [...]}`
- Tools → same OpenAI format (`type: function, function: {name, description, parameters}`)
- Options → map `ChatOptions` fields to Ollama `options` object

### 4. parse_response()

Parse Ollama non-streaming response:
- `message.content` → `LLMResponse.content`
- `message.tool_calls` → `LLMResponse.tool_calls` (same structure as OpenAI)
- `done_reason` → `LLMResponse.stop_reason`
- `prompt_eval_count` → `LLMResponse.usage.input_tokens`
- `eval_count` → `LLMResponse.usage.output_tokens`

### 5. stream_chat()

NDJSON parsing strategy:
- Use `HttpClient::stream_post()` to receive raw data
- Buffer data, split on `\n`
- Parse each line as JSON
- For each chunk:
  - `message.content` non-empty → emit `StreamChunk::Content`
  - `message.tool_calls` present → accumulate, emit `StreamChunk::ToolCallEnd` per call
  - `done: true` → emit `StreamChunk::Done` with usage

### 6. list_models()

GET `/api/tags`, extract `models[].name` from response.

### 7. ProviderFactory Update

Update the `ollama` branch to pass `default_model`, `tls`, `retry` from `ProviderConfig`.

## File Changes

| File | Change |
|------|--------|
| `src/provider/OllamaProvider.h` | Expand Config, add HttpClient member, add build_request_body/parse_response helpers |
| `src/provider/OllamaProvider.cpp` | Full implementation of chat/stream_chat/list_models |
| `src/provider/ProviderFactory.cpp` | Pass new Config fields |
| `tests/test_provider_ollama.cpp` | New test file with mock-based tests |

## Test Strategy

Use a mock HTTP approach: inject a mock HttpClient or test build_request_body/parse_response in isolation. Tests cover:
1. build_request_body — correct Ollama format
2. parse_response — content + tool_calls + usage
3. stream_chat — NDJSON parsing
4. list_models — /api/tags parsing
5. Tool result message format (tool_name vs tool_call_id)
6. Error handling (HTTP errors, parse errors)
7. Integration with ProviderFactory
