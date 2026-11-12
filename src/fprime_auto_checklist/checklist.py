"""The F Prime Component Development Checklist v1.2.0 and its evidence map.

Every one of the 73 items names the automated evidence that decides it:

* ``check``  -- a component-checks script (``fprime_auto_checklist.checks``)
  identified by its id (R1..R9, D1..D10, I1..I4, U1..U5, C1..C3, SDD).  An
  optional ``pattern`` restricts a composite check to the failure messages
  that concern this item.
* ``codeql`` -- a CodeQL rule id from the vendored ``codeql/fprime-queries``
  pack (or the stock ``codeql/cpp-queries`` pack).
* ``review`` -- a residual judgement no tool can make; the item is handed
  to a human or LLM with the named review rule.
* ``human``  -- an action a person must perform (running on hardware); the
  evidence record is still script-checked.

Where a script covers an item that also has an LLM quality residue, the
script alone is considered sufficient (per the tool's charter).
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Tuple

CHECKLIST_VERSION = "1.2.0"


@dataclass(frozen=True)
class Evidence:
    kind: str  # "check" | "codeql" | "review" | "human"
    ref: str
    pattern: Optional[str] = None  # regex over check failure messages
    note: str = ""


@dataclass(frozen=True)
class Item:
    id: str
    section: str
    text: str
    evidence: Tuple[Evidence, ...]


def check(ref: str, pattern: Optional[str] = None, note: str = "") -> Evidence:
    return Evidence("check", ref, pattern, note)


def codeql(ref: str) -> Evidence:
    return Evidence("codeql", ref if ref.startswith("cpp/") else "cpp/fprime/" + ref)


def review(ref: str) -> Evidence:
    return Evidence("review", ref)


SECTIONS = (
    ("1", "Requirements"),
    ("2", "Design"),
    ("3", "Implementation"),
    ("4", "Unit Testing"),
    ("5", "Close-Out"),
)

#: Guidance handed over with every item that needs a human or LLM.
REVIEW_RULES = {
    "CL-1": (
        "ISR state before enable (3.21): for every ISR-named function (*_isr, "
        "*interrupt_handler*, *_irq*) find the code that enables its interrupt; every "
        "variable the ISR reads or writes must be assigned on a path that completes "
        "before that enable call."
    ),
    "CL-2": (
        "Interrupt enables cleared at init (3.23): in the constructor/init/configure/"
        "preamble, interrupt-enable bits must be explicitly cleared or masked before "
        "peripheral configuration, not assumed clear from reset."
    ),
    "CL-3": (
        "Units in names (3.33): every variable, member, parameter, telemetry channel and "
        "command argument carrying a physical quantity must encode its unit in the name "
        "(timeoutMs, angleRad, sizeBytes) or in an FPP annotation."
    ),
    "CL-4": (
        "Implementation matches requirements and design (3.38): for each requirement row "
        "locate the handler(s) that satisfy it and confirm the behaviour including each "
        "error path the SDD describes; report requirements with no implementing code and "
        "handler behaviour the SDD does not describe."
    ),
    "CL-5": (
        "Boundary tests (4.3): for each bounded quantity (array sizes, FW_ASSERT limits, "
        "configuration constants, command-argument ranges, queue depths) a unit test must "
        "exercise the limit value and one past it."
    ),
    "CL-6": (
        "Requirement coverage (1.1-1.6): read every requirement in the SDD and decide "
        "whether the aspect this item names (behavior, input data, output data, memory, "
        "hardware I/O, runtime performance) is constrained by at least one of them, or is "
        "explicitly declared not applicable with a rationale. Deciding which aspect a "
        "shall-statement constrains is a judgement; no column or keyword convention is "
        "imposed on the requirements table. R1-R6 (traceability of ports, commands, events, "
        "telemetry and parameters to requirements) and R8 (1.7) remain tool-checked."
    ),
    "HW-RUN": (
        "Run the flight deployment that instantiates this component on the target "
        "hardware and record platform, deployment, commit, date, result and tester in "
        "docs/hardware-run.md (check C2 validates the record)."
    ),
}

ITEMS: Tuple[Item, ...] = (
    # 1. Requirements
    Item(
        "1.1",
        "1",
        "Do requirements exist in the component SDD that define the component's expected operational behaviors?",
        (review("CL-6"),),
    ),
    Item(
        "1.2",
        "1",
        "Do requirements exist in the component SDD that define input data expected to be received and handled by the component?",
        (review("CL-6"),),
    ),
    Item(
        "1.3",
        "1",
        "Do requirements exist in the component SDD that define output data expected to be produced by the component?",
        (review("CL-6"),),
    ),
    Item(
        "1.4",
        "1",
        "Do requirements exist in the component SDD for all relevant memory usage constraints for the component?",
        (review("CL-6"),),
    ),
    Item(
        "1.5",
        "1",
        "Do requirements exist in the component SDD for all relevant hardware I/O usage constraints for the component?",
        (review("CL-6"),),
    ),
    Item(
        "1.6",
        "1",
        "Do requirements exist in the component SDD for all relevant runtime performance constraints for the component?",
        (review("CL-6"),),
    ),
    Item("1.7", "1", "Does every requirement specify a verification method?", (check("R8"),)),
    Item(
        "1.8",
        "1",
        "Are there any relevant external interface or functional requirements documented elsewhere such as ICDs/FDDs? If so, please specify in notes.",
        (check("D9", r"external interfaces"),),
    ),
    # 2. Design
    Item("2.1", "2", "Is the high level component functionality documented/described in the SDD?", (check("SDD"),)),
    Item(
        "2.2",
        "2",
        "Is the component block diagram documented in the SDD and consistent with the FPP model?",
        (check("D9", r"block diagram|no docs/sdd\.md"),),
    ),
    Item(
        "2.3",
        "2",
        "Is the component port table documented in the SDD and consistent with the FPP model?",
        (check("D7"),),
    ),
    Item("2.4", "2", "Are the component input port behaviors described in the SDD?", (check("D10"),)),
    Item("2.5", "2", "Are the component input port handlers defined?", (check("D1"),)),
    Item("2.6", "2", "Are the component command handlers defined?", (check("D2"),)),
    Item(
        "2.7",
        "2",
        "Are the component sequence diagrams documented?",
        (check("D9", r"sequence diagram|no docs/sdd\.md"),),
    ),
    Item(
        "2.8",
        "2",
        "Are component state machines and use-cases described in the SDD?",
        (check("D9", r"state machine|use-case|no docs/sdd\.md"),),
    ),
    Item("2.9", "2", "Are the state machine actions defined?", (check("D3"),)),
    Item(
        "2.10",
        "2",
        "Are component commands, telemetry, events and parameters documented in the SDD (and consistent with the FPP model)?",
        (check("D8", r"command|telemetry|event|parameter|no docs/sdd\.md"),),
    ),
    Item(
        "2.11",
        "2",
        "Are component data products documented and consistent with the FPP model?",
        (check("D8", r"data product|container|record|no docs/sdd\.md"),),
    ),
    Item("2.12", "2", "Are all data product containers assigned a default priority?", (check("D4"),)),
    Item(
        "2.13",
        "2",
        "Does each component port in the FPP model have a known/planned use-case?",
        (check("R6"), check("D10")),
    ),
    Item("2.14", "2", "Are port directions and types as expected?", (check("D7"),)),
    Item(
        "2.15",
        "2",
        "Is the component type as expected (passive, queued or active)?",
        (check("D9", r"component|no docs/sdd\.md"),),
    ),
    Item(
        "2.16", "2", "Are all the component requirements mapped to design description and/or artifacts?", (check("D5"),)
    ),
    Item("2.17", "2", "Does the component FPP compile and generate auto-code?", (check("D6"),)),
    # 3. Implementation
    Item(
        "3.1",
        "3",
        "Are all component source dependencies listed in the CMakeLists.txt file for the component?",
        (check("I1"),),
    ),
    Item("3.2", "3", "Does the component compile without warnings for all required targets?", (check("I2"),)),
    Item("3.3", "3", "Are all loops properly terminated/bounded?", (codeql("jpl-c/loop-bounds"),)),
    Item(
        "3.4",
        "3",
        "Has gating been provided for any potential cyclic error/status messages (e.g. log messages, events)?",
        (codeql("unthrottled-cyclic-event"),),
    ),
    Item(
        "3.5",
        "3",
        "Do all switch statements have a break after each case and terminate with a default case?",
        (codeql("switch-default"), codeql("switch-case-fallthrough")),
    ),
    Item(
        "3.6",
        "3",
        "Verify all non-empty case statements that fall through to the following case statement are explicitly commented as such",
        (codeql("switch-case-fallthrough"),),
    ),
    Item(
        "3.7",
        "3",
        "Are all variables locally defined unless global or specific visibility is required?",
        (codeql("global-mutable-variable"),),
    ),
    Item(
        "3.8",
        "3",
        "Are all class members defined appropriately using private, public and protected keywords?",
        (codeql("public-data-member"),),
    ),
    Item(
        "3.9",
        "3",
        "Verify that no floating point expressions are tested for equality/inequality",
        (codeql("float-equality"),),
    ),
    Item(
        "3.10",
        "3",
        "Is the precision of floating point numbers sufficient to ensure accuracy and floating-point equivalence is evaluated with margin (i.e. safe floating-point usage), and is not used in a loop control?",
        (codeql("float-narrowing"), codeql("float-loop-control"), codeql("float-equality")),
    ),
    Item(
        "3.11",
        "3",
        "Are all variables initialized before use?",
        (codeql("uninitialized-member"), codeql("cpp/uninitialized-local")),
    ),
    Item(
        "3.12",
        "3",
        "Are the priorities of all data products produced by the component configurable via command(s) or parameter(s)?",
        (check("I3"),),
    ),
    Item("3.13", "3", "Are all commands acknowledged with success/failure?", (codeql("command-response"),)),
    Item(
        "3.14",
        "3",
        "Do all validation failure cases for port and command handlers include a return statement to avoid unintended code execution beyond validation failures?",
        (codeql("validation-failure-return"),),
    ),
    Item(
        "3.15",
        "3",
        "Are all internal port interfaces invoked via the auto-coded internal port invoke() function?",
        (codeql("internal-port-invoke"),),
    ),
    Item(
        "3.16",
        "3",
        "All pointer arguments to functions are checked to be not nullptr using FW_ASSERT",
        (codeql("checking-parameter-values"),),
    ),
    Item(
        "3.17",
        "3",
        "Assumptions on software state are validated using FW_ASSERT.",
        (codeql("jpl-c/use-of-assertions-density"),),
    ),
    Item(
        "3.18",
        "3",
        "Assertions shall not be used to validate state that could be dependent on hardware or user input.",
        (codeql("assert-on-untrusted-input"),),
    ),
    Item("3.19", "3", "Verify all ISR code is free of floating point operations", (codeql("no-float-in-isr"),)),
    Item(
        "3.20", "3", "Has the ISR code been optimized to minimize length/execution time?", (codeql("isr-complexity"),)
    ),
    Item(
        "3.21",
        "3",
        "Verify any software state that's expected upon receive of an interrupt is configured prior to enabling/setting conditions that would allow the interrupt to trigger.",
        (review("CL-1"),),
    ),
    Item("3.22", "3", "Verify there are no blocking operations in ISRs", (codeql("no-blocking-in-isr"),)),
    Item(
        "3.23",
        "3",
        "Verify any relevant interrupt enables and interrupts are cleared during initialization and re-enabled selectively as needed.",
        (review("CL-2"),),
    ),
    Item("3.24", "3", "Are RateGroup schedule calls non-blocking?", (codeql("no-blocking-in-sched-handler"),)),
    Item(
        "3.25",
        "3",
        "Verify no synchronous file access is done within a critical region (e.g. SchedIn)",
        (codeql("no-file-access-in-critical"),),
    ),
    Item(
        "3.26",
        "3",
        "Verify that all loops and if/else statements are compound statements (i.e. always use brackets {})",
        (codeql("compound-statements"),),
    ),
    Item(
        "3.27",
        "3",
        "Verify that all divisions first check for small numbers (including zero)",
        (codeql("division-guard"), codeql("float-division-guard")),
    ),
    Item(
        "3.28",
        "3",
        "Verify all array accesses are checked to be within array index bounds",
        (codeql("unchecked-array-index"), codeql("checking-parameter-values")),
    ),
    Item(
        "3.29",
        "3",
        "Verify there are no function declarations within a function.",
        (codeql("no-function-decl-in-function"),),
    ),
    Item(
        "3.30",
        "3",
        "Verify that the three expressions in all for statements only concern flow control",
        (codeql("for-header-flow-control"),),
    ),
    Item(
        "3.31",
        "3",
        "Verify that there are no dependencies on signed integer overflow behavior",
        (codeql("signed-overflow"),),
    ),
    Item("3.32", "3", "Verify return values are used or voided otherwise.", (codeql("jpl-c/checking-return-values"),)),
    Item("3.33", "3", "Verify units are incorporated into variable name, where applicable", (review("CL-3"),)),
    Item(
        "3.34",
        "3",
        "Verify that all invariant numbers are in enums, static constexpr or #defines. Enums or static constexpr must be used unless #define is only way to specify constant.",
        (codeql("magic-numbers"), codeql("define-constant")),
    ),
    Item("3.35", "3", "Verify no use of printf", (codeql("no-printf"),)),
    Item(
        "3.36",
        "3",
        "Does the component use platform independent standard data types (e.g. I32, F32 etc.) rather than primitive data types (e.g. int, float etc.)?",
        (codeql("jpl-c/basic-int-types"),),
    ),
    Item("3.37", "3", "Does the component pass static analysis?", (check("I4"),)),
    Item(
        "3.38",
        "3",
        "Does the implementation address all component requirements and remains consistent with the documented design?",
        (review("CL-4"),),
    ),
    # 4. Unit Testing
    Item("4.1", "4", "Have all test output/results been published?", (check("U1"),)),
    Item(
        "4.2",
        "4",
        "Are all component unit test source dependencies listed in the CMakeLists.txt file for the component?",
        (check("U3"),),
    ),
    Item("4.3", "4", "Have input data ranges been tested for boundary conditions?", (review("CL-5"),)),
    Item("4.4", "4", "Do the unit tests pass a memory leak check?", (check("U4"),)),
    Item("4.5", "4", "Is there sufficient code coverage?", (check("U5"),)),
    # 5. Close-Out
    Item(
        "5.1",
        "5",
        "Verify no TBDs, TODOs or printfs in the code (no incomplete code).",
        (codeql("incomplete-code"), codeql("no-printf")),
    ),
    Item(
        "5.2",
        "5",
        "Verify that component can be instantiated, connected, and compiled along with the Topology for all applicable targets.",
        (check("C1"),),
    ),
    Item(
        "5.3",
        "5",
        "Verify that the flight deployment with component instantiation/connections runs successfully on a hardware platform. Indicate hardware platform used for checkout in Notes.",
        (Evidence("human", "HW-RUN"), check("C2")),
    ),
    Item(
        "5.4",
        "5",
        "Update the SDD to accurately reflect the final state of the component. Add any additional design detail, including any known idiosyncrasies.",
        (check("D7"), check("D8"), check("D9"), check("SDD")),
    ),
    Item("5.5", "5", "Validate that CI passes.", (check("C3"),)),
)

ITEMS_BY_ID = {item.id: item for item in ITEMS}


def codeql_rule_ids() -> Tuple[str, ...]:
    """Every CodeQL rule id the checklist relies on, in checklist order."""
    seen: dict = {}
    for item in ITEMS:
        for ev in item.evidence:
            if ev.kind == "codeql":
                seen.setdefault(ev.ref, None)
    return tuple(seen)


def check_ids() -> Tuple[str, ...]:
    seen: dict = {}
    for item in ITEMS:
        for ev in item.evidence:
            if ev.kind == "check":
                seen.setdefault(ev.ref, None)
    return tuple(seen)
