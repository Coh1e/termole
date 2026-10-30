# Issue tracker: GitHub

仓库：Coh1e/termole
地址：https://github.com/Coh1e/termole/issues

规格和任务均发布为 GitHub Issues。
优先使用 GitHub 连接工具；能力不足时使用 gh，
并显式指定 --repo Coh1e/termole。

- 读取任务时包含正文、标签和评论。
- 多行正文使用结构化参数或 --body-file，保留真实换行。
- 规格拆出的任务关联为子任务。
- 阻塞关系优先使用 GitHub 原生依赖；不可用时，
  在正文维护 Blocked by: #编号。
- 所有阻塞项完成后才能开始任务。
- 完整规格和已确认的实施任务标记 ready-for-agent。
- 标签映射见 triage-labels.md。

PRs as a request surface: no.
