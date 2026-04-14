## Repository Map

A full codemap is available at `codemap.md` in the project root.

Before working on any task, read `codemap.md` to understand:
- Project architecture and entry points
- Directory responsibilities and design patterns
- Data flow and integration points between modules

For deep work on a specific folder, also read that folder's `codemap.md`.

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
   - Read the relevant codemaps and code paths first.
   - Trace the actual runtime or build path instead of assuming behavior.
   - Base decisions on present code and observed evidence, not speculation.

3. Create and maintain explicit tasks.
   - Break the work into concrete steps.
   - Re-prioritize tasks when a blocker invalidates the current path.
   - Treat major gatekeeping blockers as higher priority than downstream fixes.

4. Review the plan and tasks before implementation.
   - Confirm the planned fix matches the actual pipeline.
   - Confirm the expected outcome is tied to concrete evidence.
   - If evidence is weak or indirect, continue investigation instead of editing code.

5. Verify preconditions before making code changes.
   - If the build, configure, runtime, or dependency state prevents meaningful validation, fix that blocker first.
   - Do not continue speculative feature fixes when a gating blocker prevents review and verification.

6. Implement the smallest justified change.
   - Prefer minimal, targeted edits.
   - Avoid single-line or localized fixes that are not checked against the full affected pipeline.
   - Consider all affected occurrences, call paths, and feature interactions before keeping a change.

7. Review the implementation after each fix attempt.
   - Review not only the edited lines, but each affected path and related stage in the pipeline.
   - Check whether the change impacts other features, workflows, or assumptions.
   - Verify the change still aligns with the final user-facing goal.

8. Verify results with evidence.
   - Build, run, test, or otherwise validate the change whenever the task requires verification.
   - If verification is blocked, treat that blocker as the active task.
   - Do not present unverified speculative fixes as completed work.

9. Handle fallout immediately.
   - If a fix reveals blockers, broken builds, or regressions in other features, address those before considering the work done.
   - If the change does not produce a concrete, reviewable improvement, revert it.
   - Do not leave dead, speculative, or ineffective code in the codebase.

10. Close only after end-goal review.
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

## Filesystem Safety Rules

- The project folder is the only writable scope. For this repository, that means files and folders under the repository root only.
- Never create, modify, delete, move, rename, export, copy, or overwrite any file or folder outside the project folder unless the user explicitly authorizes that exact path and action in the current session.
- Never use home-directory, mounted-drive, removable-drive, desktop, downloads, appdata, system temp, or other external filesystem locations as implicit defaults for writes.
- Treat all paths outside the project folder as read-only by default, even if the environment technically allows writing.
- Never generate fixtures, temp files, caches, logs, databases, exports, or test artifacts outside the project folder.
- If existing code points to writable paths outside the project folder, prefer changing it to a project-local path instead of using the external path.
- If a task requires writing outside the project folder and the user has not explicitly approved it, stop and ask.
- All agents and subagents must follow the same filesystem restrictions.
