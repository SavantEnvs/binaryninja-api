#!/usr/bin/env bash
#
# mayhem/test.sh — RUN this repo's OWN functional test suite (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Known-answer tests over the core-free disassemblers built by mayhem/build.sh (normal flags):
#  * armv7: every (bytes, expected text) case in upstream's arch/armv7/armv7_disasm/test.py
#  * mips / thumb2 / powerpc: golden decodes in mayhem/tests/*.golden (input<TAB>expected output)
#  * demangle_gnu3 / demangle_msvc: golden demangled names through the plugin callbacks
B="$SRC/mayhem/build"
for t in armv7_kat mips_kat thumb2_kat powerpc_kat demangle_gnu3_kat demangle_msvc_kat; do
  [ -x "$B/$t" ] || { echo "missing $B/$t — build.sh did not produce it" >&2; emit_ctrf kat 0 1; exit 1; }
done
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

# armv7: input lines "<insword hex> <addr hex>", expected lines alongside
python3 -I - "$SRC/arch/armv7/armv7_disasm/test.py" "$work" "$SRC/mayhem/tests/armv7_stale.txt" <<'PY'
import ast, struct, sys
stale = set(tuple(l.rstrip('\n').split('\t', 1)) for l in open(sys.argv[3]) if not l.startswith('#'))
src = open(sys.argv[1]).read()
start = src.index('test_cases = (') + len('test_cases = ')
cases = ast.literal_eval(src[start:src.index('\n)\n', start) + 2])
with open(sys.argv[2] + '/armv7.in', 'w') as i, open(sys.argv[2] + '/armv7.exp', 'w') as e:
    skipped = 0
    for data, exp, opts in cases:
        if (data.hex(), exp) in stale:
            skipped += 1
            continue
        i.write('%08x %x\n' % (struct.unpack('<I', data)[0], opts.get('addr', 0)))
        e.write(exp + '\n')
open(sys.argv[2] + '/armv7.skipped', 'w').write('%d\n' % skipped)
PY
"$B/armv7_kat" < "$work/armv7.in" | sed -E 's/[[:space:]]+/ /g' > "$work/armv7.out" || true

for g in mips thumb2 powerpc demangle_gnu3 demangle_msvc; do
  cut -f1 "$SRC/mayhem/tests/$g.golden" > "$work/$g.in"
  cut -f2 "$SRC/mayhem/tests/$g.golden" > "$work/$g.exp"
  "$B/${g}_kat" < "$work/$g.in" | sed -E 's/[[:space:]]+/ /g; s/ $//' > "$work/$g.out" || true
done

passed=0; failed=0
for g in armv7 mips thumb2 powerpc demangle_gnu3 demangle_msvc; do
  n=$(wc -l < "$work/$g.exp")
  ok=$(paste "$work/$g.exp" "$work/$g.out" | awk -F '\t' '$1==$2{c++} END{print c+0}')
  # a line missing from the output (short or empty output) counts as a failure
  passed=$((passed + ok)); failed=$((failed + n - ok))
  echo "$g: $ok/$n known answers match"
  if [ "$ok" -ne "$n" ]; then
    diff <(head -n "$n" "$work/$g.exp") "$work/$g.out" | head -10
  fi
done
skipped=$(cat "$work/armv7.skipped" 2>/dev/null || echo 0)
emit_ctrf "disasm-kat" "$passed" "$failed" "$skipped"
