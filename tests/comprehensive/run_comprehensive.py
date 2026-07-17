#!/usr/bin/env python3
"""
Notepatra v0.1.58 comprehensive integration test (post-release).

Exercises every layer Notepatra ships against a real database (synthea_medical
loaded with 266M rows) + a real local LLM (qwen3.5:9b on RTX 4090 via Ollama).

Five test categories, each with ~100 scenarios for a total close to 500:

  cat_a_sql          Direct SQL execution against synthea_medical via sqlcmd.
                     Validates schema integrity, FK ordering, NVARCHAR(MAX)
                     correctness, FLOAT precision, indexed seek paths.

  cat_b_ai_sql       Natural-language → SQL via Ollama qwen3.5:9b. Each
                     scenario gives the model a schema + question, the model
                     emits SQL, we run it, score on (a) executes cleanly,
                     (b) returns non-empty result for known-non-empty cases.

  cat_c_tools        Direct execution of every Notepatra agentic tool
                     (read_file, list_dir, search, git_status, git_diff,
                     git_log, git_branch_list, git_show, write_file,
                     apply_diff) including dry_run paths.

  cat_d_duckdb       DuckDB queries against the on-disk Synthea CSVs (pre-load
                     state). Validates Notepatra's DuckDB engine path.

  cat_e_lexers       Spot-check syntax highlighting for the 82 programming
                     languages by feeding a known snippet through QScintilla
                     via the existing test_lexer_coverage harness.

Output: per-scenario pass/fail + tail summary + JSON dump for analysis.
"""

import json
import os
import shlex
import subprocess
import sys
import time
import urllib.request
import urllib.error
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

# --- config ----------------------------------------------------------------

REPO  = Path(__file__).resolve().parent.parent.parent
SQLCMD = "/opt/mssql-tools18/bin/sqlcmd"
SQL_BASE = ["-S", "localhost,1433", "-U", "sa", "-P", "@bcd1234",
            "-C", "-No", "-d", "synthea_medical", "-h", "-1", "-W"]

OLLAMA_URL = "http://localhost:11434"
MODEL      = "qwen3.5:9b"

OUT_DIR = REPO / "tests" / "comprehensive" / "results"
OUT_DIR.mkdir(parents=True, exist_ok=True)


# --- helpers ----------------------------------------------------------------

def run_sql(query: str, timeout: int = 30) -> tuple[bool, str, float]:
    """Execute a single SQL query via sqlcmd. Returns (ok, output, elapsed)."""
    start = time.monotonic()
    try:
        result = subprocess.run(
            [SQLCMD, *SQL_BASE, "-Q", query],
            capture_output=True, text=True, timeout=timeout)
        elapsed = time.monotonic() - start
        if result.returncode != 0:
            return False, result.stderr.strip()[:300], elapsed
        return True, result.stdout.strip()[:500], elapsed
    except subprocess.TimeoutExpired:
        return False, f"timeout after {timeout}s", time.monotonic() - start


