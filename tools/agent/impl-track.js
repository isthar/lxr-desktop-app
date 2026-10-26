export const meta = {
  name: 'lichomat-impl-track',
  description: 'Execute LICHOMAT stage 1 plan tasks in order: implement (TDD), spec and quality reviews (in turn, in parallel or one light review), fix loops',
  phases: [
    { title: 'Implement', detail: 'fresh implementer per task, full task text from the plan, check.sh, commit' },
    { title: 'Spec review', detail: 'independent check that the commit does exactly what the task and spec ask' },
    { title: 'Quality review', detail: 'code review of the task range after spec compliance passes' },
    { title: 'Final review', detail: 'whole-track review (when requested)' },
  ],
}

// Workflow script for Claude Code: executes plan tasks in order with an implementer, a spec
// review and a quality review per task. Run it with the Workflow tool (scriptPath) and args below.
// Copyright (C) 2026 Tomasz Krzywicki
// SPDX-License-Identifier: GPL-3.0-only
//
// Licensed under the GNU General Public License version 3, with the additional
// permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
// the repository.
//
// args: {
//   repo: '/Users/tk/Code/lxr' (or a worktree path),
//   branch: 'timing-core',
//   plan: 'docs/superpowers/plans/2026-10-23-timing-core.md',
//   context: 'scene-setting text shared by all tasks of this track',
//   tasks: [{ id, title, text | file, review? }],  // full task text or a file holding it; review: 'full' (default), 'parallel' or 'light'
//   final: { review: true, attacks: [{ key, prompt }] }   // optional
// }

const A = args
const MAX_ROUNDS = 3

const RULES = `Working rules (all mandatory):
- Work only in ${A.repo} on branch ${A.branch}. Never push, never switch branches, never touch main, never change git config, never run destructive git commands (reset --hard, checkout -- ., clean, rebase, stash drop, branch -D), and never rewrite a commit (no commit --amend): fix with a new commit.
- Add files explicitly (git add <path> ...); never git add -A or git add . ; commit only the files of your task.
- Every commit message ends with this trailer line: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Do not edit docs/lichomat/tracker.json or docs/lichomat/reports/, and do not run tools/tracker_note.py: the controller keeps the tracker. Where the plan tells you to log a commit in the tracker (its convention 7), skip that step and say so in your report.
- Toolchain: "source tools/env.sh" puts arm-none-eabi-gcc, cmake and ninja on PATH. "tools/check.sh" runs the host tests, the firmware release build, prints the memory summary and the warning count. It must pass with 0 warnings after your work.
- Authoritative documents: docs/superpowers/specs/2026-10-23-timing-core-design.md (spec), docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (notes N1-N21; they win over the spec), the plan ${A.plan}.
- Line endings: files in Core/ use CRLF, the others LF. After you edit a file in Core/, run perl -pi -e 's/\\r?\\n/\\r\\n/' <file> and check before committing that the file's count of CRLF equals its count of LF bytes (python3: d.count(b'\\r\\n') == d.count(b'\\n')); the file command reads only the start of a file.
- Hardware checks (H1-H13) cannot be run here; never claim them.`

const IMPL = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['DONE', 'DONE_WITH_CONCERNS', 'BLOCKED', 'NEEDS_CONTEXT'] },
    base_sha: { type: 'string', description: 'git rev-parse HEAD before you changed anything' },
    head_sha: { type: 'string', description: 'git rev-parse HEAD after your last commit' },
    summary: { type: 'string' },
    tests: { type: 'string', description: 'which tests ran and their result, including the expected failure before implementing' },
    check: { type: 'string', description: 'tools/check.sh result: the memory line and the warning count' },
    files: { type: 'array', items: { type: 'string' } },
    deviations: { type: 'array', items: { type: 'string' }, description: 'every place where you departed from the plan text and why' },
    concerns: { type: 'array', items: { type: 'string' } },
  },
  required: ['status', 'base_sha', 'head_sha', 'summary', 'tests', 'check', 'files'],
}

