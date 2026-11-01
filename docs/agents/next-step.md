# Next-step guidance

## When to apply

Apply this document only to coding and directly related work, such as design,
verification, debugging, release preparation, retrospectives, or a necessary independent review.
Ordinary conversation, explanations, and standalone multimodal tasks use normal
responses. Media used as evidence for coding does not change the task category.

At a response boundary, choose at most one mode below. Continue authorized work
that you can complete now. Do not stop early to manufacture a next step.
Optional advice and routine acceptance do not create required work. When the user
explicitly requests only candidates or analysis, deliver only that output: omit
closing sections, questions, and publication. Use the user's response language for
headings and explanations; keep the AI continuation field names shown below.

For an applicable closing section, separate it from the result with `---` and use
one level-three heading. Select only one mode, including across question-tool text
and the final response. An active planning conversation can ask several rounds;
do not append a different closing mode to each round.

## Complete

Report the result, verification, and material limitations. When no required work
remains, close with `### 已完成` and a short statement that no additional action is
needed and the current chat can end. Do not ask for routine acceptance or invent
a continuation. A retrospective with no actionable candidates uses this mode.

## Human decision or action

Use this mode for an ordinary blocker requiring a human decision, permission,
information, or an operation that the agent cannot perform. Use `### 需要你决定`
or `### 需要你操作`. It takes priority over an AI continuation when human input
blocks that continuation. Do not turn a temporary blocker into a planning workflow.
For retrospective candidates or new improvement scope, use Improvement planning.

Explain the concrete decision or operation, why it is needed, and the recommended
choice. Use an available question tool within its supported purpose and mode.
If no suitable tool is available, ask a direct text question with numbered choices.

- For a decision, offer meaningful, mutually exclusive choices and identify the
  recommended choice. Allow free text when fixed choices cannot capture the answer.
- For an operation, state the steps and completion criteria. Ask for an outcome
  such as completed, blocked, or the relevant observed result. Use a normal text
  request when files or images are needed and the question tool cannot accept them.
- Wait for required input before dependent work. Continue independent authorized work.
  Silence and elapsed time are not approval or completion.
- Reuse existing authorization. Keep agent-performable work with the agent.
  Do not convert optional advice into a required human action.

Do not add Next Action, Chat, Reason, or Prompt fields in this mode.

## Improvement planning

Use `### 改进计划` for retrospective results or new improvement scope that needs
disposition. Show a draft plan, not just a prose recommendation or an untracked
to-do list. Explicit output-only constraints take precedence over this workflow.

### Route to the owning skill

Read the relevant skill instructions and project tracker, domain, and label
configuration before using a workflow. Those skills own their interview, artifact,
approval, and publication procedures; this document does not redefine them.

- Unresolved decisions: follow grilling, using the supported question tool or its
  text fallback. Reuse settled decisions rather than starting another interview.
- **Spec:** a behavior or design needs a full specification; follow to-spec, then
  to-tickets if decomposition is needed.
- **Ticket:** work is bounded and independently verifiable; follow to-tickets directly.
- **No action:** record the reason and applicable conditions, without an implementation
  task. Keep undecided or deferred candidates pending instead.

If a required skill or configuration is missing, report that dependency. Do not
silently substitute another tracker, invent a replacement workflow, or claim a
skill was invoked when it was not.

### Preserve tracking completeness

Give candidates stable IDs and show a disposition table with the recommendation,
rationale, and open decisions. Preserve source IDs when candidates merge.
Create or reuse one retrospective tracking issue in the configured tracker (or its
configured local equivalent). Map every candidate to a Spec, Ticket, or No action
reason, including conditions. Record pending decisions and publication status.
The tracking issue is an index, not an executable node in the ticket dependency graph.

Follow project authorization rules and the owning skill's confirmation/publication
requirements. Do not add another approval round for an already approved concrete
draft. Publishing a plan does not authorize implementation. On refusal, leave drafts
unpublished; do not create an unapproved fallback record.

Verify candidate mappings and actual artifact links. Drafts are not published issues.
Keep existing IDs and make unresolved decisions or partial publication failures
visible; check existing tracker records before retrying to prevent duplicates.
A continuation references those records and remaining work, rather than relying
on the human to remember a prose to-do list. Do not declare planning complete
while candidate dispositions remain unresolved.

## Long-running Matt implementation

Follow the active implement or implement-spec skill's completion criteria and
project authorization. This document controls response boundaries, not execution.
Continue authorized work across ticket completions, progress updates, tool calls,
and context compaction. Those events alone do not trigger a question or handover.
Use a closing mode only for necessary human input, an explicit handover, actual
inability to continue, or completion of the whole authorized goal.

Preserve the goal, authorization, spec/ticket references, task frontier, blockers,
and verification state across turns; resume without reopening settled scope.
Keep optional out-of-scope improvements in existing authorized records or session
context for later disposition. Do not interrupt execution or expand its scope.
Required review and resolution follow the owning skill, so code written alone
does not establish whole-task completion.

## AI continuation

Use this mode only when concrete coding or related work remains for a later turn
or another chat, and no required human decision or action blocks that work.
Recommend exactly one next action from the actual result, in this order:

1. Resolve unfinished verification or blockers that the agent can investigate.
2. Recommend an available, applicable Matt skill when the project setup supports it.
3. Recommend the necessary ordinary continuation when no Matt skill applies.

Before recommending a Matt skill, inspect the project's Agent skills entry points,
confirm that the candidate is available in the current environment, and read its
instructions to verify its trigger. Check only the configuration it requires:
issue tracker, domain documentation, and triage label mapping when relevant.
Follow the project's configured paths. Existing files do not prove external service
availability or authentication. These checks are read-only; they do not authorize
installation, configuration changes, or publication.

Recommend a missing setup step only if it prevents the required next action.
Do not invent work to use a skill. Include the skill name, goal, and necessary
context in the prompt when recommending one.

Separate this section from the result with a Markdown horizontal rule (`---`).
Use a level-three Next Action heading, one concrete action, a list for Chat and
Reason, and a level-four Prompt heading followed by a Markdown blockquote.
Render the structure as Markdown, outside code fences:

---

### Next Action

[One concrete action.]

- **Chat:** [SAME or NEW]
- **Reason:** [One short reason for this chat choice.]

#### Prompt

> [A ready-to-use prompt. Prefix every paragraph with >.]

Select SAME when current context remains useful. Select NEW for independent work
or a necessary separate review with a clear boundary. For NEW, make the prompt
self-contained: include the repository path, goal, relevant branch or commit,
changed files, verification evidence, unresolved issues, and acceptance criteria
as needed. Use verified facts and identify material unknowns.