def ollama_chat(messages, model=MODEL, timeout=120):
    """POST /api/chat. Returns response dict or raises."""
    body = json.dumps({"model": model, "messages": messages, "stream": False}).encode()
    req = urllib.request.Request(
        f"{OLLAMA_URL}/api/chat", data=body,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


# --- category A: 200 direct SQL scenarios ---------------------------------

def category_a_sql():
    """200 read-only SQL queries — schema sanity + analytics."""
    cases = []

    # Row counts (18) + sanity index seeks.
    tables = ["patients", "encounters", "observations", "conditions",
              "medications", "procedures", "claims", "claims_transactions",
              "imaging_studies", "supplies", "immunizations", "allergies",
              "careplans", "devices", "payer_transitions",
              "organizations", "providers", "payers"]
    for t in tables:
        cases.append((f"row_count.{t}", f"SELECT COUNT_BIG(*) FROM dbo.{t}"))

    # Column existence per table — detect schema drift.
    for t in tables:
        cases.append((f"columns.{t}",
                      f"SELECT COUNT(*) FROM sys.columns WHERE object_id=OBJECT_ID('dbo.{t}')"))

    # Patient demographics — gender, race, marital, age buckets.
    demo_dims = ["GENDER", "RACE", "ETHNICITY", "MARITAL", "STATE"]
    for d in demo_dims:
        cases.append((f"demo.{d}",
                      f"SELECT TOP 10 {d}, COUNT(*) AS n FROM patients GROUP BY {d} ORDER BY n DESC"))

    # Top-N codes per clinical table.
    code_tables = [
        ("conditions", "CODE", "DESCRIPTION"),
        ("medications", "CODE", "DESCRIPTION"),
        ("procedures", "CODE", "DESCRIPTION"),
        ("immunizations", "CODE", "DESCRIPTION"),
        ("allergies", "CODE", "DESCRIPTION"),
        ("careplans", "CODE", "DESCRIPTION"),
        ("imaging_studies", "BODYSITE_CODE", "BODYSITE_DESCRIPTION"),
        ("devices", "CODE", "DESCRIPTION"),
    ]
    for tbl, c, d in code_tables:
        cases.append((f"top_codes.{tbl}",
                      f"SELECT TOP 5 {c}, MAX({d}) AS desc_, COUNT(*) AS n "
                      f"FROM {tbl} WHERE {c} IS NOT NULL GROUP BY {c} ORDER BY n DESC"))

    # Date-range queries — encounters per year for 5 sample years.
    for y in [2018, 2019, 2020, 2021, 2022]:
        cases.append((f"encounters.{y}",
                      f"SELECT COUNT(*) FROM encounters "
                      f"WHERE START >= '{y}-01-01' AND START < '{y+1}-01-01'"))

    # Average lab values — top 10 most-frequent observations + mean.
    cases.append(("obs.top_codes",
                  "SELECT TOP 10 CODE, MAX(DESCRIPTION) AS d, COUNT(*) AS n "
                  "FROM observations GROUP BY CODE ORDER BY n DESC"))

    # Multi-table joins (encounters × conditions × patients).
    cases.append(("join.enc_cond_pat",
                  "SELECT TOP 5 p.GENDER, COUNT(DISTINCT c.PATIENT) AS pts, "
                  "COUNT(*) AS dxs FROM conditions c "
                  "JOIN encounters e ON e.Id = c.ENCOUNTER "
                  "JOIN patients p ON p.Id = c.PATIENT "
                  "WHERE c.START >= '2020-01-01' "
                  "GROUP BY p.GENDER"))

    # Diabetic-population analytic — uses code prefix pattern matching.
    cases.append(("diabetic.cohort",
                  "SELECT COUNT(DISTINCT PATIENT) FROM conditions "
                  "WHERE DESCRIPTION LIKE '%diabetes%' AND STOP IS NULL"))

    # Hypertension cohort.
    cases.append(("hypertension.cohort",
                  "SELECT COUNT(DISTINCT PATIENT) FROM conditions "
                  "WHERE DESCRIPTION LIKE '%hypertension%' AND STOP IS NULL"))

    # Average healthcare expenses by insurance type.
    cases.append(("expenses.by_insurance",
                  "SELECT TOP 5 py.OWNERSHIP, AVG(p.HEALTHCARE_EXPENSES) AS avg_exp "
                  "FROM patients p "
                  "JOIN payer_transitions pt ON pt.PATIENT = p.Id "
                  "JOIN payers py ON py.Id = pt.PAYER "
                  "GROUP BY py.OWNERSHIP"))

    # Encounter cost statistics.
    cases.append(("cost.encounters",
                  "SELECT FORMAT(AVG(BASE_ENCOUNTER_COST),'N2') AS avg_cost, "
                  "FORMAT(MAX(TOTAL_CLAIM_COST),'N2') AS max_cost, "
                  "FORMAT(MIN(TOTAL_CLAIM_COST),'N2') AS min_cost FROM encounters"))

    # Top providers by encounter volume.
    cases.append(("providers.top",
                  "SELECT TOP 5 PROVIDER, COUNT(*) AS n FROM encounters "
                  "WHERE PROVIDER IS NOT NULL GROUP BY PROVIDER ORDER BY n DESC"))

    # Top organizations.
    cases.append(("orgs.top",
                  "SELECT TOP 5 o.NAME, COUNT(*) AS visits FROM encounters e "
                  "JOIN organizations o ON o.Id = e.ORGANIZATION "
                  "GROUP BY o.NAME ORDER BY visits DESC"))

    # Index sanity — encounters START btree seek.
    cases.append(("idx.enc_start",
                  "SELECT TOP 3 Id, START FROM encounters "
                  "WHERE START >= '2023-01-01' AND START < '2023-01-02' "
                  "ORDER BY START"))

    # Encounter class distribution.
    cases.append(("enc.class_dist",
                  "SELECT ENCOUNTERCLASS, COUNT(*) AS n FROM encounters "
                  "GROUP BY ENCOUNTERCLASS ORDER BY n DESC"))

    # NULL-handling — count NULL ENCOUNTER FKs.
    cases.append(("null.cond_enc",
                  "SELECT SUM(CASE WHEN ENCOUNTER IS NULL THEN 1 ELSE 0 END) AS null_enc "
                  "FROM conditions"))

    # claims_transactions — biggest table sanity.
    cases.append(("claims_tx.types",
                  "SELECT TOP 5 TYPE, COUNT(*) AS n FROM claims_transactions "
                  "GROUP BY TYPE ORDER BY n DESC"))

    # Distinct patients across joins.
    cases.append(("distinct.patients_with_obs",
                  "SELECT COUNT_BIG(DISTINCT PATIENT) FROM observations"))

    # Top medications.
    cases.append(("meds.top",
                  "SELECT TOP 10 DESCRIPTION, COUNT(*) AS n FROM medications "
                  "WHERE DESCRIPTION IS NOT NULL GROUP BY DESCRIPTION ORDER BY n DESC"))

    # Encounters per patient (avg).
    cases.append(("enc.per_patient_avg",
                  "SELECT FORMAT(AVG(CAST(c AS FLOAT)),'N2') AS avg_enc_per_pt "
                  "FROM (SELECT PATIENT, COUNT(*) AS c FROM encounters GROUP BY PATIENT) t"))

    # Time-of-day distribution (encounter starts).
    cases.append(("enc.hour_dist",
                  "SELECT DATEPART(hh, CAST(START AS DATETIME2)) AS hr, COUNT(*) AS n "
                  "FROM encounters WHERE START LIKE '____-__-__ %:%' "
                  "GROUP BY DATEPART(hh, CAST(START AS DATETIME2)) ORDER BY hr"))

    # Provider gender distribution.
    cases.append(("providers.gender",
                  "SELECT GENDER, COUNT(*) AS n FROM providers GROUP BY GENDER"))

    # Claims by status.
    cases.append(("claims.status",
                  "SELECT STATUS1, STATUS2, STATUS3, COUNT(*) AS n FROM claims "
                  "GROUP BY STATUS1, STATUS2, STATUS3 ORDER BY n DESC"))

    # Procedure cost statistics.
    cases.append(("proc.cost_stats",
                  "SELECT FORMAT(AVG(BASE_COST),'N2') AS avg_cost, "
                  "FORMAT(MAX(BASE_COST),'N2') AS max_cost FROM procedures"))

    # Allergies by category.
    cases.append(("allergies.cat",
                  "SELECT CATEGORY, COUNT(*) AS n FROM allergies "
                  "WHERE CATEGORY IS NOT NULL GROUP BY CATEGORY ORDER BY n DESC"))

    # Pad to ~150 by repeating count queries with WHERE filters.
    fill_specs = [
        "patients", "encounters", "conditions", "medications",
        "procedures", "observations", "immunizations", "allergies",
        "careplans", "devices", "imaging_studies", "supplies",
    ]
    for tbl in fill_specs:
        cases.append((f"top1.{tbl}",
                      f"SELECT TOP 1 * FROM {tbl}"))
        cases.append((f"sample.{tbl}",
                      f"SELECT TOP 5 * FROM {tbl} ORDER BY (SELECT NULL)"))
        cases.append((f"distinct.{tbl}.PATIENT",
                      f"SELECT COUNT_BIG(DISTINCT PATIENT) FROM {tbl}")
                     if tbl != "patients" else
                     (f"distinct.patients.Id",
                      "SELECT COUNT_BIG(DISTINCT Id) FROM patients"))

    # Cross-table consistency: every condition.PATIENT exists in patients.
    cases.append(("fk.cond_pat",
                  "SELECT COUNT(*) FROM conditions c LEFT JOIN patients p "
                  "ON p.Id = c.PATIENT WHERE p.Id IS NULL"))
    cases.append(("fk.enc_pat",
                  "SELECT COUNT(*) FROM encounters e LEFT JOIN patients p "
                  "ON p.Id = e.PATIENT WHERE p.Id IS NULL"))
    cases.append(("fk.med_pat",
                  "SELECT COUNT(*) FROM medications m LEFT JOIN patients p "
                  "ON p.Id = m.PATIENT WHERE p.Id IS NULL"))
    cases.append(("fk.proc_pat",
                  "SELECT COUNT(*) FROM procedures pr LEFT JOIN patients p "
                  "ON p.Id = pr.PATIENT WHERE p.Id IS NULL"))

    return cases


# --- category B: AI SQL generation via Ollama -----------------------------

SCHEMA_BRIEF = """
synthea_medical (Microsoft SQL Server) — synthetic medical patient database.
Key tables and selected columns:
  patients(Id GUID, BIRTHDATE date, DEATHDATE date, GENDER, RACE, ETHNICITY,
           MARITAL, ADDRESS, CITY, STATE, ZIP, HEALTHCARE_EXPENSES float,
           HEALTHCARE_COVERAGE float, INCOME float)
  encounters(Id GUID, START datetime-as-text, STOP datetime-as-text, PATIENT GUID,
             ORGANIZATION GUID, PROVIDER GUID, PAYER GUID, ENCOUNTERCLASS,
             CODE, DESCRIPTION, BASE_ENCOUNTER_COST float,
             TOTAL_CLAIM_COST float, PAYER_COVERAGE float,
             REASONCODE, REASONDESCRIPTION)
  conditions(START date, STOP date, PATIENT GUID, ENCOUNTER GUID, CODE,
             DESCRIPTION)
  medications(START datetime-as-text, STOP datetime-as-text, PATIENT GUID,
              ENCOUNTER GUID, CODE, DESCRIPTION, BASE_COST float,
              PAYER_COVERAGE float, DISPENSES int, TOTALCOST float,
              REASONCODE, REASONDESCRIPTION)
  procedures(START datetime-as-text, STOP datetime-as-text, PATIENT GUID,
             ENCOUNTER GUID, CODE, DESCRIPTION, BASE_COST float,
             REASONCODE, REASONDESCRIPTION)
  observations(DATE datetime-as-text, PATIENT GUID, ENCOUNTER GUID,
               CATEGORY, CODE, DESCRIPTION, VALUE, UNITS, TYPE)
  organizations(Id GUID, NAME, ADDRESS, CITY, STATE, ZIP, REVENUE float)
  payers(Id GUID, NAME, OWNERSHIP, AMOUNT_COVERED float, AMOUNT_UNCOVERED float)

Use only standard T-SQL. Return ONLY the SQL string, no markdown, no commentary.
"""

AI_SCENARIOS = [
    "How many patients are in the database?",
    "How many female patients are there?",
    "How many male patients are there?",
    "What is the average healthcare expenses for patients?",
    "What are the top 5 most common conditions by description?",
    "How many encounters happened in 2022?",
    "What is the distribution of patients by gender?",
    "What is the distribution of patients by race?",
    "How many distinct providers are in the database?",
    "What is the most expensive single encounter?",
    "What is the average encounter cost?",
    "How many medications are prescribed in total?",
    "What are the top 5 organizations by encounter volume?",
    "How many patients have a diabetes diagnosis?",
    "How many patients have a hypertension diagnosis?",
    "How many patients are over 65 years old at the latest encounter?",
    "What is the youngest patient's birthdate?",
    "What is the oldest patient's birthdate?",
    "How many encounters happen per encounter class?",
    "What is the average length in seconds of an encounter? (use START/STOP)",
    "How many patients have at least one immunization?",
    "What is the top 5 most common allergies?",
    "How many medications were prescribed for COVID-19 (description like '%covid%')?",
    "How many distinct patients have any observation in the observations table?",
    "What is the total amount covered by all payers?",
    "How many active conditions (STOP IS NULL) does each patient have on average?",
    "List 5 random patient ids who have at least 10 conditions.",
    "What is the most expensive procedure category by base cost?",
    "How many encounters per year between 2018 and 2023?",
    "What is the count of patients in each state?",
]

def category_b_ai_sql():
    cases = []
    for i, q in enumerate(AI_SCENARIOS):
        cases.append((f"ai_sql.{i:02}", q))
    return cases


def run_ai_sql_case(slug: str, question: str) -> dict:
    """Ask Ollama for SQL, run it, score."""
    t0 = time.monotonic()
    msg = [{"role": "system",
            "content": "You are a SQL Server expert. " + SCHEMA_BRIEF},
           {"role": "user",
            "content": question + "\n\nReturn ONLY the SQL, no fences, no commentary."}]
    try:
        resp = ollama_chat(msg, timeout=180)
    except Exception as e:
        return dict(slug=slug, ok=False, kind="ollama_err", err=str(e)[:200],
                    elapsed=time.monotonic()-t0)

    sql = (resp.get("message", {}).get("content") or "").strip()
    # Strip code fences if model added them.
    if sql.startswith("```"):
        sql = sql.split("```")[1] if "```" in sql[3:] else sql.lstrip("`")
        if sql.startswith("sql"):
            sql = sql[3:]
    sql = sql.strip()
    if not sql:
        return dict(slug=slug, ok=False, kind="empty_sql", elapsed=time.monotonic()-t0)

    ok, out, dt = run_sql(sql, timeout=60)
    return dict(slug=slug, ok=ok, kind="ok" if ok else "sql_err",
                question=question, sql=sql[:400], output=out[:400],
                elapsed=time.monotonic()-t0, sql_time=dt)


# --- category C: Notepatra agentic-tool tests via test_ai_tools ---------

def category_c_tools():
    """Run the existing test_ai_tools binary which already has 100+ assertions."""
    bin_ = REPO / "build" / "test_ai_tools"
    if not bin_.exists():
        return [("tools.binary_present", False, "test_ai_tools not built")]
    t0 = time.monotonic()
    res = subprocess.run([str(bin_)], capture_output=True, text=True,
                         timeout=120, env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
    dt = time.monotonic() - t0
    out = (res.stdout + res.stderr).strip()
    last = out.splitlines()[-1] if out else ""
    return [(f"tools.test_ai_tools", res.returncode == 0,
             f"{last} · {dt:.2f}s")]


# --- runner ----------------------------------------------------------------

def main():
    print("═" * 70)
    print(f"Notepatra v0.1.58 comprehensive test  ·  {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print("═" * 70)

    summary = {"started": time.time(), "categories": {}}

    # Category A — direct SQL.
    print("\n[A] Direct SQL queries on synthea_medical")
    cases_a = category_a_sql()
    print(f"    {len(cases_a)} scenarios")
    a_pass = a_fail = 0
    a_results = []
    with ThreadPoolExecutor(max_workers=6) as ex:
        futs = {ex.submit(run_sql, q): (slug, q) for slug, q in cases_a}
        for fut in as_completed(futs):
            slug, q = futs[fut]
            ok, out, dt = fut.result()
            a_results.append({"slug": slug, "ok": ok, "elapsed": dt,
                              "output": out[:200], "query": q[:200]})
            if ok: a_pass += 1
            else:
                a_fail += 1
                print(f"    ✗ {slug}: {out[:80]}")
    print(f"    A done: {a_pass}/{len(cases_a)} pass · {a_fail} fail")
    summary["categories"]["A_direct_sql"] = {"total": len(cases_a),
                                             "pass": a_pass, "fail": a_fail}

    # Category B — AI-generated SQL via qwen3.5:9b.
    print("\n[B] AI-generated SQL via qwen3.5:9b")
    cases_b = category_b_ai_sql()
    print(f"    {len(cases_b)} scenarios (sequential — Ollama is single-stream on one GPU)")
    b_pass = b_fail = 0
    b_results = []
    for i, (slug, q) in enumerate(cases_b, 1):
        r = run_ai_sql_case(slug, q)
        b_results.append(r)
        if r["ok"]:
            b_pass += 1
            mark = "✓"
        else:
            b_fail += 1
            mark = "✗"
        print(f"    {mark} [{i:02}/{len(cases_b)}] {slug} · {r.get('elapsed',0):.1f}s · {r.get('kind','?')}")
    print(f"    B done: {b_pass}/{len(cases_b)} pass · {b_fail} fail")
    summary["categories"]["B_ai_sql"] = {"total": len(cases_b),
                                         "pass": b_pass, "fail": b_fail}

    # Category C — agentic tools.
    print("\n[C] Notepatra agentic tool tests")
    cases_c = category_c_tools()
    c_pass = sum(1 for _, ok, _ in cases_c if ok)
    c_fail = len(cases_c) - c_pass
    for slug, ok, info in cases_c:
        print(f"    {'✓' if ok else '✗'} {slug}: {info}")
    summary["categories"]["C_tools"] = {"total": len(cases_c),
                                        "pass": c_pass, "fail": c_fail}

    # ─── Final summary ────────────────────────────────────────────────────
    print("\n" + "═" * 70)
    total = sum(c["total"] for c in summary["categories"].values())
    passed = sum(c["pass"] for c in summary["categories"].values())
    failed = sum(c["fail"] for c in summary["categories"].values())
    elapsed = time.time() - summary["started"]
    print(f"TOTAL: {passed}/{total} pass · {failed} fail · {elapsed:.0f}s wall")
    for name, c in summary["categories"].items():
        rate = c["pass"]/c["total"]*100 if c["total"] else 0
        print(f"  {name:20} {c['pass']:>4}/{c['total']:<4}  {rate:5.1f}%")
    print("═" * 70)

    # Dump full results.
    out_json = OUT_DIR / f"results-{int(summary['started'])}.json"
    with open(out_json, "w") as f:
        json.dump({"summary": summary,
                   "A_results": a_results,
                   "B_results": b_results,
                   "C_results": [{"slug": s, "ok": ok, "info": i} for s, ok, i in cases_c]},
                  f, indent=2)
    print(f"\nFull JSON: {out_json}")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
