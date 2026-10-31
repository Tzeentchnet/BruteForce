# Agent instructions

## Project documentation

Keep these documents current when related work changes the evidence or
status. Update them in the same change as the work, not afterward:

- `docs/NEXTSTEPS.md`: active workstreams, ordering, settled decisions, and
  completion gates.
- `docs/UPSTREAM.md`: selected and filed upstream work, contribution
  candidates, overlap, tests, and remaining follow-up.
- `docs/FINDINGS.md`: Brute Force-specific executable, runtime, and bring-up
  findings, including measured performance results.
- `docs/ARCHITECTURE.md`: game and resource organization, asset-format
  evidence, and the future Blender-importer investigation.
- `docs/RENDERING.md`: renderer ownership and synchronization boundaries,
  CPU/GPU reference behavior, presentation, scaling, and widescreen design.
- `docs/PERFPLAN.md`: the performance plan, its gates, and phase status. Mark
  a phase's progress there, and record its measurements in FINDINGS.
- `docs/USER_GUIDE.md`: detailed user-facing build, launcher, runtime,
  diagnostic, and benchmark instructions.
- `docs/DEVELOPMENT.md`: synthetic test and developer validation commands.

Distinguish observations from hypotheses and unverified behavior; identify the
game revision, evidence, and test coverage when relevant. Update `README.md`
when user-facing build instructions or status change. Keep relative links
valid when documentation moves.

## Framework changes and upstream candidates

The aim is to improve xboxrecomp itself, so that later projects do not hit
the same problems.

- **Separate generic from title-specific.** Code that depends on Brute Force
  addresses, guest structures, or observed behaviour belongs in `src/`. A
  framework fix goes in a numbered patch under `patches/xboxrecomp/`, applied
  by `tools/toolkit.py`; update its expected tree. Write framework patches as
  if for upstream:
  - put behaviour changes behind an environment switch, off by default
    unless the change is a plain bug fix;
  - name variables `RECOMP_*`, not after this title;
  - cover the change with a synthetic fixture.
- **Record every framework change in `docs/UPSTREAM.md`.** Any new or changed
  framework patch, and any framework-level cause found while debugging the
  title, gets or updates an entry in the format described there: problem,
  evidence, change, tests, overlap, before filing, status. This applies even
  when the local fix is only a title-side workaround: record the framework
  gap that made it necessary.
- **Check upstream before writing a fix.** Search open and merged PRs (for
  example,
  `gh pr list -R sp00nznet/xboxrecomp --state all --limit 500`) and the
  tracking index #184. Prefer selecting or improving an existing PR over a
  competing one, and record the overlap and the date checked.
- **Credit prior art.** When adapting another project's code, such as Blood
  Wake's, keep its licence notice, add it to `THIRD_PARTY_NOTICES.md`, and
  name the source in the UPSTREAM entry. Copy only generic code, never its
  title-specific hooks, addresses, or assets.
- **Never interact upstream without approval.** Do not file issues or pull
  requests, push amendments to filed PRs, or comment upstream without the
  user's explicit approval for that action.
- **Record filed work immediately.** After an approved submission, record its
  PR number, commit, validation, and status in `docs/UPSTREAM.md`; update
  `docs/NEXTSTEPS.md` and `README.md` when their status or ordering changes.

## Validation

- Use the smallest relevant checks described in
  [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md#tests).
- After changing a framework patch or selected upstream commit, run its
  focused synthetic fixture and `python tools/toolkit.py --verify`; update
  the expected toolkit tree in the same change.
- For documentation-only changes, run `git diff --check` and verify affected
  relative links.

## Performance work

Follow `docs/PERFPLAN.md`.

- **Report distinct frames.** Report distinct world frames, not flip counts
  alone. Label flip figures as flips.
- **Compare A/B/A.** Run alternating comparisons of at least three runs on
  the same generation, scene, and scripted input. Record the generation,
  thread count, enabled options, and run count.
- **Keep the CPU reference path.** The CPU rasterizer stays working as the
  correctness reference. A GPU-path change is not done until it matches the
  CPU path at matched frames, or its differences are listed.
- **Don't skip synchronization for speed.** Do not skip or prematurely
  satisfy the PFB flush handshake, semaphore releases, or flip holds to gain
  frame rate. See `docs/FINDINGS.md` for why.

## Local data

Keep disc images, extracted assets, generated game code, screenshots, runtime
captures, profiles, and proprietary payloads local. Document format
signatures and structural observations without copying game content into the
project or upstream reports.

Direct3D 12 work is deferred unless the user changes that direction. Direct3D
11 is the GPU target.
