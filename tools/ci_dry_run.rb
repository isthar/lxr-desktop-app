#!/usr/bin/env ruby
# Dry run of the CI workflows on this machine, to check them without pushing
# anything: .github/workflows/build.yml for the jobs host-tests and firmware, and
# .github/workflows/emulator.yml for emu-release and emu-sanitize.
#
#   ruby tools/ci_dry_run.rb firmware                    # every run step of the job
#   ruby tools/ci_dry_run.rb host-tests
#   ruby tools/ci_dry_run.rb emu-release
#   ruby tools/ci_dry_run.rb emu-sanitize
#   ruby tools/ci_dry_run.rb firmware "Memory usage"     # one step
#
# It first checks what the spec fixes about the job's workflow (triggers, jobs,
# runner, toolchain and its checksum, sanitizers, artifacts). Then it runs the run
# steps of the job from the repository root the way GitHub does, with
# bash --noprofile --norc -eo pipefail and the workflow's env. Local differences
# for build.yml:
# - the Arm toolchain is the one in .toolchain/ (tools/env.sh), and the download
#   step is skipped, as on CI when the toolchain cache hits;
# - the host tests run with UBSan only, because AddressSanitizer binaries hang in
#   the macOS sandbox that Claude Code runs commands in;
# - the host tests build in build/ci-dry-run/host-tests, not in build/host-tests,
#   so that the job's -DSEQC_REQUIRE_ALL_TESTS=ON stays out of the cache that
#   tools/check.sh reuses.
# Local differences for emulator.yml:
# - the step "Fetch CMake and Ninja" is skipped: CMake and Ninja come from
#   .toolchain/ through tools/env.sh (tools/setup-toolchain.sh, the same pins);
# - the sanitizer job runs with UBSan only (address,undefined becomes undefined),
#   for the same reason as the host tests;
# - build/emu-release and build/emu-sanitize become build/ci-dry-run/emu-release
#   and build/ci-dry-run/emu-sanitize, apart from the builds of tools/check.sh;
# - GITHUB_SHA is git rev-parse HEAD, and GITHUB_PATH is build/ci-dry-run/path,
#   which nothing reads.
# The upload steps are not run (they are uses: steps). The job summary goes to
# build/ci-dry-run/summary.md, emptied at every run.
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
abort("usage: ruby tools/ci_dry_run.rb host-tests|firmware|emu-release|emu-sanitize [step name]") unless job_name

