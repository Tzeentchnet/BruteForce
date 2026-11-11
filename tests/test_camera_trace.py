import json
import os
from pathlib import Path
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
CAMERA_EXE = os.environ.get("BRUTEFORCE_TEST_CAMERA_TRACE")
PREFIX = "[CAMERA-TRACE] "
SETTINGS = (
    "BRUTEFORCE_CAMERA_TRACE",
    "BRUTEFORCE_CAMERA_TRACE_LIMIT",
    "BRUTEFORCE_CAMERA_TRACE_EVERY",
)


@pytest.fixture
def native():
    if not CAMERA_EXE:
        pytest.skip("set BRUTEFORCE_TEST_CAMERA_TRACE to the native camera trace fixture")
    return CAMERA_EXE


def run_camera(executable, mode="direct-be", *, switch=None, limit=None, every=None):
    env = os.environ.copy()
    for name in SETTINGS:
        env.pop(name, None)
    for name, value in zip(SETTINGS, (switch, limit, every)):
        if value is not None:
            env[name] = str(value)
    return subprocess.run(
        [executable, mode], capture_output=True, text=True, check=False, timeout=15, env=env
    )


def records(result):
    assert result.returncode == 0, result.stderr
    lines = result.stderr.splitlines()
    assert all(line.startswith(PREFIX) for line in lines), result.stderr
    return [json.loads(line[len(PREFIX):]) for line in lines]


def matrix(first):
    return [f"{first + i:08X}" for i in range(16)]


@pytest.mark.parametrize("switch", [None, "", "0"])
@pytest.mark.parametrize("mode", ["direct-be", "guard-both", "guard-matrix", "guard-state"])
def test_disabled_trace_preserves_state_without_reading_guest_memory(native, mode, switch):
    assert records(run_camera(native, mode, switch=switch)) == []


@pytest.mark.parametrize(
    ("mode", "caller", "return_va", "parent", "owner_source", "view_scope"),
    [
        ("direct-be", "0006BE90", "0006BFA2", "00112233", "edi",
         "caller_context_candidate"),
        ("indirect-d", "0006D240", "0006D291", "00223344", "esi_minus_0x48",
         "global_context_only"),
    ],
)
def test_trace_records_both_callers_and_preserves_full_state(
    native, mode, caller, return_va, parent, owner_source, view_scope
):
    [record] = records(run_camera(native, mode, switch="1"))
    assert record["version"] == 1
    assert record["seq"] == record["call"] == 1
    assert record["t_us"] > 0 and record["thread"] > 0
    assert (record["caller_va"], record["return_va"], record["parent_return_va"]) == (
        caller, return_va, parent
    )
    assert record["matrix_va"] == record["entry_regs"]["ecx"] == "00376000"
    assert record["args"] == {
        "fov_bits": "42700000",
        "aspect_bits": "3FAAAAAB",
        "near_bits": "3F800000",
        "far_bits": "447A0000",
    }
    assert record["input_matrix_bits"] == matrix(0x3F000000)
    assert record["output_matrix_bits"] == matrix(0x40000000)
    assert record["owner"] == {
        "source": owner_source,
        "candidate_va": "00371000",
        "candidate_class_vtable": "00372000",
        "candidate_interface_vtable": "00372100",
        "candidate_state_va": "00374000",
        "hyp_fov_h_bits": "41F00000",
        "hyp_fov_v_bits": "41C00000",
        "hyp_tangent_h_bits": "3F000000",
        "hyp_tangent_v_bits": "3EC00000",
        "hyp_aspect_bits": "3FAAAAAB",
    }
    assert record["viewport"] == {
        "scope": view_scope,
        "manager_va": "00375000",
        "manager_vtable": "00373000",
        "rect_getter_va": "00067D60",
        "enable_getter_va": "00067D30",
        "enabled_candidate": True,
        "rect_candidate_bits": ["00000000", "00000000", "3F800000", "3F800000"],
    }


def test_unknown_caller_does_not_invent_an_owner(native):
    [record] = records(run_camera(native, "unknown", switch="1"))
    assert record["return_va"] == "00123456"
    assert record["caller_va"] is None and record["parent_return_va"] is None
    assert record["owner"]["source"] == "unknown"
    assert record["owner"]["candidate_va"] is None
    assert record["owner"]["candidate_state_va"] is None
    assert record["viewport"]["scope"] == "global_context_only"


def test_high_guest_alias_is_read_safely(native):
    [record] = records(run_camera(native, "high", switch="1"))
    assert record["matrix_va"] == "80001000"
    assert record["input_matrix_bits"] == matrix(0x3F000000)
    assert record["output_matrix_bits"] == matrix(0x40000000)
    assert record["owner"]["candidate_state_va"] == "80002000"
    assert record["owner"]["hyp_tangent_v_bits"] == "3EC00000"


