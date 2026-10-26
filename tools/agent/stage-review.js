export const meta = {
  name: 'lichomat-stage-review',
  description: 'E4.1: the stage 1 review. Checklist and automated rows, parallel reviewers for the manual rows, a skeptic per finding, test-first fixes, the gate, a light review',
  phases: [
    { title: 'Setup', detail: 'the checklist, the stage 1 report and the automated rows (task Steps 1-5)' },
    { title: 'Review', detail: 'one read-only reviewer per group of manual rows (Step 6)' },
    { title: 'Verify', detail: 'independent skeptics try to refute each finding' },
    { title: 'Fix', detail: 'record the results, then each finding test-first in its own commit (Step 7)' },
    { title: 'Gate', detail: 'the result line, the gate and the commit (Steps 8-10)' },
    { title: 'Light review', detail: 'one combined review of the task at high effort, at most 2 rounds' },
  ],
}

// Workflow script for Claude Code: runs task E4.1 (the multi-dimension review of LICHOMAT stage 1)
// with the parallel reviewers its Step 6 asks for, an adversarial check of every finding, and one
// fixer per finding. Run it with the Workflow tool (scriptPath) and args below.
// Copyright (C) 2026 Tomasz Krzywicki
// SPDX-License-Identifier: GPL-3.0-only
//
// args: {
//   repo, branch, plan, context,                 // as for impl-track.js
//   task: { id, title, file },                   // the task text, e.g. build/plan/tasks/E4.1.md
//   groups: [{ key, rows, focus }],              // the reviewers of Step 6; rows as the checklist names them
// }

const A = args
const LIGHT_ROUNDS = 2
const REVIEW = 'docs/lichomat/reports/stage1-review.md'
const REPORT = 'docs/lichomat/reports/stage1-report.md'

const RULES = `Working rules (all mandatory):
- Work only in ${A.repo} on branch ${A.branch}. Never push, never switch branches, never touch main, never change git config, never run destructive git commands (reset --hard, checkout -- ., clean, rebase, stash drop, branch -D), and never rewrite a commit (no commit --amend): fix with a new commit.
- Add files explicitly (git add <path> ...); never git add -A or git add . ; commit only the files of your step.
- Every commit message ends with this trailer line: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Do not edit docs/lichomat/tracker.json and do not run tools/tracker_note.py: the controller keeps the tracker. Where the task says to add a question to the tracker, return it in your result instead.
- Toolchain: "source tools/env.sh" puts arm-none-eabi-gcc, cmake and ninja on PATH. "tools/check.sh" runs the host tests, the firmware release build, prints the memory summary and the warning count. It must pass with 0 warnings after every change.
- Authoritative documents: docs/superpowers/specs/2026-10-23-timing-core-design.md (spec), docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (notes N1-N21; they win over the spec), the plan ${A.plan} (its "Execution amendments" section lists the changes made while executing it).
- Line endings: files in Core/ use CRLF, the others LF. After you edit a file in Core/, run perl -pi -e 's/\\r?\\n/\\r\\n/' <file> and check before committing that the file's count of CRLF equals its count of LF bytes (python3: d.count(b'\\r\\n') == d.count(b'\\n')); the file command reads only the start of a file.
- Hardware checks (H1-H13) cannot be run here; never claim them.`

const READ_ONLY = `You change no file in ${A.repo} and run nothing there that writes: tools/check.sh and ctest write build/. To build or test, export HEAD into a directory of your own under the scratchpad (git -C ${A.repo} archive HEAD | tar -x -C <dir>, then ln -s ${A.repo}/.toolchain <dir>/.toolchain).`

function taskText() {
  return `The full text of task ${A.task.id}, exactly as the plan has it plus the controller's notes, is in the file ${A.task.file}. Read that whole file first.`
}

const FINDING = {
  type: 'object',
  properties: {
    row: { type: 'string' },
    file: { type: 'string' },
    line: { type: 'string' },
    problem: { type: 'string' },
    breaks: { type: 'string', description: 'the spec section, note or plan decision it breaks' },
    severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
    fix: { type: 'string' },
  },
  required: ['row', 'problem', 'severity'],
}

const SETUP = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['DONE', 'BLOCKED'] },
    base_sha: { type: 'string', description: 'git rev-parse HEAD before you changed anything' },
    head_sha: { type: 'string' },
    automated: { type: 'array', items: { type: 'object', properties: { row: { type: 'string' }, result: { type: 'string' } }, required: ['row', 'result'] } },
    findings: { type: 'array', items: FINDING, description: 'findings of the automated rows, in the findings table as F1, F2, ... in this order' },
    notes: { type: 'string' },
  },
  required: ['status', 'base_sha', 'head_sha', 'automated', 'findings'],
}

