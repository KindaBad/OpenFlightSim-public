# Repository instructions

## Completion and GitHub delivery

The user has given standing authorization to commit and push completed work to
GitHub. Always finish a task by committing its changes and pushing the commits to
the repository's GitHub remote before reporting completion. Do not stop with
changes only in the workspace, and do not routinely ask for push confirmation.

- Run checks appropriate to the changes before committing.
- Commit only the task's intended changes; preserve unrelated user files.
- Push the current task branch to its GitHub upstream. For this checkout, the
  remote is `origin` (`https://github.com/KindaBad/OpenFlightSim-public.git`).
- Use a normal push. Do not force-push or overwrite other people's work.
- Verify the push succeeded, and include the branch or commit in the final reply.
- If authentication, permissions, branch protection, or another genuine blocker
  prevents pushing, explain the blocker clearly; never claim the push succeeded.
- When a task changes no files, push any pending task commits. No empty commit is
  required when there is nothing to commit or push.

Keep the existing source-only repository policy: generated models, build output,
dependency caches, imported restricted reference data, and unrelated audit ZIPs
stay local unless the user explicitly requests otherwise.

## Public source delivery

This is the independent public source repository. Push completed source tasks to
its public origin normally. The original OpenFlightSim repository and its older
asset-bearing history remain private. Preserve this public repository's existing
commits; never merge, mirror-push or copy the private Git history into it.

Exclude reference/, generated models/Blender files, restricted imported data,
caches, generated test/build output and unrelated archives. See docs/PUBLIC_SOURCE.md.

## Player update delivery

The public release workflow automatically builds and publishes a new CMake
project version on `main`. For completed changes that affect the shipped game
or launcher, increment the root `project(OpenFlightSim VERSION ...)` before
committing and delivering both source snapshots. Published versions are immutable.
Documentation-only and local-only artwork work does not require a version bump.
Do not increment again merely to retry an unpublished or draft version.

Player builds currently exclude the Su-57 with `OFS_INCLUDE_SU57=OFF`; local
development retains it. Rebuild and verify the approved aircraft content pack
and refresh its CI hash/size/URL when shipping changes to its original aircraft.
Do not publish the current Su-57 model files without resolving their distribution
rights. Verify release workflow results and the public launcher manifest before
reporting that a player update is available; report genuine release blockers.

Aircraft provenance hashes cover tracked configuration inputs, including
`core/src/aircraft_definition.cpp`. After changing those inputs, rebuild the
normal four-aircraft `ofs_provenance_export` target, refresh the records with
`scripts/export_aircraft_provenance.py --binary <exporter>`, and run
`regression.provenance` before delivery. Keep source line endings governed by
`.gitattributes` so Windows checkouts preserve the same source hashes.
