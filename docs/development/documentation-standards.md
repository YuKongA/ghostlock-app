English | [简体中文](documentation-standards_ZH.md)

# Documentation standards

> These rules cover all documentation in this repository. Follow the Google Developer Documentation Style Guide: short, direct, and written for the reader.
> Classify content with Diátaxis. Precedence: user requirements > `AGENTS.md` > this document. If documentation conflicts with code, the code takes precedence. Update the documentation with the change.

## Choose the document type first

Before writing, answer two questions: who is the reader, and what question does the document answer? Each document belongs to one category.

| Type | Content | Location |
|---|---|---|
| Tutorial / quick start | Guide the reader through one successful run | `README.md`, `docs/kernel_profiles/README.md` |
| How-to guide | Complete one task | `docs/development/*` |
| Reference | Fields, commands, and structures | `docs/kernel_profiles/PROFILE_SCHEMA.md`, `defaults.md`, `src/core/README.md` |
| Explanation | Reasons behind the design | `design-philosophy.md`, `engineering-standards.md`, `docs/analysis/**` in history |
| Plan | Before an L-level change | `docs/analysis/*-plan.md`, which may be archived after completion |
| Gate record | After device verification | `docs/analysis/device-gates/`, archived and retained permanently |
| Release notes | Each version | `docs/release-note-*.md`, archived |

Write each piece of content in one place and link to it elsewhere. Update an existing document of the same kind instead of creating a parallel version.

## Write for the reader

- Keep it short. Delete sentences that do not change the reader's actions. Prefer concrete practices to abstract descriptions.
- Be direct. Use active voice and present tense. Write "Run X", not "X can be run".
- Make claims checkable. Record numbers, commands, and log names accurately. Conclusions must stay within the evidence. Do not write "should be fine", "probably", or "looks normal".
- Make facts traceable. Cite history with `git show <commit>:<path>`. Plans and gate records must include a commit and date.
- Do not overpromise. Avoid words such as "seamless", "perfect", and "complete" as unsupported claims.
- Keep terms consistent. Do not translate `route`, `profile`, `gate`, `handoff`, `waiter`, or `payload`. Use full-width punctuation in Chinese, and backticks for code and English terms.
- Use MUST, SHOULD, and MAY in normative text, corresponding to 必须, 应当, and 可以. Do not use a commanding tone in other documents.
- Follow Google style for English: active voice, second person, present tense, and sentence case headings.
- Use relative links. Cross-document references name the file, not a section number.

## Draw diagrams

Key structures and flows must have diagrams. Prose makes readers reconstruct the topology in their heads. A diagram shows it directly, exposing dangling calls, missing cleanup paths, and conflicting boundaries.

- Diagrams are required for L-level changes that add or modify a route, a cross-layer contract between Kotlin and native, session and resource ownership, the attack state machine, or state and cleanup or rollback boundaries.
- Use class or component diagrams for structure, including classes and ownership. Use flowcharts or sequence diagrams for control flow, state diagrams for states and exits, and flowcharts for data flow.
- Use Mermaid. Its text can be versioned, reviewed, diffed, and updated with code. Update diagrams in the same batch as the code.
- Give each diagram one concern, following the view/viewpoint model of ISO/IEC/IEEE 42010. Diagrams show topology. Prose gives reasons and exceptions. Do not duplicate one in the other.
- Keep one authoritative diagram per structure. Other documents link to it.

## Bilingual documents

Every reader document throughout the repository must have an English `.md` version and a matching Chinese `_ZH.md` version. This includes `README.md`, `docs/**`, and `src/core/README.md`. Update both languages in the same batch. Do not let either version drift.

Add language-switch links at the top of each version. Chinese prose links to the `_ZH.md` versions of current reader documents. English prose links to the English `.md` versions. Keep source-code paths and historical `git show` paths unchanged. Write code comments and commit messages in English.

## Templates

**Plan, required for L-level changes**

```markdown
# <topic> plan (<YYYY-MM-DD>)
## Current state and baseline  # Branch, commit, current behavior, known issues
## Goals and constraints       # List non-goals explicitly
## Change list                 # Per file: file → change → reason
## Data-flow/control-flow differences  # Before and after; invariants
## Compatibility and rollback
## Verification matrix         # Commands and expected results for each batch
## Explicit preservation       # Parts left unchanged and why
## Progress                    # [ ]/[x] for each item, updated on completion
```

**Gate record, required for device verification**

```markdown
# <id> device gate: <topic> (<route>), PASS/FAIL
Commit `<hash>` (`<commit message>`)
## Device and entry point  # Model, kernel uname -r, entry point, uid/selinux/seccomp, home
## Result                  # route_done count and status/clean/step/errno, child is root, handoff, whether a panic occurred
## Logs                    # Filenames matching the device's Download/ghostlock-debug-log/<time>/ directory
## Change description      # Behavioral differences covered by this verification
```

Keep the S/CPP/U01/NS*/PROFILE-* ID prefixes. Archive failures on the same terms as passes. Failure records state the impact, including panic, reboot, and duration, the root cause, and action items. Use postmortem fields without assigning blame.

## Workflow

- Update documentation in the same batch as code. Use `docs:` commits for documentation-only changes.
- Changes to commands, fields, or workflows must be reflected in the documentation.
- Deleting a document means archiving it in git history with `git rm`, without rewriting history. Update the historical index in `AGENTS.md`.
- If behavior has changed, add a "Current-state note (date)" or archive the document as described above.

## Checklist

- [ ] The document type and intended reader are clear
- [ ] No vague conclusions; facts are traceable and numbers are checkable
- [ ] Template fields are complete, including commit, date, and log names
- [ ] Both languages agree; relative links work
- [ ] Deletions and archival are reflected in `AGENTS.md`

---

Sources: [Google Developer Documentation Style Guide](https://developers.google.com/style),
[Diátaxis](https://diataxis.fr/), OASIS DITA, Carroll Minimalism, ISO/IEC/IEEE 42010, and RFC 2119.