const GROUP = {
  type: 'object',
  properties: {
    rows: { type: 'array', items: { type: 'object', properties: { row: { type: 'string' }, result: { type: 'string', enum: ['pass', 'findings'] }, note: { type: 'string' } }, required: ['row', 'result'] } },
    findings: { type: 'array', items: FINDING },
  },
  required: ['rows', 'findings'],
}

const VERDICT = {
  type: 'object',
  properties: {
    real: { type: 'boolean' },
    severity: { type: 'string', enum: ['blocker', 'major', 'minor'] },
    reason: { type: 'string' },
  },
  required: ['real', 'severity', 'reason'],
}

const STEP = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['DONE', 'BLOCKED'] },
    head_sha: { type: 'string' },
    summary: { type: 'string' },
    decisions: { type: 'array', items: { type: 'object', properties: { id: { type: 'string' }, decision: { type: 'string', enum: ['fixed', 'accepted', 'question'] }, commit: { type: 'string' }, note: { type: 'string' } }, required: ['id', 'decision'] } },
    questions: { type: 'array', items: { type: 'object', properties: { id: { type: 'string' }, question: { type: 'string' }, assumption: { type: 'string' } }, required: ['question', 'assumption'] } },
  },
  required: ['status', 'head_sha', 'summary'],
}

const LIGHT = {
  type: 'object',
  properties: {
    compliant: { type: 'boolean' },
    issues: { type: 'array', items: { type: 'object', properties: { severity: { type: 'string', enum: ['critical', 'important', 'minor'] }, file: { type: 'string' }, line: { type: 'string' }, problem: { type: 'string' }, fix: { type: 'string' } }, required: ['severity', 'problem', 'fix'] } },
    notes: { type: 'string' },
  },
  required: ['compliant', 'issues'],
}

// 1. Setup: Steps 1-5 of the task.
phase('Setup')
const setup = await agent(`You run the first part of ${A.task.id}: ${A.task.title}, one task of the LICHOMAT stage 1 plan.

## Task

${taskText()}

## Context

${A.context}

${RULES}

## Your job

Do Steps 1-5 of the task, in order:
1. Write ${REVIEW} (Step 1) and see the gate fail (Step 2); commit it (Step 3).
2. Write ${REPORT} (Step 4). Collect every deviation and intended change from the plan as it is now, including the tasks added during execution (E1.15-E1.19) and the "Execution amendments" section, and from the notes N1-N21.
3. Run the automated rows (Step 5) and write each result into the checklist. For a row that does not pass, add a finding to the findings table with the next F number (F1, F2, ... in the order you return them) and status \`fix\`; do not fix it yet.
4. Commit the report and the results (explicit file list, the trailer).

The task was written when the notes ran to N20 and E1 to E1.14. Adapt the checklist text to the current state, keep the rows' intent, and report every adaptation in notes. Where a controller note in the task file amends a row, such as R6.2's RAM budget, use the amended text.`, { label: `setup:${A.task.id}`, phase: 'Setup', schema: SETUP, effort: 'high' })
if (!setup || setup.status !== 'DONE') return { stoppedAt: 'setup', setup }
log(`setup done at ${setup.head_sha}: ${setup.findings.length} findings from the automated rows`)

// 2. Review: one read-only reviewer per group of manual rows, all at once.
phase('Review')
const known = setup.findings.map((f, i) => `F${i + 1} (${f.row}): ${f.problem}`).join('\n') || 'none'
const reviewPrompt = (g) => `You review LICHOMAT stage 1, rows ${g.rows} of ${REVIEW} (task ${A.task.id}). ${g.focus}

Read docs/superpowers/specs/2026-10-23-timing-core-design.md and docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md first; the notes N1-N21 win where they differ from the spec. ${REPORT} lists the deviations and the intended changes that the plan decided; the plan's "Execution amendments" section lists the decisions taken while executing it. Review the changes since the official firmware 1.7, \`git diff a43cd4f..HEAD\` in ${A.repo}, for these rows only, and read the code around each change.

${READ_ONLY}

For each row answer "pass", or give findings: file and line, what is wrong, which spec section or note it breaks, a severity (blocker: wrong behaviour, a broken build or a broken file format; major: a requirement of the spec or the notes not met; minor: anything else) and the fix. A row passes only if its "Pass when" holds as written. Findings already in the table, do not repeat them:
${known}

## Context

${A.context}`
const reviews = await parallel(A.groups.map((g) => () => agent(reviewPrompt(g), { label: `review:${g.key}`, phase: 'Review', schema: GROUP })))
// A reviewer that returned nothing gets one more try, so that no group of rows stays unreviewed.
for (const [i, g] of A.groups.entries()) {
  if (!reviews[i]) reviews[i] = await agent(reviewPrompt(g), { label: `review:${g.key}#2`, phase: 'Review', schema: GROUP })
}
const groupResults = reviews.map((r, i) => ({ group: A.groups[i].key, result: r }))
const failedGroups = groupResults.filter((g) => !g.result).map((g) => g.group)
if (failedGroups.length) log(`reviewers that returned nothing: ${failedGroups.join(', ')}`)
// Two groups can find the same problem: keep the first finding per file and line.
const seen = new Set()
const raw = []
for (const f of groupResults.flatMap((g) => (g.result ? g.result.findings.map((x) => ({ ...x, group: g.group })) : []))) {
  const key = f.file && f.line ? `${f.file}:${f.line}` : f.problem.slice(0, 80)
  if (seen.has(key)) continue
  seen.add(key)
  raw.push(f)
}
log(`${raw.length} distinct findings from ${A.groups.length - failedGroups.length} reviewers`)