const ISSUES = {
  type: 'array',
  items: {
    type: 'object',
    properties: {
      severity: { type: 'string', enum: ['critical', 'important', 'minor'] },
      file: { type: 'string' },
      line: { type: 'string' },
      problem: { type: 'string' },
      fix: { type: 'string' },
    },
    required: ['severity', 'problem', 'fix'],
  },
}

const SPEC = {
  type: 'object',
  properties: {
    compliant: { type: 'boolean' },
    issues: ISSUES,
    notes: { type: 'string' },
  },
  required: ['compliant', 'issues'],
}

const QUALITY = {
  type: 'object',
  properties: {
    ready: { type: 'string', enum: ['yes', 'no', 'with_fixes'] },
    strengths: { type: 'string' },
    issues: ISSUES,
  },
  required: ['ready', 'issues'],
}

function implementPrompt(t) {
  return `You are implementing ${t.id}: ${t.title}, one task of the LICHOMAT stage 1 plan.

## Task description (full text from the plan)

${taskText(t)}

## Context

${A.context}

${RULES}

## Your job

1. Record git rev-parse HEAD (base_sha). Check that the working tree has no uncommitted changes to files of this task.
2. Implement exactly what the task specifies, following its steps in order. TDD where the task says so: write the test, run it and see it fail for the expected reason, implement, run it and see it pass.
3. If the plan's code does not compile, or a test fails for a reason the plan did not intend, make the smallest correct fix that keeps the task's intent and the spec/notes, and list it in deviations. Do not redesign.
4. Run tools/check.sh. It must pass with 0 warnings.
5. Tick the checkboxes of this task in ${A.plan} (- [ ] -> - [x]) and include the plan file in the task's commit.
6. Commit as the task's commit step says (explicit file list, trailer).
7. Self-review with fresh eyes: completeness against the task, names, no overbuilding, tests that verify behaviour. Fix what you find (new commit).
8. Report back.

Escalate instead of guessing: return BLOCKED or NEEDS_CONTEXT with specifics when the task needs a design decision the plan and spec do not settle, or when you cannot make it work. Bad work is worse than no work.`
}

function fixPrompt(t, impl, issues, kind) {
  return `You are fixing ${kind} review findings for ${t.id}: ${t.title} of the LICHOMAT stage 1 plan.

## Task description (full text from the plan)

${taskText(t)}

## Context

${A.context}

${RULES}

## Current state

Implementation range: ${impl.base_sha}..HEAD (inspect with git log and git diff).

## Findings to fix (JSON)

${JSON.stringify(issues, null, 1)}

## Your job

For each finding: verify it against the code, the task text and the spec/notes. Fix every real one with the smallest correct change; for a finding that is wrong, say why in concerns instead of changing code. Run tools/check.sh (must pass, 0 warnings), commit with an explicit file list and the trailer, and report. base_sha in your report is HEAD before your fix.`
}

function specReviewPrompt(t, impl) {
  return `You are reviewing whether an implementation matches its specification: ${t.id}: ${t.title} of the LICHOMAT stage 1 plan.

## What was requested (full task text)

${taskText(t)}

## Context

${A.context}

Authoritative documents: docs/superpowers/specs/2026-10-23-timing-core-design.md, docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (notes win over the spec), the plan ${A.plan}.

## What the implementer claims (do not trust it)

${JSON.stringify(impl, null, 1)}

## Your job

Read the actual code in ${A.repo}: git diff ${impl.base_sha}..HEAD and the files around it. Verify independently, line by line against the task and the spec/notes:
- missing requirements, or claims that are not implemented;
- extra work that was not requested;
- misunderstandings: right feature built the wrong way, wrong semantics, wrong fw 1.7 behaviour preserved or changed;
- tests that do not test what their name claims, or would pass on a broken implementation.
Run the tests yourself (tools/check.sh or the host ctest) if useful. Do not edit files and do not commit.
Return compliant=true only if nothing is missing or wrong. Each issue needs file, line, the problem and the fix.`
}

