# Triage labels

| 角色 | GitHub 标签 | 含义 |
| --- | --- | --- |
| needs-triage | needs-triage | 待评估 |
| needs-info | needs-info | 待补充信息 |
| ready-for-agent | ready-for-agent | 可交给代理实现 |
| ready-for-human | ready-for-human | 需要人工处理 |
| verify | verify | 实现已完成，只剩人工或端到端验收；不阻塞其他票 |
| wontfix | wontfix | 不予实施 |

复用现有同名标签，只创建缺少的标签。

`verify` 是验证票的标签：实现已经在别的票交付，本票只欠确认。它不作为 blocking edge，避免「等人点一下」卡住实现工作。
