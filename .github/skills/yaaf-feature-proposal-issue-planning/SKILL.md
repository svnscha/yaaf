---
name: yaaf-feature-proposal-issue-planning
description: 'Use when turning a raw feature request, product goal, enhancement idea, or user prompt into a concrete implementation plan and local task-file draft. Produces a scoped proposal, acceptance criteria, actionable task list, legend, task tracker, phased tasks, subtasks, and step-by-step execution items. Replaces open questions with explicit user decisions backed by recommended options, then asks for approval before creating `.tasks/<slug>.md` in the repository.'
argument-hint: 'What feature or goal should be turned into a local task plan?'
---

# Feature Proposal Local Task Planning

Use this skill when the user has a desired outcome but not yet a concrete delivery plan. The goal is to convert a loose feature request into an execution-ready local task file and, after explicit user approval, create `.tasks/<slug>.md` in the current repository.

Default behavior: prepare one local task file with a phased tracker under `.tasks/<slug>.md`, assume the current repository as the task-plan home, and only split the work into multiple linked task files when repository evidence shows clearly separable slices. Do not end with unresolved open questions or vague decision points. When repository evidence leaves a material choice unresolved, present the user with a concise decision prompt, a recommended default, and 2-3 meaningful options with brief tradeoffs.

## When to Use

- A user describes a feature, enhancement, workflow, or product goal in broad terms.
- The work needs to be decomposed into actionable implementation tasks rather than discussed abstractly.
- The user wants a repository-local task plan that an engineer or agent can execute without inventing the plan from scratch.
- The plan needs explicit scope, non-goals, dependencies, acceptance criteria, and progress tracking.

## Outputs

This skill should produce:

- A short feature proposal summary in plain language.
- A concrete scope statement and explicit non-goals.
- Assumptions, dependencies, and risks.
- Acceptance criteria that can be validated.
- A legend and task tracker for execution status.
- A phased task list with actionable tasks and subtasks.
- A step-by-step task file body ready to save as `.tasks/<slug>.md`.
- Explicit user decision prompts only when needed, each with a recommended option and short rationale.
- A confirmation step asking the user whether to create the local task file now.

Load the reusable local-task scaffold when drafting the final task file: [task template](./assets/task-template.md).

## Procedure

1. Parse the raw goal.
   - Extract the user outcome, intended audience, likely product surface, and implied constraints.
   - Rewrite the goal as an implementation-oriented problem statement.

2. Inspect the repository before proposing work.
   - Read the closest implementation, tests, documentation, and configuration surfaces that would own the feature.
   - Prefer the controlling abstraction, current extension points, and existing task or workflow patterns over broad exploration.
   - Identify the smallest credible implementation slices.

3. Decide the delivery shape.
   - Default to one `.tasks/<slug>.md` file with a phased tracker.
   - Split into multiple linked task files only when the work crosses clearly separable subsystems, teams, milestones, or validation strategies.
   - Keep each task file independently actionable and reviewable when a split is necessary.
   - If the best delivery shape depends on user intent rather than repository evidence, stop the split decision there and ask the user to choose from a short set of concrete options, highlighting the recommended default.

4. Build the proposal.
   - State the problem, goal, scope, and non-goals.
   - Add background only when it materially affects implementation choices.
   - Capture dependencies, rollout constraints, migration needs, and validation strategy.
   - Convert any material unknown into either a justified working assumption or a user decision prompt with suggested options.

5. Build the execution plan.
   - Organize work into phases such as discovery, implementation, tests, docs, and rollout.
   - For each phase, write tasks as concrete deliverables, not vague intentions.
   - For each task, add subtasks that describe specific engineer actions in execution order.
   - Prefer verbs like add, update, refactor, validate, document, wire, remove, and verify.

6. Add tracking metadata.
   - Include a legend for task states.
   - Include a single tracker section that summarizes progress by phase or workstream inside the main task file.
   - Mark blockers, dependencies, and follow-up work explicitly instead of burying them in prose.

7. Review the plan against the quality bar.
   - Every task should be actionable by one engineer without requiring hidden context.
   - Every acceptance criterion should be testable or otherwise directly verifiable.
   - Remove speculative tasks that are not supported by repository evidence.
   - Convert broad items like "implement feature" into smaller outcomes tied to files, components, APIs, tests, or docs.
   - Replace any lingering "open question," "TBD," or vague decision note with either a concrete assumption or a user-facing choice with recommendations.

8. Present the draft task file.
   - Show the user the proposed title, slug, and task-file body.
   - Call out assumptions that materially shape the plan.
   - If user input is still required, ask direct decision questions with meaningful suggestions instead of listing open questions.
   - Each decision prompt should include a recommended option, at least one credible alternative, and the implementation impact of each choice.
   - Ask whether to create `.tasks/<slug>.md` only after any required user decisions have been resolved.

9. Create the local task file only after approval.
   - If the user approves, create `.tasks/<slug>.md` in the current repository and write the finalized task body there.
   - If the user wants edits first, revise the draft and ask again.
   - If the file already exists, update it only after the user confirms that overwriting or replacing its content is intended.

## Planning Rules

- Do not jump straight to implementation tasks without first stating scope and non-goals.
- Do not write placeholder subtasks such as "do research" unless the unknown is real and bounded.
- Prefer fewer, sharper tasks over long noisy checklists.
- Separate required work from optional follow-up.
- Keep the default deliverable as one execution-ready task file unless a split materially improves execution.
- Mention tests, documentation, telemetry, migrations, or rollout only when they are actually relevant.
- Keep the plan grounded in the current repository and tooling rather than generic product-process language.
- When uncertainty is material, ask the user to choose from concrete, recommendation-backed options instead of ending on an open question.
- Do not leave unresolved decision markers in the final draft task file.
- If a choice can be safely assumed from repository evidence, make the assumption and state it briefly instead of escalating it.
- Prefer kebab-case slugs derived from the task title.

## Default Task Legend

- `[ ]` not started
- `[-]` in progress
- `[x]` completed
- `[!]` blocked or waiting
- `[?]` user decision required

## Completion Criteria

The skill is complete when:

- The feature goal has been translated into a concrete task title, slug, and task-file body.
- The task file contains scope, non-goals, acceptance criteria, legend, tracker, tasks, and subtasks.
- The task list is specific enough for an engineer or local workflow agent to execute without re-planning the work.
- Any required user choices have been surfaced as explicit, recommendation-backed prompts rather than open questions.
- The user has been asked whether to create `.tasks/<slug>.md` after those choices are resolved.

## Suggested Prompt Shapes

- "Turn this feature idea into an actionable local task file."
- "Break this goal into a concrete implementation plan and prepare `.tasks/<slug>.md`."
- "Propose the tasks, subtasks, and acceptance criteria for this enhancement."
- "Plan this feature and create the local task file once I approve it."
