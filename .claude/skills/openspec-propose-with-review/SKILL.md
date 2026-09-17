---
name: openspec-propose-with-review
description: Customized variant of openspec-propose. After creating proposal/design/tasks/spec artifacts, auto-triggers a parallel Claude + Codex review and surfaces issues and new open questions before declaring the change apply-ready. Use when the user wants to propose a change AND wants the artifacts audited automatically before implementation.
allowed-tools: Bash(openspec:*)
license: MIT
compatibility: Requires openspec CLI. Codex review requires the `codex` Claude Code plugin (openai-codex marketplace), with the Codex CLI configured via `/codex:setup`.
metadata:
  author: xliu
  version: "1.2"
  basedOn: "openspec-propose 1.0 (generatedBy 1.10.0)"
---

Propose a new change - create the change and generate all artifacts in one step, then auto-review.

**Planning boundary**: This workflow creates planning artifacts only. The user request that selected or triggered this workflow authorizes planning only, even if it asks to build or fix something. Do not edit project code. After the planning artifacts are complete, stop. Do not start implementation in the same response, even if the initial request asks for it. Wait for a new user request after the artifacts are presented; then start the apply workflow.

I'll create a change with the artifacts your schema defines. With the default spec-driven schema that is:
- proposal.md (what & why)
- `specs/<capability-path>/spec.md` (what the system must do - a delta, not the main spec)
- design.md (how)
- tasks.md (implementation steps)

`<capability-path>` is the spec directory relative to `specs/` (for example, `user-auth` or `identity/user-auth`). Preserve an existing capability's full path and follow the project's established organization for new capabilities.

Then I'll run a parallel Claude + Codex review and surface findings before declaring the change apply-ready.

When the user is ready to implement, they must start the apply workflow explicitly — run `/opsx:apply-with-review` (which will also auto-review the diff after implementation).

---

**Store selection:** If the user names a store (a store is a standalone OpenSpec repo registered on this machine) or the work lives in one, run `openspec store list --json` to discover registered store ids, then pass `--store <id>` on the commands that read or write specs and changes (`new change`, `status`, `instructions`, `list`, `show`, `validate`, `archive`, `doctor`, `context`, `schemas`, `view`). Once selected, treat `--store <id>` as sticky for the rest of the workflow. Every unscoped example of those commands below is shorthand: before running it, append the flag. For example, run `openspec status --change "<name>" --json --store "<id>"`, not the unscoped form shown below. Other commands do not take the flag. Hints printed by commands already carry the flag; keep it on follow-ups. Without a store, commands act on the nearest local `openspec/` root.

**Input**: The user's request should include a change name (kebab-case) OR a description of what they want to build.

**Steps**

1. **Understand the request and clarify material ambiguity**

   If no clear input is provided, ask the user (open-ended, no preset options):
   > "What change do you want to work on? Describe what you want to build or fix."

   From their description, derive a kebab-case name (e.g., "add user authentication" → `add-user-auth`).

   **IMPORTANT**: Do NOT proceed without understanding what the user wants to build.

   If the request contains ambiguity that would materially affect scope, externally observable behavior, compatibility, or acceptance criteria, ask the user before creating the change. For minor details, make a reasonable assumption and record it in the planning artifacts.

2. **Determine the workflow schema**

   Use the configured default schema unless the user explicitly requests a different workflow.

   **Use a different schema only if the user:**
   - Explicitly requests a specific schema by name → use `--schema <schema-name>`
   - Asks to "show workflows" or asks "what workflows" exist → resolve the authoritative root by running `openspec context --json` from the current working directory. If the user explicitly selected a registered store, use `openspec context --json --store "<store-id>"`. Then run `openspec schemas --json` with its working directory set to the returned `root.path` and let them choose. This preserves roots selected by a local `store:` pointer or the global `defaultStore`; when a registered store was explicitly selected, append `--store "<store-id>"` to `openspec schemas --json` as well. If context reports only `no_openspec_root`, run `openspec schemas --json` from the current working directory instead. Do not use this fallback for invalid or unavailable stores.

   Otherwise, omit `--schema` to preserve the configured default.

3. **Create the change directory**

   Choose one schema form below. If a registered store is selected, append `--store "<store-id>"` to that command and each later OpenSpec command shown below that accepts `--store`.

   Using the configured default:
   ```bash
   openspec new change "<name>"
   ```

   Using an explicitly requested schema:
   ```bash
   openspec new change "<name>" --schema "<schema-name>"
   ```
   This creates a scaffolded change in the planning home resolved by the CLI with `.openspec.yaml`.

4. **Get the artifact build order**
   ```bash
   openspec status --change "<name>" --json
   ```
   Parse the JSON to get:
   - `applyRequires`: array of artifact IDs needed before implementation (e.g., `["tasks"]`)
   - `artifacts`: list of all artifacts, each with its `status` and its `requires` edges (the artifact IDs it directly depends on)
   - `planningHome`, `changeRoot`, `artifactPaths`, and `actionContext`: path and scope context. Use these instead of assuming repo-local paths.

