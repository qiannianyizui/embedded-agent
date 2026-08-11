# Hermes 上下文管理同步

## 目标

将 embedded-agent 的上下文管理从“固定 100 条历史裁剪”迁移到 Hermes 的
“token 驱动压缩 + 全量落库 + 可归档恢复”模型。

## 范围

1. 移除 `HistoryPruneStep` 与 `max_messages` 硬编码。
2. 升级 `ContextCompressor`：
   - 模型 context window 感知的 token 阈值；
   - head/tail 保护（token 预算 + 消息数下限）；
   - 旧 tool 结果预清理（一行摘要、去重、截断参数）；
   - 中间轮次 LLM 摘要 + 迭代更新 + 静态 fallback；
   - 真实 usage 反馈与 anti-thrashing。
3. 手动 `/compress [focus]`。
4. DB 归档：`messages.active` 列、`archive_and_compact()`、
   `load()` 只读活跃上下文。

## 实施顺序

1. Config 扩展（Compression 参数）。
2. AgentLoop 移除固定裁剪。
3. ContextCompressor 重写。
4. /compress 命令。
5. DB 归档。
6. 测试与全量构建。

## 验收标准

- 无 `max_messages` 硬编码；历史不再按条数裁剪。
- 压缩阈值由 token 估计/真实 usage 控制。
- `/compress` 可手动压缩并提示结果。
- 压缩后 DB 保留全部历史（active=0 归档），活跃上下文只含压缩结果。
- 全部现有测试 + 新增回归测试通过。
