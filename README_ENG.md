# NU Framework Query Language (.nuql) Specification

This document defines the formal specification for NUQL, a flexible, cluster-based query language designed for bidirectional data referencing and complex inheritance management.

---

## 1. Core Concepts

- **Bidirectional Referencing:** Supports both traditional key-value mapping and mutual referencing.
- **Inheritance Engine:** Features a prioritized inheritance system including "Prime" and "Local" overrides.
- **Editor Agnostic:** Human-readable text format designed to be accessible from any editing environment.

---

## 2. Reference Pairs

Data is stored in pairs within clusters.

- **One-way Reference (`A = B`):** A standard key-to-value lookup.
- **Two-way Reference (`A == B`):** Both sides can act as a key to reference the other.
- **Constraint:** The left side must be a single value, whereas the right side can contain multiple values.
- **Collision Policy:** In the event of duplicate keys, the topmost definition (geographically higher in the file) takes precedence.

---

## 3. Clusters (`clu`)

A Cluster is a collection of data defined by `clu name << ... >>`.

- **Inheritance:** Defined as `clu name (parent1, parent2)`.
- **Conflict Resolution:**
  - The inheriting cluster (child) always takes precedence over its parents.
  - Among multiple parents, the leftmost (first defined) takes precedence.
  - Geographically, a cluster cannot inherit from a cluster defined below it.

---

## 4. Prime Clusters (`*`)

Prime clusters represent high-priority data blocks.

- **Global Prime (`*clu`):** Defined with a leading `*`. In any inheritance conflict, the Prime Cluster takes precedence over standard clusters.
- **Local Prime Inheritance:** A standard cluster can be treated as a Prime Cluster only within a specific inheritance scope using the syntax `clu name (*parent1)`.
- **Conflict between Primes:** If multiple Prime clusters conflict, the first one inherited takes precedence.

---

## 5. Protected Data (`!`)

- **Definition:** Prefixed with `!`, e.g., `! Key = Value`.
- **Behavior:** Protected data cannot be overwritten by any inheritance-based values.
- **Internal Priority:** If two protected entries exist within the same cluster, the topmost rule applies.

---

## 6. Inclusion and External References

- **External Inclusion:** `<include path>` allows a cluster to inherit from an external file.
- **Pending Inclusion:**
  - `<append path>`: Merges external content at the end of the local file.
  - `<prepend path>`: Merges external content at the beginning of the local file (giving it potential priority).

---

## 7. Meta Clusters (`meta` / `wmeta`)

Special clusters that are automatically inherited by all other clusters.

- **Meta (`meta`):** Only one per file. Absolute priority over all data, including protected entries.
- **Weak Meta (`wmeta`):** Multiple allowed. Can be excluded by individual clusters using the `rejected` keyword.
- **Scope:** Meta clusters cannot be referenced or accessed from external files.

---

## 8. Branch Clusters (`::`)

A cluster can define internal variants called Branches.

- **Definition:** `::name <~~ ... ~~>`.
- **Differential Inheritance:** Branches within the same cluster inherit from each other. In conflicts, the current branch's local data always wins.
- **Restriction:** Clusters containing branches cannot be inherited by other clusters.

---

## 9. Header Clusters (`&`)

- **Definition:** `&clu name << Key1=; Key2==; >>`.
- **Behavior:** Defines keys without values. Values must be injected via a program. Until then, they return `None`.
- **Restriction:** Header clusters cannot inherit from other clusters.

---

## 10. List Parse (L-Parse)

A query mode that returns all conflicting values as a prioritized list instead of a single value.

- **Explore Scope:** The API can restrict how deep the search goes (e.g., local-only or specific inheritance levels).

---

## 11. Data Types and Groups

- **One-way Reference:** Supports `string` and `identifier` (`#id`).
- **Two-way Reference:** Supports `string`, `id`, `integer` (`@`), `float` (`@`), `bool` (`%true`), `set` (`s{}`), `JSON` (`j{}`), `regex` (`r{}`), and `fstring` (`%x%`).
- **Groups:** `$name$ ... $$`. A non-readable logical grouping for batch processing commands.
- **Sideway (`/`):** Marks data or groups as read-only (immutable).

---

## 12. Syntax Requirements

- **Formatting:** Whitespace, tabs, and newlines are ignored.
- **Separators:** Underscores (`_`) are ignored (used for readability).
- **Case Sensitivity:** Defaults to insensitive. Use `<--NUQL 1.0 cc-->` at the file header to enable Case Sensitivity.
- **Comments:** Wrapped in `|++ comment ++|`.
- **Nesting:** No cluster nesting allowed.
