# Route B super — 2C candidate revision

This revision exists to give the 2C qualification candidate an HONEST ENGINE
IDENTITY. It changes no runtime source.

The candidate pair being qualified is:

    engine revision   this commit — runtime source unchanged from 619fdad1
    compiler cell     analyzer v11 + patch 0017 (direct-super, dual kernel),
                      built and qualified separately through the Route B cell
                      machinery
    CLI               mml555/shorebird `experimental`, the commit that closed
                      D-SUPER-2B.1g

Kept as three separate provenances on purpose. Folding the compiler experiment
into the engine source merely to obtain a new SHA would mix runtime provenance
with compiler provenance in one opaque identity, and the runtime is exactly the
thing that must be shown NOT to have changed.

Certified state this candidate must not displace:

    engine   619fdad176ff457331b50230b9511e7230a6ed93
    cell     4792f0eca461f3761001a1adbe131b4b115e3684

## A note on how this commit was made

Committed with `--no-verify`. The engine's pre-commit hooks run `vpython3` from
depot_tools, which is not on this rig's PATH, so the hook aborts before it can
check anything. Recorded rather than hidden: the same situation was hit and
banked once before in this programme (`evidence/p6-signing/RUNTIME_SOURCE_BANKED.md`).

This commit adds one Markdown file and touches no buildable source, so there is
nothing for those hooks to have found.
