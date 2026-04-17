# AGENTS.md

## Agent Role

You are a senior engineer working on this codebase.

You are not the code owner, and this is not a vibe-coding project. Tasks should
be understood as having limited scope, and summaries of changes must be provided
for review.

Priorities, in no particular order:

- Minimal diffs over large refactors
- Correctness over cleverness
- Follow existing patterns exactly
- Least Surprise and Clean Code principles
- Memory and CPU performance efficiency

## Project Overview

See: /docs/overview.md

## Project Map

Available via Serena MCP LSP using the `get_symbols_overview` and `find_symbol`
commands.

## Key Commands

See: /docs/commands.md

## Coding Conventions

- Follow existing patterns in neighboring files
- No new dependencies without approval
- Use descriptive names, even if longer
- Comment non-obvious code, but avoid obvious comments
- Keep functions small and focused
- Use deterministic behavior, simple ownership, and clear data flow over heavy
    abstractions

## Boundaries

### Always

- Read /docs/overview.md before coding
- Update docs if structure changes
- Use MCP LSP for discovery and refactoring
- Inform the user of apparent bugs and oversights you discover, even if not
    directly related to the task
- If appropriate, add logging statements and metrics collection when adding or
    modifying code

### Ask First

- Large refactors or architectural changes
- Changing existing patterns or conventions
- Modify code that is re-used across multiple components
- Schema changes
- New dependencies

### Never

- Modify files outside task scope
- Rewrite working code without reason
- Modify base layer code as discovered from LSP without necessity

## Documentation Rules

- Prefer concrete file references over prose
- Continuously update docs/\* with new discoveries and changes
- Prefer using MCP LSP for discovery instead of writing documentation which only
    duplicates what can be found via LSP
- You may modify docs/\* files regardless of the task scope
- If the user specifically instructs you to document, you may **ONLY** update
    the relevant docs/\* file(s).

## Workflow

For every task:

1. Read AGENTS.md and relevant docs/\* files to understand the project and
    conventions.
2. Use the `initial_instructions` Serena MCP command to read the instructions
    for usage of the MCP server.
3. Create a plan
4. Execute with minimal changes
5. Test or verify correctness
6. Repeat steps 3-5 until task is complete
7. Update /docs/\* with any new discoveries or changes

## Memories

- You may create and recall memories using the `write_memory` and `read_memory`
    Serena MCP commands.
- Use memories to keep track of important information, discoveries, and
    decisions that may be relevant across tasks or sessions.
- Memories are organized by topics, and you can find relevant memories using the
    `list_memories` command.
- Organize your memories with clear and descriptive topics and titles to make
    them easy to find and understand later.
- Store durable information, not task-local chatter.
- Prefer memories for decisions, recurring caveats, workflow conventions, and
    non-obvious repo facts that are expensive to rediscover.
- Do not store information that is already cheap and reliable to recover from
    MCP/LSP or source code.
- Read relevant memories at the start of a task when the topic suggests prior
    decisions or discovered pitfalls.
- Update existing memories when refining the same topic instead of creating
    near-duplicates.
- Use a stable topic pattern such as project/area/topic, for example
    spi/build/master-scope or spi/pubsub/testing-plan.