5. **Create every artifact in the required set**

   Use a todo list to track progress through the artifacts.

   Loop through artifacts in dependency order (artifacts with no pending dependencies first):

   a. **For each artifact that is `ready` (dependencies satisfied)**:
      - Get instructions:
        ```bash
        openspec instructions <artifact-id> --change "<name>" --json
        ```
      - The instructions JSON includes:
        - `context`: Project background (constraints for you - do NOT include in output)
        - `rules`: Artifact-specific rules (constraints for you - do NOT include in output)
        - `template`: The structure to use for your output file
        - `instruction`: Schema-specific guidance for this artifact type
        - `skipped`/`warning`: present when the change declares skip_specs and this artifact must NOT be created - stop and pick another artifact
        - `resolvedOutputPath`: Resolved path or pattern to write the artifact
        - `dependencies`: Completed artifacts to read for context
      - Read any completed dependency files for context - always re-read them from disk, even if you saw them earlier in the conversation (the user may have edited them)
      - If the `instruction` field delegates creation to a specific skill or command, invoke it to produce the artifact instead of writing the file yourself, then verify the artifact file exists at `resolvedOutputPath`
      - Otherwise create the artifact file using `template` as the structure and write it to `resolvedOutputPath`. If `resolvedOutputPath` is a glob, follow `instruction` to choose the concrete file path
      - Apply `context` and `rules` as constraints - but do NOT copy them into the file
      - Show brief progress: "Created <artifact-id>"

   b. **Continue until every artifact in the required set exists (not just `apply.requires`)**
      - After creating each artifact, re-run `openspec status --change "<name>" --json`
      - The required set is `applyRequires` plus every artifact reachable from those by following the `requires` edges in `status --json` - walk them transitively (spec-driven closes over proposal, specs, design, tasks). Leave artifacts outside that set alone
      - `status` is file-existence only, so an `applyRequires` artifact reading `done` does NOT mean its dependencies exist - writing `tasks.md` early marks `tasks` done while `specs` was never written. Use each artifact's `requires` edges, not its `status`, to build the required set: a `done` artifact still lists what it depends on
      - An artifact already reading `status: "skipped"` is satisfied: the change declares `skip_specs` in `.openspec.yaml`, so its files must NOT exist. Never try to create one
      - Create every artifact in the required set that is missing, then re-check - creating one can unblock others
      - Skip one only when `status` already reports it `skipped`, or when its own `instruction` says it is conditional: run `openspec instructions <artifact-id> --change "<name>" --json` and skip only if its `instruction` field marks it optional (e.g. "create only if..."). Spec-driven's `design.md` qualifies; `specs` qualifies only via the `skipped` status above, never by your own judgment. Tell the user, and do not reconsider it
      - Dependencies are enablers, not gates: if a required artifact is still `blocked` only because you skipped a conditional dependency, write it anyway
      - Stop when every artifact in the required set is `done`, `skipped`, or was deliberately skipped

   c. **If an artifact requires user input** (unclear context):
      - Ask the user to clarify
      - Then continue with creation

6. **Show final status**
   ```bash
   openspec status --change "<name>"
   ```

