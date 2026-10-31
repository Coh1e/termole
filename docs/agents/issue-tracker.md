# Issue tracker: GitHub

仓库：Coh1e/termole
地址：https://github.com/Coh1e/termole/issues

规格和任务均发布为 GitHub Issues。
优先使用 GitHub 连接工具；能力不足时使用 gh，
并显式指定 --repo Coh1e/termole。

- 当前 GitHub 仓库没有分支和代码（空仓库），代码只在本地工作区；
  不要假设远端有代码，也不要从远端取实现。
- 读取任务时包含正文、标签和评论。
- 多行正文使用结构化参数或 --body-file，保留真实换行。
- 规格拆出的任务关联为子任务。
- 阻塞关系优先使用 GitHub 原生依赖；不可用时，
  在正文维护 Blocked by: #编号。
- 所有阻塞项完成后才能开始任务。
- 完整规格和已确认的实施任务标记 ready-for-agent。
- 标签映射见 triage-labels.md。

## 切票

规格拆票时按契约切，不按界面切。

- 一条验收标准只属于一张票：等待、退出码、stdio 字节这类契约在一张票里定义，其他票引用编号，不复述标准。
- 先定契约归属，再按入口或层分片；入口票不要复述下游票的验收标准。
- 验证与交付分开：实现完成的票可以关闭；只剩人工或端到端确认的另开 `verify` 票。
- `verify` 票不作为 blocking edge。blocking edges 只连交付票。

## 关闭

- 关闭前留一条证据评论：验证文档路径或提交号，写明覆盖了哪些验收标准、哪些仍未覆盖。
- 同一改动内的行为声明，必须在同一改动里找到对应代码和测试依据（见 `CODING_STANDARDS.md`）。

## 状态查询

- 状态以 GitHub 为准；`docs/issue-index.md` 只维护阻塞边。
- `scripts/issue-status.ps1` 把两者 join 起来，输出 READY / BLOCKED / DRIFT。

PRs as a request surface: no.