// 3. Verify: skeptics try to refute each finding; blockers and majors get two.
phase('Verify')
const verified = await parallel(raw.map((f, i) => async () => {
  const n = f.severity === 'minor' ? 1 : 2
  const votes = (await parallel(Array.from({ length: n }, (_, k) => () => agent(`You check one finding of the LICHOMAT stage 1 review (task ${A.task.id}, row ${f.row}). Try to refute it: read the code it names in ${A.repo}, the spec, the notes N1-N21, ${REPORT} and the plan's "Execution amendments". It is not real if the code does what the spec and notes ask, if a note or a recorded plan decision allows the behaviour, or if the facts it cites are wrong. If it is real, say whether its severity is right (blocker: wrong behaviour, a broken build or a broken file format; major: a requirement of the spec or the notes not met; minor: anything else).

${READ_ONLY}

## The finding (JSON)

${JSON.stringify(f, null, 1)}`, { label: `verify:${i + 1}.${k + 1}`, phase: 'Verify', schema: VERDICT })))).filter(Boolean)
  const real = votes.filter((v) => v.real)
  if (votes.length === 0) return { ...f, verdict: 'unchecked' }
  if (real.length === 0) return { ...f, verdict: 'refuted', reasons: votes.map((v) => v.reason) }
  const order = ['minor', 'major', 'blocker']
  const severity = real.map((v) => v.severity).sort((a, b) => order.indexOf(b) - order.indexOf(a))[0]
  return { ...f, severity, verdict: 'real', reasons: real.map((v) => v.reason) }
}))
const confirmed = verified.filter(Boolean).filter((f) => f.verdict !== 'refuted')
const refuted = verified.filter(Boolean).filter((f) => f.verdict === 'refuted')
log(`${confirmed.length} findings confirmed, ${refuted.length} refuted`)

// 4. Fix: record every row result and finding first, then one fixer per blocker or major and one
// for the minors together, in turn, each test-first with one commit per finding.
phase('Fix')
const rowResults = groupResults.flatMap((g) => (g.result ? g.result.rows : []))
const numbered = confirmed.map(({ reasons, verdict, ...f }, i) => ({ ...f, id: `F${setup.findings.length + i + 1}` }))
const record = await agent(`You record the review results of ${A.task.id} in ${REVIEW}.

${RULES}

## Your job

1. Write each manual row's result into its Result cell: \`pass\`, or the F numbers of its findings.
2. Add each finding below to the findings table under its id: the row, file and line, what is wrong, the severity and the status \`fix\`. Where a row has findings, its Result cell names their ids.
3. Do not fix anything. Commit ${REVIEW} alone, with the trailer.

## Row results (JSON)

${JSON.stringify(rowResults, null, 1)}

## Confirmed findings (JSON)

${JSON.stringify(numbered, null, 1)}

Findings the skeptics refuted are not recorded; they are listed in the workflow's result.`, { label: 'record', phase: 'Fix', schema: STEP })
if (!record || record.status !== 'DONE') return { stoppedAt: 'record', setup, confirmed, refuted, record }
const automatedFindings = setup.findings.map((f, i) => ({ ...f, id: `F${i + 1}` }))
const all = [...automatedFindings, ...numbered]
const batches = [...all.filter((f) => f.severity !== 'minor').map((f) => [f]), all.filter((f) => f.severity === 'minor')].filter((b) => b.length)
const fixes = []
for (const [i, batch] of batches.entries()) {
  const fix = await agent(`You fix findings of the LICHOMAT stage 1 review (task ${A.task.id}). They are in the findings table of ${REVIEW} with status \`fix\`.

## Task

${taskText()}

## Context

${A.context}

${RULES}

## Findings to handle (JSON)

${JSON.stringify(batch, null, 1)}

## Your job

Follow "Fixing findings" in ${REVIEW} for each finding above, one at a time:
- Decide it: \`fix\`, \`accepted\` (a known limitation for the release notes) or \`question\` (the owner decides; return the question and the reversible default you keep, the controller adds it to the tracker). Blockers and majors are \`fix\` unless the owner has to decide.
- For a \`fix\`: write the failing test first and see it fail, fix with the smallest change, see it pass, run tools/check.sh (0 warnings), and commit that finding alone with its files added by name: "Fix F<n>: <what was wrong>". If the fix changes a deviation or an intended change, update ${REPORT} in the same commit.
- Set the finding's status (\`fixed <short commit>\`, \`accepted\` or \`question\`) and rerun the checklist rows it touches; commit the review file with the finding.
Return a decision per finding.`, { label: `fix:${i + 1}/${batches.length}`, phase: 'Fix', schema: STEP, effort: 'high' })
  fixes.push(fix)
  if (!fix || fix.status !== 'DONE') return { stoppedAt: `fix ${i + 1}`, setup, confirmed, refuted, fixes }
}

