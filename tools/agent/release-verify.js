export const meta = {
  name: 'lichomat-release-verify',
  description: 'The owner-facing documents of stage 1: five read-only lenses check the hardware checklist and the v0.1 release notes against the code, a skeptic per lens, one documents fixer, a light review',
  phases: [
    { title: 'Review', detail: 'five read-only lenses on the checklist and the release notes' },
    { title: 'Verify', detail: 'one skeptic per lens tries to refute each of its findings' },
    { title: 'Fix', detail: 'one fixer applies the confirmed findings, documents only' },
    { title: 'Light review', detail: 'one review of the fix commits at high effort, at most 2 rounds' },
  ],
}

// Workflow script for Claude Code: an independent check of the two documents the owner works from
// after stage 1, the hardware checklist (E4.2) and the v0.1 release notes (E4.3). The owner runs
// the checklist on the real LXR-02, so a wrong number or a step that cannot tell the fix from the
// defect costs a session at the device. Run it with the Workflow tool (scriptPath) and args below.
// Copyright (C) 2026 Tomasz Krzywicki
// SPDX-License-Identifier: GPL-3.0-only
//
// Licensed under the GNU General Public License version 3, with the additional
// permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
// the repository.
//
// args: {
//   repo, branch, plan, context,                 // as for impl-track.js
//   known: '...',                                // markdown list of known items from the E4.2 and E4.3 reviews
// }

const A = args
const LIGHT_ROUNDS = 2
const CHECKLIST = 'docs/lichomat/hardware-checklist.md'
const NOTES = 'docs/lichomat/release-notes-v0.1.md'
const REPORT = 'docs/lichomat/reports/stage1-report.md'
const REVIEW = 'docs/lichomat/reports/stage1-review.md'

const RULES = `Working rules (all mandatory):
- Work only in ${A.repo} on branch ${A.branch}. Never push, never switch branches, never touch main, never change git config, never run destructive git commands (reset --hard, checkout -- ., clean, rebase, stash drop, branch -D), and never rewrite a commit (no commit --amend): fix with a new commit.
- Add files explicitly (git add <path> ...); never git add -A or git add . ; commit only the files of your step.
- Every commit message ends with this trailer line: Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
- Do not edit docs/lichomat/tracker.json and do not run tools/tracker_note.py: the controller keeps the tracker. Return owner questions in your result instead.
- Toolchain: "source tools/env.sh" puts arm-none-eabi-gcc, cmake and ninja on PATH. "tools/check.sh" runs the host tests, the firmware release build, prints the memory summary and the warning count. It must pass with 0 warnings after your work.
- Authoritative documents: docs/superpowers/specs/2026-10-23-timing-core-design.md (spec), docs/superpowers/specs/2026-10-24-timing-core-implementation-notes.md (notes N1-N21; they win over the spec), the plan ${A.plan} (its "Execution amendments" list the changes made while executing it), ${REPORT} (every deviation and intended change of stage 1) and ${REVIEW} (the stage 1 review and its findings F1-F52).
- Line endings: files in Core/ use CRLF, the others LF.
- Hardware checks (H1-H13, U1, X1-X4) cannot be run here; never claim them, and never mark one as run or passed.`

const READ_ONLY = `You change no file in ${A.repo} and run nothing there that writes: tools/check.sh and ctest write build/. To build, test or try a computation, export HEAD into a directory of your own under the scratchpad (git -C ${A.repo} archive HEAD | tar -x -C <dir>, then ln -s ${A.repo}/.toolchain <dir>/.toolchain).`