emulator = %w[emu-release emu-sanitize].include?(job_name)
wf = YAML.load_file(emulator ? ".github/workflows/emulator.yml" : ".github/workflows/build.yml")
on = wf["on"] || wf[true]	# YAML 1.1 reads the key "on" as true
jobs = wf.fetch("jobs")
steps = ->(name) { jobs.fetch(name, {}).fetch("steps", []) }
find_step = ->(job, name) { steps.call(job).find { |s| s["name"] == name } }
runs = ->(job) { steps.call(job).map { |s| s["run"].to_s }.join("\n") }
uploads = ->(job) { steps.call(job).select { |s| s["uses"].to_s.start_with?("actions/upload-artifact@") } }
# the pins of tools/setup-toolchain.sh
pin = ->(var) { File.read("tools/setup-toolchain.sh")[/^#{var}=(\S+)$/, 1] }
checks = emulator ? {
  "it runs on push and pull_request" =>
    on.is_a?(Hash) && on.key?("push") && on.key?("pull_request"),
  "the jobs are emu-release and emu-sanitize, on macos-15, with timeouts of 30 and 45 minutes" =>
    jobs.keys.sort == %w[emu-release emu-sanitize] && jobs.values.all? { |j| j["runs-on"] == "macos-15" } &&
    jobs["emu-release"]["timeout-minutes"] == 30 && jobs["emu-sanitize"]["timeout-minutes"] == 45,
  "DEVELOPER_DIR is the Command Line Tools" =>
    wf.fetch("env", {})["DEVELOPER_DIR"] == "/Library/Developer/CommandLineTools",
  "CMAKE_PKG, CMAKE_SHA256 and NINJA_SHA256 are the pins of tools/setup-toolchain.sh" =>
    !pin.call("CMAKE_PKG").nil? && !pin.call("CMAKE_SHA").nil? && !pin.call("NINJA_SHA").nil? &&
    wf["env"]["CMAKE_PKG"] == pin.call("CMAKE_PKG") && wf["env"]["CMAKE_SHA256"] == pin.call("CMAKE_SHA") &&
    wf["env"]["NINJA_SHA256"] == pin.call("NINJA_SHA"),
  "emu-release configures a Release build without sanitizers" =>
    runs.call("emu-release").match?(/-DCMAKE_BUILD_TYPE=Release -DLICHOMAT_EMU_SANITIZE=(\s|$)/),
  "emu-sanitize configures with ASan and UBSan" =>
    runs.call("emu-sanitize").match?(/-DLICHOMAT_EMU_SANITIZE=address,undefined(\s|$)/),
  "the two jobs fetch CMake and Ninja and record the toolchain with the same steps" =>
    ["Fetch CMake and Ninja", "Record the toolchain"].all? { |n|
      !find_step.call("emu-release", n).nil? && find_step.call("emu-release", n) == find_step.call("emu-sanitize", n) },
  "emu-sanitize builds without the app, emu-release with it" =>
    runs.call("emu-sanitize").match?(/-DLICHOMAT_EMU_APP=OFF(\s|$)/) && !runs.call("emu-release").include?("LICHOMAT_EMU_APP"),
  "emu-release uploads lichomat-render" =>
    uploads.call("emu-release").any? { |s| s["if"].nil? && s.fetch("with", {})["name"].to_s.start_with?("lichomat-render-") &&
      s.fetch("with", {})["if-no-files-found"] == "error" },
  "emu-release uploads LICHOMAT Emu" =>
    uploads.call("emu-release").any? { |s| s["if"].nil? && s.fetch("with", {})["name"].to_s.start_with?("lichomat-emu-") &&
      s.fetch("with", {})["if-no-files-found"] == "error" },
  "the otool step checks the app and its renderer" =>
    find_step.call("emu-release", "Check the linked libraries").to_h["run"].to_s.include?("LICHOMAT Emu.app/Contents/MacOS/lichomat-render"),
  "both jobs upload the golden outputs on a failure" =>
    %w[emu-release emu-sanitize].all? { |j| uploads.call(j).any? { |s| s["if"] == "failure()" &&
      s.fetch("with", {})["path"] == "build/#{j}/goldens-out" } },
} : {
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
FileUtils.mkdir_p("build/ci-dry-run")
env = wf.fetch("env").transform_values(&:to_s)
if emulator
  abort("no CMake or Ninja in .toolchain/: run tools/setup-toolchain.sh") unless
    system({ "PATH" => path }, "bash", "-c", "command -v cmake && command -v ninja", out: File::NULL)
  env["GITHUB_SHA"] = IO.popen(%w[git rev-parse HEAD], &:read).strip
  env["GITHUB_PATH"] = File.join(root, "build/ci-dry-run/path")
else
  abort("no Arm toolchain in .toolchain/: run tools/setup-toolchain.sh") if tc.to_s.empty?
  env["ARM_GNU_PKG"] = File.basename(File.dirname(tc))
end
env["GITHUB_WORKSPACE"] = root
env["GITHUB_STEP_SUMMARY"] = File.join(root, "build/ci-dry-run/summary.md")
File.write(env["GITHUB_STEP_SUMMARY"], "")	# the steps append to it, as on GitHub
env["PATH"] = path

ran = 0
steps.call(job_name).each do |step|
  next unless step["run"]
  name = step["name"]
  next if only && name != only
  if ["Download the Arm GNU Toolchain", "Fetch CMake and Ninja"].include?(name)
    puts "== skipped: #{name}"
    next
  end
  script = step["run"].gsub("-DSEQC_SANITIZE=address,undefined", "-DSEQC_SANITIZE=undefined")
                      .gsub("-DLICHOMAT_EMU_SANITIZE=address,undefined", "-DLICHOMAT_EMU_SANITIZE=undefined")
                      .gsub("build/host-tests", "build/ci-dry-run/host-tests")
                      .gsub("build/emu-release", "build/ci-dry-run/emu-release")
                      .gsub("build/emu-sanitize", "build/ci-dry-run/emu-sanitize")
  puts "== #{name}"
  ok = system(env, "bash", "--noprofile", "--norc", "-eo", "pipefail", "-c", script)
  abort("FAILED: #{name}") unless ok
  ran += 1
end
abort("no run step of job #{job_name}#{only ? " named #{only}" : ''}") if ran.zero?
puts "dry run of #{job_name}: #{ran} steps passed"