7. **Review the artifacts (REQUIRED — auto-triggered)**

   Before declaring the change apply-ready, run two independent reviews **in parallel** (issue both tool calls in the same turn) and surface their findings.

   a. **Claude review** — Launch the **Agent tool** with `subagent_type: "general-purpose"` and the following prompt (substitute `<name>` and the actual artifact paths):

      > You are reviewing OpenSpec artifacts that were just generated for change `<name>`. Do NOT write code or modify artifacts — this is a read-only audit.
      >
      > 1. Read every artifact under `openspec/changes/<name>/` in full: `proposal.md`, `design.md`, `tasks.md`, and every file under `openspec/changes/<name>/specs/`.
      > 2. Read the code paths referenced by the artifacts. Verify the "current behavior" claims match what the code actually does. Use Grep/Read liberally.
      > 3. Cross-check against `AOI_GLOBAL_CONTEXT.md` and the affected repos' `CONTEXT.md` for architectural conflicts: dependency direction violations, plugin C-ABI changes, bypassing `aoi_pcb_server` or `DataStore`, cross-repo impact not acknowledged.
      > 4. Cross-check spec deltas against existing `openspec/specs/<capability>/spec.md` for contradictions, silent overrides, or duplication.
      >
      > Report in this exact structure:
      > - **Issues** — grouped by category (Contradiction / Gap / Vague requirement / Cross-repo impact / Scope risk). Each entry: one-line summary + file:line citation + why it matters.
      > - **New open questions** — things the artifacts don't pin down that must be answered before `/opsx:apply-with-review`. Each question should note what you checked before raising it.
      > - **Verdict** — `apply-ready` or `address-first`, with one sentence why.
      >
      > Be concise. Only flag real problems. Cap the report at ~300 words.

   b. **Codex review** — Run via the **Bash tool** through the codex plugin's companion runtime (the same engine behind `/codex:adversarial-review`; the slash command itself is user-only, so call the runtime directly — adversarial mode is used because it accepts focus text, which native `review` does not). The artifacts are new/modified files in the working tree, so a working-tree review covers them.

      **Preflight (cheap, ~2 s)** — before launching the long-running review, verify the runtime is usable:

      ```bash
      COMPANION=$(ls "$HOME/.claude/plugins/cache/openai-codex/codex"/*/scripts/codex-companion.mjs 2>/dev/null | sort -V | tail -1)
      [ -n "$COMPANION" ] && node "$COMPANION" setup --json || echo "codex-unavailable"
      ```

      Proceed to the review only if the JSON reports `"ready": true` and `auth.loggedIn: true`. If the companion script is missing, or `ready`/`loggedIn` is false (plugin not installed, Codex CLI absent, or no Codex login/plan on this machine), skip the codex half immediately — do NOT launch the review below or burn its timeout — and report the gap per the guardrail.

      **Review** — set a generous timeout (600000 ms), Codex reviews can take minutes. Substitute `<name>`:

      ```bash
      COMPANION=$(ls "$HOME/.claude/plugins/cache/openai-codex/codex"/*/scripts/codex-companion.mjs 2>/dev/null | sort -V | tail -1)
      node "$COMPANION" adversarial-review --wait --scope working-tree "Review only the OpenSpec artifacts under openspec/changes/<name>/ (proposal.md, design.md, tasks.md, specs/). Check for contradictions, vague requirements, missing edge cases, unsupported assumptions, and scope risks. Report **Issues** and **New open questions** separately. Be concise. Only flag real problems."
      ```

      Treat the stdout as the codex review verbatim — do not paraphrase it before merging in step (c). If the run fails despite a passing preflight, report this and skip the codex half — do NOT silently drop the review.

   c. **Surface findings** — Merge both reviews into one report for the user:
      - **Issues found** — deduplicated, grouped by category, with reviewer attribution `(claude)` / `(codex)` / `(both)`.
      - **New open questions** — with reviewer attribution and what each reviewer checked.
      - **Verdict** — `apply-ready` (no blocking issues) or `address-first` (blockers exist).

   d. **Offer to update artifacts** — Propose concrete edits (e.g., "Add Q3 to `design.md` open questions section?", "Tighten requirement R7 in `proposal.md`?", "Add task for cross-repo impact in `tasks.md`?"). Do NOT auto-edit. The user decides what to capture.

**Output**

After completing all artifacts and the review, summarize:
- Change name and location
- List of artifacts created with brief descriptions, plus any conditional artifact you skipped and why
- Review outcome:
  - If verdict is `apply-ready`: "All artifacts created and reviewed — no blocking issues. When you are ready, run `/opsx:apply-with-review` to start implementing."
  - If verdict is `address-first`: Show issues and open questions, then prompt: "Address these before `/opsx:apply-with-review`, or accept and proceed."

**Artifact Creation Guidelines**

- Follow the `instruction` field from `openspec instructions` for each artifact type - it is the authoritative guidance, even for familiar artifact names
- If the `instruction` field directs you to use a specific skill or command to create the artifact, invoke it instead of writing the artifact directly
- The schema defines what each artifact should contain - follow it
- Read dependency artifacts for context before creating new ones
- Use `template` as the structure for your output file - fill in its sections
- **IMPORTANT**: `context` and `rules` are constraints for YOU, not content for the file
  - Do NOT copy `<context>`, `<rules>`, `<project_context>` blocks into the artifact
  - These guide what you write, but should never appear in the output

**Guardrails**
- The request that invoked this workflow authorizes planning only. Any implementation or apply instruction in that request does not carry forward. Do NOT implement the change, start the apply workflow, or edit project code during this workflow. After presenting the artifacts and the review findings, stop and wait for a new user request to start the apply workflow
- Create every artifact the apply phase transitively depends on, not just the ids listed in `apply.requires`
- Always read dependency artifacts before creating a new one - re-read from disk, not from conversation memory (files may have changed since you last saw them)
- Ask about ambiguities that would materially change scope, externally observable behavior, compatibility, or acceptance criteria; for minor details, make reasonable assumptions and record them
- If a change with that name already exists, ask if user wants to continue it or create a new one
- Verify each artifact file exists after writing before proceeding to next
- Run Step 7 review every time, no skipping. If the codex preflight fails (companion missing, CLI absent, or not logged in) or the run itself fails, run Claude review alone and report the codex gap.