const LENSES = [
  {
    key: 'numbers',
    focus: `Recompute every number that ${CHECKLIST} expects or records: times, intervals, BPM, tolerances, swing and ratchet positions, pulse and step counts, CPU figures, file names and the SHA-256 values in the run sheet. Derive each one from the spec's timing rules (a step is 96 ticks; swing is round(shuffle x 64 / 127) ticks on odd steps; shift is 12 ticks per unit; ratchet hit j of k is j x 96 / k ticks into the step), the notes and the constants in the code at HEAD, and state the formula and the inputs you used. Check the hashes by building in your own export (tools/check.sh there) and by comparing with what the checklist and ${NOTES} say. A wrong number is a finding. So is a tolerance so wide that the check passes with the defect it is meant to catch, or so narrow that a correct LXR fails it.`,
  },
  {
    key: 'procedures',
    focus: `Walk through every check of ${CHECKLIST} as the owner would at the LXR-02. Check that every menu page, parameter name and value exists as written (Core/Inc/menu/menuPages.h, Core/Src/menu/menu.c, the text tables), that every button combination does what the step says (Core/Src/Hardware/... or wherever the front panel's button handling lives; find it), that the default settings and the routing names are right, that the SD card steps match the updater (find the bootloader or updater code and what it requires of the file name), and that the gear and software the check assumes are named. A step the owner cannot do as written, a missing setup step, or a step whose result does not tell the fixed behaviour from the fw 1.7 behaviour is a finding.`,
  },
  {
    key: 'coverage',
    focus: `List every row of section 2 of ${REPORT} whose Notes column says Fixed, Changed or New, and every user-visible statement under "Fixed", "Changed behaviour" and "New" in ${NOTES}. For each, name the hardware check step (H1-H13, U1, X1-X4) of ${CHECKLIST} that shows it on the device, or the reason no hardware check is needed: the behaviour is fully covered by host tests and the device cannot show it differently, or it cannot be observed. A user-visible change with neither is a finding: propose the check step in full (setup, steps, an Expected line with numbers, a Record line), as short as the existing checks are, preferably as a step of an existing check.`,
  },
  {
    key: 'notes',
    focus: `Check every statement of ${NOTES} at HEAD against the code, the host tests, ${REPORT} and ${REVIEW}: installing and going back to the official firmware (the updater's rules for the file name and the steps it shows), the fixed, changed and unchanged lists, the CPU page (how to reach it and what it shows, from menu.c), the known limitations (every accepted finding a user can notice, and the defaults of the open owner questions Q9, Q10 and Q11 where a user would notice them), the files and hashes, the licence (NOTICE.md) and the publishing steps. A wrong, misleading or missing statement is a finding. A sentence a musician who owns an LXR-02 cannot follow is a minor finding.`,
  },
  {
    key: 'consistency',
    focus: `Check that ${NOTES}, ${CHECKLIST}, ${REPORT}, ${REVIEW}, README.md and docs/lichomat/HANDOFF.md agree with each other: numbers, hashes, commit ids, names of files, commands, the owner-question defaults, the list of checks and how they are referred to. Check the branding rules of review row R10.3 in all of them: LICHOMAT is never presented as official, and "Erica Synths" and "LXR" appear only in truthful references (the device, the official firmware and its update file, the trademarks, the update file name that the updater requires to start with LXR). A disagreement is a finding; say which document is right, from the code.`,
  },
]

const FINDING = {
  type: 'object',
  properties: {
    file: { type: 'string' },
    line: { type: 'string' },
    problem: { type: 'string' },
    evidence: { type: 'string', description: 'the code, test, formula or document that shows it, with paths and lines' },
    severity: { type: 'string', enum: ['important', 'minor'], description: 'important: wrong or misleading for the owner, or a check that cannot tell the fix from the defect; minor: anything else' },
    fix: { type: 'string', description: 'the exact change, with the new text where it is text' },
  },
  required: ['file', 'problem', 'evidence', 'severity', 'fix'],
}

const LENS = {
  type: 'object',
  properties: {
    checked: { type: 'string', description: 'what you checked and how, briefly' },
    findings: { type: 'array', items: FINDING },
  },
  required: ['checked', 'findings'],
}

const VERDICTS = {
  type: 'object',
  properties: {
    verdicts: {
      type: 'array',
      items: {
        type: 'object',
        properties: {
          index: { type: 'integer' },
          real: { type: 'boolean' },
          severity: { type: 'string', enum: ['important', 'minor'] },
          reason: { type: 'string' },
          fix: { type: 'string', description: 'the corrected fix, if the finding is real but its fix is wrong' },
        },
        required: ['index', 'real', 'severity', 'reason'],
      },
    },
  },
  required: ['verdicts'],
}