// 5. Gate: Steps 8-10.
phase('Gate')
const gate = await agent(`You finish ${A.task.id}: ${A.task.title}.

## Task

${taskText()}

${RULES}

## Your job

Do Steps 8-10 of the task: fill in the result line of ${REVIEW}, run the gate (it must print 0, then tools/check.sh must pass with 0 warnings), and commit the result. If the gate does not print 0, list the rows and findings that are still \`open\` or \`fix\` and return BLOCKED. Tick the task's checkboxes in ${A.plan} and include the plan in the commit.`, { label: 'gate', phase: 'Gate', schema: STEP })
if (!gate || gate.status !== 'DONE') return { stoppedAt: 'gate', setup, confirmed, refuted, fixes, gate }

// 6. Light review (the owner's choice for E3 and E4): one combined reviewer at high effort.
phase('Light review')
let head = gate.head_sha
let light = null
for (let round = 1; round <= LIGHT_ROUNDS; round++) {
  light = await agent(`You review task ${A.task.id}: ${A.task.title} of the LICHOMAT stage 1 plan, done in ${A.repo} in the commits ${setup.base_sha}..${head}: the review file ${REVIEW}, the report ${REPORT} and the fix commits.

## Task

${taskText()}

## Context

${A.context}

${READ_ONLY}

Check, as a senior reviewer: every row has a result that its "Pass when" supports; every finding is decided with a reason; each fix has a test that fails without it and does what the finding asked; the report lists every deviation and intended change of stage 1, including the execution amendments; nothing in the files is stale or wrong. Severities: critical = a wrong result, a broken fix or a broken build; important = must be fixed now; minor = polish. Return compliant=true only without critical or important issues.`, { label: `light:${A.task.id}#${round}`, phase: 'Light review', schema: LIGHT, effort: 'high' })
  if (!light) return { stoppedAt: 'light review', setup, confirmed, refuted, fixes, gate }
  const blocking = (light.issues || []).filter((x) => x.severity === 'critical' || (x.severity === 'important' && round < LIGHT_ROUNDS))
  if (blocking.length === 0) break
  if (round === LIGHT_ROUNDS) return { stoppedAt: 'light review', reason: 'critical issues after the last round', setup, confirmed, refuted, fixes, gate, light }
  const fix = await agent(`You fix the review findings of task ${A.task.id} (LICHOMAT stage 1).

## Task

${taskText()}

${RULES}

## Findings (JSON)

${JSON.stringify(blocking, null, 1)}

Verify each against the files; fix each real one with the smallest change, test-first where it touches code; run tools/check.sh (0 warnings); commit with an explicit file list. For a finding that is wrong, say why.`, { label: `light-fix:${A.task.id}#${round}`, phase: 'Light review', schema: STEP, effort: 'high' })
  if (!fix || fix.status !== 'DONE') return { stoppedAt: 'light fix', setup, confirmed, refuted, fixes, gate, light, fix }
  head = fix.head_sha
}

const questions = fixes.flatMap((f) => (f && f.questions) || [])
return { done: true, head_sha: head, automated: setup.automated, findings: all.length, confirmed: numbered.length, refuted, decisions: fixes.flatMap((f) => (f && f.decisions) || []), questions, leftovers: (light && light.issues) || [] }