function qualityReviewPrompt(t, impl) {
  return `You are a senior embedded C reviewer. Review completed work against its plan and code quality standards before it cascades into more work.

## What was implemented

${t.id}: ${t.title}. Implementer summary: ${impl.summary}

## Requirements / plan

${taskText(t)}

## Context

${A.context}

## Git range to review

Repository ${A.repo}: git diff --stat ${impl.base_sha}..HEAD and git diff ${impl.base_sha}..HEAD

## What to check

- Plan alignment; deviations justified or not.
- Correctness: logic bugs, integer/float edge cases, overflow, ordering, ISR/main-context safety (volatile, atomicity, priorities), real-time cost on a Cortex-M7 (no heap, no unbounded loops in IRQ context), RAM/ITCM budget.
- Tests: verify real behaviour, cover edge cases, deterministic.
- Clean separation of concerns, names that say what things do, matches the surrounding code style (this repo: C, tabs or the file's existing indentation, CRLF where the file uses CRLF), comments only where they add information.
- Each file has one clear responsibility; flag files this change made large.
Do not edit files and do not commit.

Severity: critical = bug or broken behaviour; important = test gap, architecture or robustness problem that should be fixed now; minor = polish. ready = yes only without critical or important issues. Each issue needs file, line, the problem and the fix.`
}

// Light review (owner's choice for tooling and docs tasks, 2026-10-25): one reviewer checks spec
// compliance and quality together, at high effort, for at most LIGHT_ROUNDS rounds. Critical and
// important issues are fixed between rounds; in the last round only critical ones block. Whatever is
// left unfixed (important or minor) is returned as leftovers in the task's `minor` list.
const LIGHT_ROUNDS = 2

function lightReviewPrompt(t, impl) {
  return specReviewPrompt(t, impl) + `

In the same review, also judge code and document quality as a senior reviewer would: correctness, real-time and ISR safety where firmware is touched, tests that verify behaviour, names, matching the surrounding style (CRLF where the file uses it), and no stale or wrong statements in docs. Use severities: critical = bug or broken behaviour; important = must be fixed now; minor = polish. Return compliant=true only if there is no critical or important issue.`
}

// Parallel review (owner's choice for workflow B, 2026-10-25 12:25): in each pass the spec and the
// quality reviewer work at the same time on the same HEAD, and one fixer takes both lists. A spec
// review that passed stays passed, as in the sequential flow. After every fix the quality review
// runs again, because in the sequential flow it always saw the code after the spec fixes. Each kind
// keeps the blocking rules of reviewLoop, where its round is 1 + the fixes its own findings caused,
// so a quality review that only re-checks a spec fix still counts as that kind's earlier round.
function parallelNote(t, kind, round) {
  const other = kind === 'spec' ? 'quality' : 'spec'
  const dir = `review-${t.id}-${kind}-${round}`
  return `

## Working beside another reviewer

The ${other} review may be running on the same HEAD at the same time as yours. So that the two of you do not collide:
- Do not build, run tests or write anything in ${A.repo}: tools/check.sh and ctest write build/ there.
- To build, test or try mutants, export HEAD into a directory of your own under the scratchpad, named ${dir}:
  git -C ${A.repo} archive HEAD | tar -x -C <that directory>, then ln -s ${A.repo}/.toolchain <that directory>/.toolchain so tools/env.sh finds the toolchain.
- Review only what your own review covers; the ${other} reviewer covers the rest.`
}