const STEP = {
  type: 'object',
  properties: {
    status: { type: 'string', enum: ['DONE', 'BLOCKED'] },
    base_sha: { type: 'string' },
    head_sha: { type: 'string' },
    summary: { type: 'string' },
    decisions: { type: 'array', items: { type: 'object', properties: { id: { type: 'string' }, decision: { type: 'string', enum: ['fixed', 'not real', 'needs code', 'question'] }, commit: { type: 'string' }, note: { type: 'string' } }, required: ['id', 'decision'] } },
    questions: { type: 'array', items: { type: 'object', properties: { question: { type: 'string' }, assumption: { type: 'string' } }, required: ['question', 'assumption'] } },
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

const lensPrompt = (l) => `You check the owner-facing documents of LICHOMAT stage 1 through one lens: ${l.key}. The owner (a techno producer who owns the LXR-02) runs ${CHECKLIST} on the device and publishes ${NOTES} as the release text; both were just written (plan tasks E4.2 and E4.3) and had one light review each.

## Your lens

${l.focus}

## Known items

The light reviews of E4.2 and E4.3 and their implementers left these. Report each one that falls under your lens and still holds as a finding with a concrete fix; skip the ones that do not hold, and say so under "checked".

${A.known}

${READ_ONLY}

## Context

${A.context}

Return every finding with the file, the line, the evidence (paths and lines of the code, test or document that shows it) and the exact fix. Do not report style preferences. An empty list is a valid result.`

// 1-2. Review and verify: the five lenses run in parallel, and each lens's skeptic starts as soon as
// its lens reports (a pipeline, no barrier). A lens that returns nothing gets one more try.
const lensResults = {}
const verified = await pipeline(
  LENSES,
  async (l) => {
    const r = (await agent(lensPrompt(l), { label: `review:${l.key}`, phase: 'Review', schema: LENS }))
      || (await agent(lensPrompt(l), { label: `review:${l.key}#2`, phase: 'Review', schema: LENS }))
    lensResults[l.key] = r
    if (!r) log(`lens ${l.key} returned nothing twice: unchecked`)
    return r
  },
  async (r, l) => {
    if (!r || !r.findings.length) return []
    const v = await agent(`You check the findings of one lens (${l.key}) of a review of the LICHOMAT stage 1 documents ${CHECKLIST} and ${NOTES}. For each finding below, try to refute it: read the code, tests and documents it names in ${A.repo}. It is not real if the document already says it correctly, if the facts it cites are wrong, or if its fix would make the document wrong. If it is real, say whether its severity is right (important: wrong or misleading for the owner, or a check that cannot tell the fix from the defect; minor: anything else), and correct its fix if the fix is wrong.

${READ_ONLY}

## The findings (JSON, index = position in this list)

${JSON.stringify(r.findings.map((f, k) => ({ index: k, ...f })), null, 1)}`, { label: `verify:${l.key}`, phase: 'Verify', schema: VERDICTS })
    return r.findings.map((f, k) => {
      const vd = v && (v.verdicts || []).find((x) => x.index === k)
      if (!vd) return { ...f, lens: l.key, verdict: 'unchecked' }
      return { ...f, lens: l.key, verdict: vd.real ? 'real' : 'refuted', severity: vd.real ? vd.severity : f.severity, fix: vd.fix || f.fix, reason: vd.reason }
    })
  },
)
const all = verified.filter(Boolean).flat()
const confirmed = all.filter((f) => f.verdict !== 'refuted').map((f, i) => ({ id: `V${i + 1}`, ...f }))
const refuted = all.filter((f) => f.verdict === 'refuted')
log(`${all.length} findings: ${confirmed.length} confirmed, ${refuted.length} refuted`)
if (!confirmed.length) return { done: true, confirmed, refuted, lenses: lensResults }

// 3. Fix: one fixer for the documents.
phase('Fix')
const fix = await agent(`You fix the confirmed findings of an independent review of the LICHOMAT stage 1 documents for the owner: ${CHECKLIST} and ${NOTES}.

${RULES}

## Context

${A.context}

## What you may change

Documents only: ${CHECKLIST}, ${NOTES}, README.md, docs/lichomat/HANDOFF.md, ${REPORT} (where a finding shows it wrong), and the plan's E4.2 and E4.3 sections (their Step 5 evidence tables and checklist text, so that the plan matches the documents). Do not change code or tests. A finding that needs a code change gets the decision \`needs code\` with a note of what the change would be; the controller decides it.

## The findings (JSON)

${JSON.stringify(confirmed, null, 1)}

## Your job

1. Read each finding and check it against the files before you change anything. If it is not real after all, decide it \`not real\` and say why. If it is the owner's to decide, decide it \`question\` with the question and the default you keep.
2. Apply the others with the smallest change that makes the document right, in the documents' style: short sentences, numbers with their units, every new check step with an **Expected** result and a **Record:** line like the existing ones.
3. If ${NOTES} and ${CHECKLIST} quote the same fact, change both.
4. Run, from ${A.repo}: bash build/e4-checklist-check.sh and bash build/e4-notes-check.sh if they exist (they must pass), then tools/check.sh (0 warnings; the image must not change, since only documents change).
5. Commit in a few commits, one per document or per theme, with explicit file lists. Message: "Release documents: <what changed>".

Return a decision per finding (its id), the base and head commits, and a summary.`, { label: 'fix:documents', phase: 'Fix', schema: STEP, effort: 'high' })
if (!fix || fix.status !== 'DONE') return { stoppedAt: 'fix', confirmed, refuted, fix }

// 4. Light review of the fix commits.
phase('Light review')
let head = fix.head_sha
let light = null
const fixes = [fix]
for (let round = 1; round <= LIGHT_ROUNDS; round++) {
  light = await agent(`You review the commits ${fix.base_sha || 'before the fix'}..${head} in ${A.repo}: fixes of an independent review of the LICHOMAT stage 1 documents ${CHECKLIST} and ${NOTES}.

## The findings they answer (JSON)

${JSON.stringify(confirmed, null, 1)}

## The fixer's decisions (JSON)

${JSON.stringify(fixes.flatMap((f) => (f && f.decisions) || []), null, 1)}

${READ_ONLY}

Check, as a senior reviewer: every finding is decided with a reason; each change does what the finding asked and is true at HEAD (code, tests, report); every new check step has an Expected result with numbers and a Record line; the notes and the checklist still agree; nothing else in the documents became wrong; no code or test changed. Severities: critical = a wrong statement or check for the owner, or a broken file; important = must be fixed now; minor = polish. Return compliant=true only without critical or important issues.`, { label: `light:documents#${round}`, phase: 'Light review', schema: LIGHT, effort: 'high' })
  if (!light) return { stoppedAt: 'light review', confirmed, refuted, fixes }
  const blocking = (light.issues || []).filter((x) => x.severity === 'critical' || (x.severity === 'important' && round < LIGHT_ROUNDS))
  if (blocking.length === 0) break
  if (round === LIGHT_ROUNDS) return { stoppedAt: 'light review', reason: 'critical issues after the last round', confirmed, refuted, fixes, light }
  const again = await agent(`You fix the review findings of the commits ${fix.base_sha || ''}..${head} in ${A.repo} (the LICHOMAT stage 1 documents ${CHECKLIST} and ${NOTES}).

${RULES}

## Findings (JSON)

${JSON.stringify(blocking, null, 1)}

Documents only, as before: no code or test changes. Verify each finding against the files; fix each real one with the smallest change; run bash build/e4-checklist-check.sh and bash build/e4-notes-check.sh if they exist, then tools/check.sh (0 warnings); commit with an explicit file list. For a finding that is wrong, say why.`, { label: `light-fix:documents#${round}`, phase: 'Light review', schema: STEP, effort: 'high' })
  if (!again || again.status !== 'DONE') return { stoppedAt: 'light fix', confirmed, refuted, fixes, light, again }
  fixes.push(again)
  head = again.head_sha
}

return {
  done: true,
  head_sha: head,
  confirmed: confirmed.length,
  refuted: refuted.map((f) => ({ lens: f.lens, problem: f.problem, reason: f.reason })),
  decisions: fixes.flatMap((f) => (f && f.decisions) || []),
  questions: fixes.flatMap((f) => (f && f.questions) || []),
  leftovers: (light && light.issues) || [],
}
