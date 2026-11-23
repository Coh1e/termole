# 分诊标签

| 标准角色 | GitHub 标签 | 含义 |
| --- | --- | --- |
| `needs-triage` | `needs-triage` | 等待维护者评估 |
| `needs-info` | `needs-info` | 等待报告者补充信息 |
| `ready-for-agent` | `ready-for-agent` | 规格完整，可交给代理独立实现 |
| `ready-for-human` | `ready-for-human` | 需要人工实施 |
| `wontfix` | `wontfix` | 不予处理 |

按表映射技能使用的分诊角色。复用同名标签，只创建缺少的标签。

项目另用 `verify` 标记实现已交付、只剩人工或端到端验收的任务。依赖关系按 [Issue 跟踪说明](issue-tracker.md#切票)处理。
