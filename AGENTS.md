## Repository Map

The root `codemap.md` is a navigation snapshot only. Keep it current when repository structure changes, but do not use it as an authority over live source.

Before working on any task, inspect the exact relevant source files, build files, plans, and documentation for the current task scope.

For deep work on a specific folder, prefer that folder's current documentation if it exists and is directly relevant; otherwise trace the live code path.

## General Agent Workflow

For all non-trivial implementation, debugging, refactor, and validation tasks, follow this workflow:

0. Read `AGENTS.md` first.
   - Before starting any thinking, planning, subagent workflow, implementation exploration, code change, or validation step, read `AGENTS.md`.
   - Treat `AGENTS.md` as the first mandatory workflow gate for every task in this project.
   - Do not begin any work until the current `AGENTS.md` instructions have been reviewed.

1. Plan the work before editing code.
   - State the objective.
   - Identify the relevant pipeline or workflow end to end.
   - Identify gatekeeping events that must pass before later steps are meaningful.

2. Show evidence for the plan.
   - Read the relevant current code paths, build files, plans, and documentation first.
   - Trace the actual runtime or build path instead of assuming behavior.
   - Base decisions on present code and observed evidence, not speculation.

3. Create and maintain explicit tasks.
   - Break the work into concrete steps.
   - Re-prioritize tasks when a blocker invalidates the current path.
   - Treat major gatekeeping blockers as higher priority than downstream fixes.


4. Delegate only with explicit sequencing and strict prompt bounds.
   - These rules apply to all agents and subagents used in this repository.
   - If one agent depends on another agent's output, do not spawn them simultaneously.
   - Run the first agent, wait for it to finish, then spawn the dependent agent.
   - Forbidden pattern: plan + review in parallel, or implementation + review in parallel.
   - Before delegating non-trivial work, write the plan and task list to a file that other agents can read.
   - Reuse that file as the shared source of truth when spawning follow-up agents.
   - The main agent acts as the orchestrator, not the primary coding/exploring/reviewing agent when a suitable agent exists.
   - Use code exploration tools and/or exploration agents for codebase discovery.
   - Exploration agents must be prompted with exact scope boundaries so they do not drift into unrelated code.
   - If an agent drifts or returns bad feedback, stop it, discard the result, and respawn a new one with narrower scope and more explicit commands.

5. Use precise, bounded agent prompts only.
   - Every agent prompt must contain exact file paths, directories, symbols, or exact commands.
   - Every agent prompt must contain exact acceptance criteria, exact stop conditions, and explicit non-goals.
   - Do not ask agents to inspect broadly, figure things out, make broad decisions, or assume missing requirements.
   - Prefer constrained, mechanical prompts over open-ended exploratory prompts.
   - If exact scope boundaries are not yet known, first use a bounded exploration tool or exploration agent to establish them, then delegate the next step from that result.

6. Review the plan and tasks before implementation.
   - Confirm the planned fix matches the actual pipeline.
   - Confirm the expected outcome is tied to concrete evidence.
   - If evidence is weak or indirect, continue investigation instead of editing code.

7. Verify preconditions before making code changes.
   - If the build, configure, runtime, or dependency state prevents meaningful validation, fix that blocker first.
   - Do not continue speculative feature fixes when a gating blocker prevents review and verification.

8. Implement the smallest justified change.
   - Prefer minimal, targeted edits.
   - Avoid single-line or localized fixes that are not checked against the full affected pipeline.
   - Consider all affected occurrences, call paths, and feature interactions before keeping a change.

9. Review the implementation after each fix attempt.
   - Review not only the edited lines, but each affected path and related stage in the pipeline.
   - Check whether the change impacts other features, workflows, or assumptions.
   - Verify the change still aligns with the final user-facing goal.

10. Verify results with evidence.
   - Build, run, test, or otherwise validate the change whenever the task requires verification.
   - If verification is blocked, treat that blocker as the active task.
   - Do not present unverified speculative fixes as completed work.

11. Handle fallout immediately.
   - If a fix reveals blockers, broken builds, or regressions in other features, address those before considering the work done.
   - If the change does not produce a concrete, reviewable improvement, revert it.
   - Do not leave dead, speculative, or ineffective code in the codebase.

12. Close only after end-goal review.
   - Confirm the final result solves the intended problem.
   - Confirm no known affected feature remains broken because of the fix.
   - Summarize the evidence used, the validations performed, and any remaining blockers.


## Behavioral Rules

- Never assume anything.
- Treat all user-provided paths, constraints, and scope boundaries as authoritative unless the user explicitly revises them.
- If a required fact, path, environment detail, or workflow condition is unknown, stop and ask or inspect only the exact user-provided scope needed to answer it.
- Do not speculate on fixes without concrete and presentable evidence.
- Do not keep code changes that cannot be justified against the traced pipeline.
- Do not ignore build or runtime gatekeepers in favor of downstream fixes.
- Revert unverified or ineffective changes instead of leaving them in place.
- Treat review and verification as required parts of implementation, not optional follow-up steps.


## Read/Search Scope Rules

- By default, all agents and subagents MUST restrict file reads, searches, and file discovery to paths under `/home/`.
- Preferred default scope is the current repository only; broaden to another `/home/...` path only when the task explicitly requires it.
- Agents MUST NOT read, search, or discover paths on mounted NAS, remote filesystems, external drives, or any non-`/home` path unless the user explicitly authorizes that exact broader scope in the current session.
- If a task appears to require content outside `/home/`, stop and ask before accessing it.
## Filesystem Safety Rules

- The project folder is the only writable scope. For this repository, that means files and folders under the repository root only.
- Never create, modify, delete, move, rename, export, copy, or overwrite any file or folder outside the project folder unless the user explicitly authorizes that exact path and action in the current session.
- Never use home-directory, mounted-drive, removable-drive, desktop, downloads, appdata, system temp, or other external filesystem locations as implicit defaults for writes.
- Treat all paths outside the project folder as read-only by default, even if the environment technically allows writing.
- Never generate fixtures, temp files, caches, logs, databases, exports, or test artifacts outside the project folder.
- If existing code points to writable paths outside the project folder, prefer changing it to a project-local path instead of using the external path.
- If a task requires writing outside the project folder and the user has not explicitly approved it, stop and ask.
- All agents and subagents must follow the same filesystem restrictions.