def test_nonfinite_float_bits_are_preserved_exactly_in_json(native):
    [record] = records(run_camera(native, "special-floats", switch="1"))
    assert record["args"]["fov_bits"] == "7FC12345"
    assert record["args"]["aspect_bits"] == "FF800000"
    assert record["input_matrix_bits"][0] == "7FC54321"
    assert record["owner"]["hyp_tangent_h_bits"] == "7F800000"


@pytest.mark.parametrize("mode", ["unmapped-matrix", "partial-matrix", "outside-matrix",
                                  "guard-matrix"])
def test_unreadable_matrix_is_null_without_changing_execution(native, mode):
    [record] = records(run_camera(native, mode, switch="1"))
    assert record["input_matrix_bits"] is None
    assert record["output_matrix_bits"] is None
    assert record["args"]["aspect_bits"] == "3FAAAAAB"
    assert record["owner"]["hyp_tangent_h_bits"] == "3F000000"


@pytest.mark.parametrize("mode", ["guard-stack", "guard-both", "wrap-stack"])
def test_unreadable_stack_never_invents_a_caller_or_arguments(native, mode):
    [record] = records(run_camera(native, mode, switch="1"))
    assert record["return_va"] is None and record["caller_va"] is None
    assert all(value is None for value in record["args"].values())
    assert record["owner"]["source"] == "unknown"


def test_partial_stack_and_state_report_each_readable_field(native):
    [stack] = records(run_camera(native, "partial-stack", switch="1"))
    assert stack["return_va"] == "0006BFA2"
    assert all(value is None for value in stack["args"].values())
    [state] = records(run_camera(native, "partial-state", switch="1"))
    assert state["owner"]["hyp_fov_h_bits"] == "41F00000"
    assert state["owner"]["hyp_tangent_h_bits"] == "3F000000"
    assert state["owner"]["hyp_tangent_v_bits"] is None
    assert state["owner"]["hyp_aspect_bits"] is None


def test_invalid_owner_and_guarded_state_are_not_dereferenced(native):
    [invalid] = records(run_camera(native, "invalid-owner", switch="1"))
    assert invalid["owner"]["candidate_va"] == "FFFFFFF0"
    assert invalid["owner"]["candidate_class_vtable"] is None
    assert invalid["owner"]["candidate_state_va"] is None
    [guarded] = records(run_camera(native, "guard-state", switch="1"))
    assert guarded["owner"]["candidate_state_va"] == "0037B000"
    assert guarded["owner"]["hyp_tangent_h_bits"] is None
    assert guarded["owner"]["hyp_aspect_bits"] is None


@pytest.mark.parametrize("mode", ["guard-viewport", "other-getter"])
def test_viewport_candidate_requires_known_getter_and_accessible_memory(native, mode):
    [record] = records(run_camera(native, mode, switch="1"))
    view = record["viewport"]
    assert view["manager_va"] == "0037AFDC"
    assert view["rect_candidate_bits"] is None
    assert view["enabled_candidate"] is True
    assert view["rect_getter_va"] == (
        "00067D60" if mode == "guard-viewport" else "00012345"
    )


def test_budget_is_bounded_and_exhaustion_stops_guest_reads(native):
    events = records(run_camera(native, "many", switch="1", limit=3))
    assert [event["seq"] for event in events] == [1, 2, 3]
    assert [event["call"] for event in events] == [1, 2, 3]
    events = records(run_camera(native, "after-cap-guard", switch="1", limit=1))
    assert len(events) == 1


def test_default_budget_is_bounded(native):
    events = records(run_camera(native, "many-default", switch="1"))
    assert len(events) == 256
    assert [event["seq"] for event in events] == list(range(1, 257))
    assert [event["call"] for event in events] == list(range(1, 257))


def test_sampling_is_bounded_and_unsampled_calls_do_not_read_guest_memory(native):
    events = records(run_camera(native, "many", switch="1", limit=10, every=3))
    assert [event["seq"] for event in events] == [1, 2, 3, 4]
    assert [event["call"] for event in events] == [1, 4, 7, 10]
    events = records(run_camera(native, "sample-gap-guard", switch="1", limit=10, every=2))
    assert len(events) == 1 and events[0]["call"] == 1


def test_concurrent_budget_produces_complete_sequenced_records(native):
    events = records(run_camera(native, "concurrent", switch="1", limit=7))
    assert [event["seq"] for event in events] == list(range(1, 8))
    assert sorted(event["call"] for event in events) == list(range(1, 8))
    assert all(event["output_matrix_bits"] == matrix(0x40000000) for event in events)
    assert all(event["owner"]["candidate_class_vtable"] == "00372000" for event in events)