// Focused re-review (owner's choice for workflow B, 2026-10-25 12:55): the first run of each kind
// reviews the whole task; a later run checks the findings it raised before and reviews the fix
// commits since the HEAD it saw, instead of reviewing the whole task again.
function focusedReviewPrompt(t, kind, run, seenHead, findings, fix) {
  const what = kind === 'spec' ? 'compliance with its specification' : 'code quality, as a senior embedded C reviewer'
  const report = fix ? { summary: fix.summary, concerns: fix.concerns || [], deviations: fix.deviations || [], tests: fix.tests } : {}
  const verdict = kind === 'spec'
    ? 'Return compliant=true only if every finding above is fixed and the fix commits break nothing.'
    : 'Severity: critical = bug or broken behaviour; important = test gap, architecture or robustness problem that should be fixed now; minor = polish. ready = yes only without critical or important issues.'
  return `You are re-reviewing ${t.id}: ${t.title} of the LICHOMAT stage 1 plan for ${what}. This is run ${run} of the ${kind} review. Run 1 reviewed the whole task, so this run is focused on what changed since your previous run.

## Task description (full text from the plan)

${taskText(t)}

## Context

${A.context}

Authoritative documents: docs/superpowers/specs/2026-10-23-timing-core-design.md, docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (notes win over the spec), the plan ${A.plan}.

## What changed since your previous run

Your previous run reviewed HEAD ${seenHead}. Since then a fixer committed ${seenHead}..HEAD in ${A.repo} (git log and git diff ${seenHead}..HEAD). Its report (do not trust it):

${JSON.stringify(report, null, 1)}

## Findings of your previous run that had to be fixed (JSON)

${findings.length ? JSON.stringify(findings, null, 1) : 'None: your previous run passed. The fix commits answer findings of the other review, and you check that they do not break what you passed.'}

## Your job

1. For each finding above, check the code: is it really fixed, at its cause, with a test where the finding asked for one? Where the fixer rejected a finding (see its concerns), judge the reason. A finding that is not fixed, or fixed wrongly, is an issue again with its original severity.
2. Review the fix commits, git diff ${seenHead}..HEAD, for ${kind === 'spec' ? 'the task and the spec/notes' : 'correctness (edge cases, overflow, ordering, ISR/main-context safety, real-time cost on the Cortex-M7), tests that verify behaviour, names and the surrounding style (CRLF where the file uses it)'}: new bugs or regressions, broken rules, tests that do not test what their name claims, comments and docs the fix left stale.
3. Run the host tests and tools/check.sh in your own export of HEAD (see below), and try mutants on the lines the fix commits changed.
Do not re-review what the earlier runs passed outside the fix commits. Do not edit files and do not commit.
${verdict} Each issue needs file, line, the problem and the fix.`
}

async function parallelReview(t, impl) {
  let current = impl
  const passed = { spec: null, quality: null }
  const fixes = { spec: 0, quality: 0 }   // fixes caused by each kind's findings
  const runs = { spec: 0, quality: 0 }    // reviews run, for the labels
  const leftovers = { spec: [], quality: [] }
  const seenAt = { spec: null, quality: null }      // the HEAD each kind last reviewed
  const findings = { spec: [], quality: [] }        // what each kind's last run sent to the fixer
  let lastFix = null
  for (let pass = 1; pass <= 2 * MAX_ROUNDS; pass++) {
    const kinds = ['spec', 'quality'].filter((k) => !passed[k])
    for (const k of kinds) runs[k]++
    const reviews = await parallel(kinds.map((k) => () => agent(
      (runs[k] === 1
        ? (k === 'spec' ? specReviewPrompt : qualityReviewPrompt)(t, current)
        : focusedReviewPrompt(t, k, runs[k], seenAt[k], findings[k], lastFix)) + parallelNote(t, k, runs[k]),
      { label: `${k}:${t.id}#${runs[k]}`, phase: k === 'spec' ? 'Spec review' : 'Quality review', schema: k === 'spec' ? SPEC : QUALITY })))
    for (const k of kinds) seenAt[k] = current.head_sha
    const blocking = []
    for (let i = 0; i < kinds.length; i++) {
      const k = kinds[i]
      const review = reviews[i]
      if (!review) return { ok: false, reason: `${k} reviewer failed`, impl: current }
      // The rules of reviewLoop, per kind: critical always; important except in the last quality
      // round; minor spec issues in the first spec round only.
      const round = 1 + fixes[k]
      const lastRound = round >= MAX_ROUNDS
      const blocks = (x) => x.severity === 'critical'
        || (x.severity === 'important' && (k === 'spec' || !lastRound))
        || (x.severity === 'minor' && k === 'spec' && round === 1)
      const b = (review.issues || []).filter(blocks)
      leftovers[k] = (review.issues || []).filter((x) => !blocks(x))
      findings[k] = b
      if (b.length === 0) {
        passed[k] = review
        continue
      }
      if (lastRound) return { ok: false, reason: `${k} review not satisfied after ${MAX_ROUNDS} rounds`, review, impl: current }
      blocking.push(...b.map((x) => ({ ...x, review: k })))
    }
    if (blocking.length === 0) return { ok: true, spec: passed.spec, quality: passed.quality, impl: current, minor: [...leftovers.spec, ...leftovers.quality] }
    const fix = await agent(fixPrompt(t, current, blocking, 'spec and quality'), { label: `fix:${t.id}:review#${pass}`, phase: 'Quality review', schema: IMPL, effort: 'high' })
    if (!fix || fix.status === 'BLOCKED' || fix.status === 'NEEDS_CONTEXT') return { ok: false, reason: `review fix ${fix ? fix.status : 'failed'}`, fix, impl: current }
    current = { ...current, head_sha: fix.head_sha, summary: current.summary + ' | fix: ' + fix.summary }
    lastFix = fix
    for (const k of new Set(blocking.map((x) => x.review))) fixes[k]++
    passed.quality = null
  }
  return { ok: false, reason: `reviews not settled after ${2 * MAX_ROUNDS} passes`, impl: current }
}

