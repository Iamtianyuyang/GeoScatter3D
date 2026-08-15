# Similar Project Hunt

Use this reference when the user is building a higher-level feature with Dear ImGui and the official demo or examples may not cover the full interaction pattern.

## When to search outside the official repo

- The task is a full feature or workflow, not a single API question.
- The requested UI sounds product-like: inspector, asset browser, log viewer, timeline, graph editor, launcher, profiler, debugger, scene hierarchy, property grid, or settings panel.
- The official demo shows only isolated widgets, but the user needs a full working composition.

## Search order

1. Search for open-source Dear ImGui applications or extensions that name the feature directly.
2. Search for the closest interaction pattern if the exact domain is rare.
3. Fall back to official Dear ImGui demo and example code when no strong external match exists.

## Search patterns

- GitHub repository search: `"Dear ImGui" "<feature>"`
- GitHub repository search: `"imgui" "<feature>"`
- Search engine with GitHub bias: `site:github.com "Dear ImGui" "<feature>"`
- Search by interaction model, not only domain terms: `timeline`, `node editor`, `property grid`, `asset browser`, `console`, `log viewer`, `profiler`

## What makes a reference worth using

- The repo clearly shows the feature in screenshots, docs, or example media.
- The implementation files are easy to identify.
- The codebase is maintained enough that the patterns are understandable.
- The feature looks complete enough to demonstrate state flow, input handling, and layout decisions.
- The license and context make it safe to reference.
- The platform and renderer pairing is close to the user's stack when that matters.

## What to extract from an external project

- Repository URL and, when possible, exact file paths or commit.
- How the feature is broken into windows, panels, tabs, or docked regions.
- Which parts use stock widgets versus custom draw list code.
- How selection state, filtering, undo/redo, drag-and-drop, or background jobs are modeled.
- Which backend assumptions or helper libraries the project depends on.

## Verification rules

- External repositories are reference implementations only.
- Confirm every Dear ImGui API, flag, backend call, and frame lifecycle detail against the current local or upstream Dear ImGui source.
- If the external code uses deprecated or version-specific calls, translate them to the current API before recommending them.
- State explicitly when a conclusion is inferred from a similar project rather than guaranteed by official Dear ImGui docs.