@pytest.mark.parametrize(
    ("switch", "limit", "every", "message"),
    [
        ("2", None, None, "BRUTEFORCE_CAMERA_TRACE must be 0 or 1"),
        ("true", None, None, "BRUTEFORCE_CAMERA_TRACE must be 0 or 1"),
        ("01", None, None, "BRUTEFORCE_CAMERA_TRACE must be 0 or 1"),
        ("1", "", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("1", "0", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("1", "-1", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("1", "+1", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("1", "1x", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("1", "4097", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must be an integer in 1..4096"),
        ("0", "999999999999999999999", None, "BRUTEFORCE_CAMERA_TRACE_LIMIT must"),
        ("1", None, "", "BRUTEFORCE_CAMERA_TRACE_EVERY must be an integer in 1..1000000"),
        ("1", None, "0", "BRUTEFORCE_CAMERA_TRACE_EVERY must be an integer in 1..1000000"),
        ("1", None, "1000001", "BRUTEFORCE_CAMERA_TRACE_EVERY must be an integer in 1..1000000"),
        ("0", None, " 3", "BRUTEFORCE_CAMERA_TRACE_EVERY must"),
    ],
)
def test_invalid_settings_are_rejected_explicitly(native, switch, limit, every, message):
    result = run_camera(native, switch=switch, limit=limit, every=every)
    assert result.returncode != 0
    assert message in result.stderr


def test_maximum_settings_are_accepted(native):
    events = records(run_camera(native, switch="1", limit=4096, every=1000000))
    assert len(events) == 1 and events[0]["call"] == 1


@pytest.mark.skipif(
    not (ROOT / ".deps" / "xboxrecomp" / "tools" / "recomp").is_dir(),
    reason="run tools/toolkit.py to install the recompiler",
)
def test_codegen_discovers_wrapper_and_routes_direct_and_indirect_calls(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / ".deps" / "xboxrecomp"))
    from tools.recomp.__main__ import _wrap_generated_bodies
    from tools.recomp.disasm import Instruction
    from tools.recomp.lifter import Lifter
    from tools.recomp.manual_scan import scan
    from tools.recomp import translator

    target = 0x0006EE30
    skipped, wrapped, _ = scan(str(ROOT / "src" / "recomp_manual.c"))
    assert target in skipped and target in wrapped
    info = {"name": "sub_0006EE30"}
    func_db = {target: info}

    class FakeTranslator:
        def __init__(self):
            self.owned_function_starts = set()
            self.lifter = Lifter(func_db=func_db)

        def translate_function(self, addr, function):
            return f"void {function['name']}(void) {{}}"

    batch = translator.BatchTranslator.__new__(translator.BatchTranslator)
    batch.func_db = func_db
    batch.translator = FakeTranslator()
    functions, manual = _wrap_generated_bodies(batch, [(target, info)], skipped, {target})
    insn = Instruction(0x0006BF9D, 5, "call", "0x6ee30", "e800000000")
    insn.call_target = target
    lifted = "\n".join(batch.translator.lifter.lift_instruction(insn))
    assert "RECOMP_ABI_CALL(0x0006EE30u, sub_0006EE30);" in lifted

    output = {}
    monkeypatch.setattr(translator.os, "makedirs", lambda *args, **kwargs: None)
    monkeypatch.setattr(translator.glob, "glob", lambda *args, **kwargs: [])
    monkeypatch.setattr(
        translator, "write_if_changed",
        lambda path, text: output.update({Path(path).name: text}),
    )

    def no_runtime_file(*args, **kwargs):
        raise OSError("synthetic in-memory generation")

    monkeypatch.setattr(translator, "open", no_runtime_file, raising=False)
    stats = batch.translate_batch_split(functions, "synthetic-generated", manual=manual)
    assert stats["failed"] == 0
    assert "void sub_0006EE30_gen(void)" in output["recomp_0000.c"]
    assert "void sub_0006EE30(void);" in output["recomp_funcs.h"]
    assert "(recomp_func_t)sub_0006EE30 }" in output["recomp_dispatch.c"]
    assert "(recomp_func_t)sub_0006EE30_gen" not in output["recomp_dispatch.c"]


def test_camera_wrapper_participates_in_generation_fingerprint(monkeypatch):
    monkeypatch.syspath_prepend(str(ROOT / "tools"))
    import build

    assert build.MANUAL_SOURCE == ROOT / "src" / "recomp_manual.c"
    pointer = ROOT / "tests" / "latest.json"
    monkeypatch.setattr(build, "ANALYSIS_DIR", ROOT / "tests")
    original_is_file = Path.is_file
    monkeypatch.setattr(Path, "is_file", lambda path: path == pointer or original_is_file(path))
    manifest = {
        "status": "generated",
        "toolkit_base": build.TOOLKIT_BASE,
        "fingerprint_sha256": "unchanged",
        "seed_sha256": "unchanged",
        "manual_sha256": "before-camera-wrapper",
        "xbe_sha256": "unused",
    }
    monkeypatch.setattr(
        build, "load_json",
        lambda path: {"run_id": "20250101-000000-00000000"} if path == pointer else manifest,
    )
    monkeypatch.setattr(build, "generation_toolkit_matches", lambda _: True)
    monkeypatch.setattr(
        build, "sha256_file",
        lambda path: "after-camera-wrapper" if path == build.MANUAL_SOURCE else "unchanged",
    )
    monkeypatch.setattr(build, "verify_xbe", lambda *args: "unused")
    with pytest.raises(build.BuildError, match="manual source"):
        build.latest_generation()
