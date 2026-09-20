# Common Agent Contract

Every agent role follows this contract.

## 1. Authority order

When instructions conflict, use this order:

1. explicit active task / owner instruction;
2. current master specification or accepted issue;
3. architecture invariants and compatibility contracts;
4. repository tests and CI policy;
5. role-specific guidance;
6. implementation preference.

An agent must not reinterpret a lower-priority convenience as permission to violate a higher-priority contract.

## 2. Before editing

Record internally:

- task objective;
- acceptance criteria;
- affected files/modules;
- contracts likely touched;
- assumptions;
- required tests;
- whether manual acceptance is needed.

If the repository state conflicts with the task, report the conflict instead of guessing.

## 3. Critical contracts

Treat these as architecture-critical when present:

- public API / ABI;
- persistent state/schema;
- serialization;
- migrations;
- identifiers;
- plugin/application identity;
- host automation IDs;
- database schema;
- protocol/message schema;
- security/auth model;
- realtime/threading model;
- ownership/lifecycle model;
- routing;
- deterministic/reproducibility contracts;
- resource budgets;
- package/install/update behaviour.

Changes to a critical contract require explicit disclosure and Architecture Guard review.

## 4. Forbidden shortcuts

Agents must not:

- delete/disable/weaken tests to get green CI;
- hide a regression behind a tolerance increase without justification;
- silently change stable IDs or serialized fields;
- add fake production UI controls;
- substitute mock behaviour for production behaviour without marking it as prototype-only;
- claim a manual acceptance gate passed without the owner actually performing it;
- add broad fallbacks that hide missing/incompatible modules or failed migrations;
- introduce blocking I/O, locks or unbounded allocation into a realtime path;
- merge red required CI;
- rewrite unrelated architecture while fixing a local problem.

## 5. Fan-out rules

Good fan-out examples:

- implementation + independent test authoring;
- UI component work + screenshot/visual audit;
- migration audit + documentation;
- performance profiling + unrelated CI packaging.

Bad fan-out examples:

- two agents editing the same core file;
- one agent defining an API while another implements against an API that is not frozen;
- parallel state-schema edits;
- parallel migrations of the same data;
- independent architectural rewrites.

The Lead owns integration and may serialize dependent work.

## 6. Common Agent Report

Every worker returns:

```yaml
agent_report:
  role: <role>
  task: <one-line objective>
  result: PASS | PARTIAL | BLOCKED | FAIL

  files_changed:
    - <path>

  contracts_touched:
    - <contract or "none">

  assumptions:
    - <assumption or "none">

  implementation:
    - <important change>

  tests_added:
    - <test or "none">

  verification:
    - command: <command>
      result: PASS | FAIL | NOT_RUN
      notes: <optional>

  architecture_changes:
    - <change or "none">

  migration_impact:
    - <impact or "none">

  performance_impact:
    - <impact or "not measured">

  security_impact:
    - <impact or "none">

  manual_acceptance_required:
    - <gate or "none">

  unresolved:
    - <item or "none">

  risks:
    - <risk or "none">
```

Do not replace this with vague prose such as "everything looks good."

## 7. Definition of done

A task is done only when:

- the requested behaviour exists;
- required tests pass;
- relevant regression gates pass;
- architecture invariants still hold;
- compatibility/migration impact is addressed;
- documentation/spec is synchronized when required;
- no known blocker is hidden;
- manual acceptance is clearly separated from automated verification.

Compilation alone is never sufficient evidence of completion for a behavioural feature.
