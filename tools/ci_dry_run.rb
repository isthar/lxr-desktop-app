#!/usr/bin/env ruby
# Dry run of the CI workflow (.github/workflows/build.yml) on this machine, to
# check it without pushing anything:
#
#   ruby tools/ci_dry_run.rb firmware                    # every run step of the job
#   ruby tools/ci_dry_run.rb host-tests
#   ruby tools/ci_dry_run.rb firmware "Memory usage"     # one step
#
# It first checks what the spec fixes about the workflow (triggers, jobs, runner,
# toolchain and its checksum, sanitizers, artifacts). Then it runs the run steps of
# the job from the repository root the way GitHub does, with
# bash --noprofile --norc -eo pipefail and the workflow's env. Local differences:
# - the Arm toolchain is the one in .toolchain/ (tools/env.sh), and the download
#   step is skipped, as on CI when the toolchain cache hits;
# - the host tests run with UBSan only, because AddressSanitizer binaries hang in
#   the macOS sandbox that Claude Code runs commands in.
# The job summary goes to build/ci-dry-run/summary.md.
#
# Copyright (C) 2026 Tomasz Krzywicki
# SPDX-License-Identifier: GPL-3.0-only
#
# Licensed under the GNU General Public License version 3, with the additional
# permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
# the repository.
require "fileutils"
require "yaml"

$stdout.sync = true	# keep this script's lines in order with the output of the steps

root = File.expand_path("..", __dir__)
Dir.chdir(root)
job_name, only = ARGV
abort("usage: ruby tools/ci_dry_run.rb host-tests|firmware [step name]") unless job_name

wf = YAML.load_file(".github/workflows/build.yml")
on = wf["on"] || wf[true]	# YAML 1.1 reads the key "on" as true
jobs = wf.fetch("jobs")
steps = ->(name) { jobs.fetch(name, {}).fetch("steps", []) }
checks = {
  "it runs on push and pull_request" =>
    on.is_a?(Hash) && on.key?("push") && on.key?("pull_request"),
  "the jobs are host-tests and firmware, on ubuntu-latest" =>
    jobs.keys.sort == %w[firmware host-tests] && jobs.values.all? { |j| j["runs-on"] == "ubuntu-latest" },
  "the toolchain is Arm GNU 14.3.rel1 for x86_64 Linux, with its SHA-256" =>
    wf["env"]["ARM_GNU_PKG"] == "arm-gnu-toolchain-14.3.rel1-x86_64-arm-none-eabi" &&
    wf["env"]["ARM_GNU_SHA256"] == "8f6903f8ceb084d9227b9ef991490413014d991874a1e34074443c2a72b14dbd",
  "the toolchain is cached" =>
    steps.call("firmware").any? { |s| s["uses"].to_s.start_with?("actions/cache/restore@") } &&
    steps.call("firmware").any? { |s| s["uses"].to_s.start_with?("actions/cache/save@") },
  "the host tests use ASan and UBSan and need every test file" =>
    steps.call("host-tests").any? { |s| s["run"].to_s.include?("-DSEQC_SANITIZE=address,undefined -DSEQC_REQUIRE_ALL_TESTS=ON") },
  "the firmware artifact holds the .img, .elf and .map" =>
    steps.call("firmware").any? { |s| s["uses"].to_s.start_with?("actions/upload-artifact@") &&
      %w[img elf map].all? { |e| s.fetch("with", {})["path"].to_s.include?("build/release/*.#{e}") } },
}
checks.each { |what, ok| puts "#{ok ? 'ok' : 'FAILED'}: #{what}" }
abort("the workflow check failed") unless checks.values.all?

tc, path = IO.popen(["bash", "-c", 'source tools/env.sh >/dev/null 2>&1; printf "%s\n%s" "$ARM_TOOLCHAIN_DIR" "$PATH"'], &:read).split("\n", 2)
abort("no Arm toolchain in .toolchain/: run tools/setup-toolchain.sh") if tc.to_s.empty?
FileUtils.mkdir_p("build/ci-dry-run")
env = wf.fetch("env").transform_values(&:to_s)
env["ARM_GNU_PKG"] = File.basename(File.dirname(tc))
env["GITHUB_WORKSPACE"] = root
env["GITHUB_STEP_SUMMARY"] = File.join(root, "build/ci-dry-run/summary.md")
env["PATH"] = path

ran = 0
steps.call(job_name).each do |step|
  next unless step["run"]
  name = step["name"]
  next if only && name != only
  if name == "Download the Arm GNU Toolchain"
    puts "== skipped: #{name}"
    next
  end
  script = step["run"].gsub("-DSEQC_SANITIZE=address,undefined", "-DSEQC_SANITIZE=undefined")
  puts "== #{name}"
  ok = system(env, "bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", script)
  abort("FAILED: #{name}") unless ok
  ran += 1
end
abort("no run step of job #{job_name}#{only ? " named #{only}" : ''}") if ran.zero?
puts "dry run of #{job_name}: #{ran} steps passed"