async function lightReview(t, impl) {
  let current = impl
  for (let round = 1; round <= LIGHT_ROUNDS; round++) {
    const review = await agent(lightReviewPrompt(t, current), { label: `review:${t.id}#${round}`, phase: 'Spec review', schema: SPEC, effort: 'high' })
    if (!review) return { ok: false, reason: 'light reviewer failed', impl: current }
    const blocking = (review.issues || []).filter((i) => i.severity !== 'minor')
    if (blocking.length === 0) return { ok: true, review, impl: current, minor: review.issues || [] }
    if (round === LIGHT_ROUNDS) {
      const critical = blocking.filter((i) => i.severity === 'critical')
      if (critical.length === 0) return { ok: true, review, impl: current, minor: review.issues || [] }
      return { ok: false, reason: `critical issues after ${LIGHT_ROUNDS} light rounds`, review, impl: current }
    }
    const fix = await agent(fixPrompt(t, current, blocking, 'light'), { label: `fix:${t.id}:light#${round}`, phase: 'Spec review', schema: IMPL, effort: 'high' })
    if (!fix || fix.status === 'BLOCKED' || fix.status === 'NEEDS_CONTEXT') return { ok: false, reason: `light fix ${fix ? fix.status : 'failed'}`, fix, impl: current }
    current = { ...current, head_sha: fix.head_sha, summary: current.summary + ' | fix: ' + fix.summary }
  }
  return { ok: false, reason: 'unreachable' }
}

// A task's full text is given inline (text) or in a file the agents read first (file).
function taskText(t) {
  return t.text ? t.text : `The full text of this task, exactly as the plan has it, is in the file ${t.file}. Read that whole file first; it is your task description.`
}

const results = []

