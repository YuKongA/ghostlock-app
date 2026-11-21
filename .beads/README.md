# Beads execution and evidence

Use the project-pinned `npm run --silent bd -- …` command for tasks, dependencies, status and approval/runtime evidence.
The sole design body is in OpenSpec proposal/design/specs; `spec_id` links to its exact file/anchor. Keep `design` empty and acceptance as a reference, not another matrix.
Installation, bootstrap and commands: [development environment](../docs/development/development-environment.md#planning-tools).
Authority and original gates: [tool responsibilities](../docs/development/documentation-standards.md#planning-tool-authority).

`issues.jsonl` is CLI-generated; never edit it by hand. It is not full Dolt history or concurrent cross-machine synchronization. Commit the referenced OpenSpec files with the task projection.
