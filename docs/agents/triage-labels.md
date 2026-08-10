# Triage Labels

The skills speak in terms of five canonical triage roles. This file maps those roles to the `Status:` values used in this repo's scratch-file issue tracker (`.scratch/`).

| Label in mattpocock/skills | `Status:` value in our tracker | Meaning                                  |
| -------------------------- | ------------------------------ | ---------------------------------------- |
| `needs-triage`             | `needs-triage`                 | Maintainer needs to evaluate this issue  |
| `needs-info`               | `needs-info`                   | Waiting on reporter for more information |
| `ready-for-agent`          | `ready-for-agent`              | Fully specified, ready for an AFK agent  |
| `ready-for-human`          | `ready-for-human`              | Requires human implementation            |
| `wontfix`                  | `wontfix`                      | Will not be actioned                     |

When a skill mentions a role, write the corresponding `Status:` value from this table near the top of the issue file (see `issue-tracker.md`).

Issues open after a triage pass default to `Status: needs-triage` unless the workflow says otherwise.