async function reviewLoop(t, impl, kind, promptFn, schema) {
  let current = impl
  for (let round = 1; round <= MAX_ROUNDS; round++) {
    const review = await agent(promptFn(t, current), { label: `${kind}:${t.id}#${round}`, phase: kind === 'spec' ? 'Spec review' : 'Quality review', schema })
    if (!review) return { ok: false, reason: `${kind} reviewer failed`, impl: current }
    // What blocks a task:
    // - critical issues, always;
    // - important issues, except in the last quality round;
    // - minor spec issues, in the first round only.
    // A stream of test-gap findings therefore cannot stop the track; the issues that do not block
    // are reported with the task's result as leftovers.
    const lastRound = round === MAX_ROUNDS
    const blocks = (i) => i.severity === 'critical'
      || (i.severity === 'important' && (kind === 'spec' || !lastRound))
      || (i.severity === 'minor' && kind === 'spec' && round === 1)
    const blocking = (review.issues || []).filter(blocks)
    if (blocking.length === 0) return { ok: true, review, impl: current, minor: (review.issues || []).filter((i) => !blocks(i)) }
    if (round === MAX_ROUNDS) return { ok: false, reason: `${kind} review not satisfied after ${MAX_ROUNDS} rounds`, review, impl: current }
    const fix = await agent(fixPrompt(t, current, blocking, kind), { label: `fix:${t.id}:${kind}#${round}`, phase: kind === 'spec' ? 'Spec review' : 'Quality review', schema: IMPL, effort: 'high' })
    if (!fix || fix.status === 'BLOCKED' || fix.status === 'NEEDS_CONTEXT') return { ok: false, reason: `${kind} fix ${fix ? fix.status : 'failed'}`, fix, impl: current }
    current = { ...current, head_sha: fix.head_sha, summary: current.summary + ' | fix: ' + fix.summary }
  }
  return { ok: false, reason: 'unreachable' }
}

for (const t of A.tasks) {
  log(`${t.id}: implementing`)
  const impl = await agent(implementPrompt(t), { label: `impl:${t.id}`, phase: 'Implement', schema: IMPL, effort: 'high' })
  if (!impl || impl.status === 'BLOCKED' || impl.status === 'NEEDS_CONTEXT') {
    results.push({ id: t.id, impl })
    return { stoppedAt: t.id, reason: impl ? impl.status : 'implementer failed', results }
  }
  if (t.review === 'light') {
    const light = await lightReview(t, impl)
    if (!light.ok) {
      results.push({ id: t.id, impl, light })
      return { stoppedAt: t.id, reason: light.reason, results }
    }
    results.push({ id: t.id, impl: light.impl, concerns: impl.concerns || [], deviations: impl.deviations || [], minor: light.minor })
    log(`${t.id}: done (light review) at ${light.impl.head_sha}`)
    continue
  }
  if (t.review === 'parallel') {
    const both = await parallelReview(t, impl)
    if (!both.ok) {
      results.push({ id: t.id, impl, review: both })
      return { stoppedAt: t.id, reason: both.reason, results }
    }
    results.push({ id: t.id, impl: both.impl, concerns: impl.concerns || [], deviations: impl.deviations || [], minor: both.minor })
    log(`${t.id}: done (parallel reviews) at ${both.impl.head_sha}`)
    continue
  }
  const spec = await reviewLoop(t, impl, 'spec', specReviewPrompt, SPEC)
  if (!spec.ok) {
    results.push({ id: t.id, impl, spec })
    return { stoppedAt: t.id, reason: spec.reason, results }
  }
  const quality = await reviewLoop(t, spec.impl, 'quality', qualityReviewPrompt, QUALITY)
  if (!quality.ok) {
    results.push({ id: t.id, impl, spec, quality })
    return { stoppedAt: t.id, reason: quality.reason, results }
  }
  const minor = [...(spec.minor || []), ...(quality.minor || [])]
  results.push({ id: t.id, impl: quality.impl, concerns: impl.concerns || [], deviations: impl.deviations || [], minor })
  log(`${t.id}: done at ${quality.impl.head_sha}`)
}

let final = null
if (A.final && A.final.review && results.length) {
  phase('Final review')
  final = await agent(`You are reviewing the whole track of LICHOMAT stage 1 work just completed: tasks ${A.tasks.map((t) => t.id).join(', ')}.

## Context

${A.context}

Authoritative documents: the spec, the notes N1-N21 and the plan ${A.plan}. Range: from ${results[0].impl.base_sha} to HEAD in ${A.repo}.

Review the combined result as one piece: does it meet the spec and notes for these tasks, are the pieces consistent with each other, are there bugs that only show across tasks, are the tests strong enough to catch regressions? Do not edit files.`, { label: 'final-review', phase: 'Final review', schema: QUALITY })
}

return { completed: results.map((r) => r.id), results, final }
