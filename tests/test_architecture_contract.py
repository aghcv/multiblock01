import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = ROOT / "docs" / "architecture_contract.json"
ARCHITECTURE_PATH = ROOT / "docs" / "architecture.md"


def normalize_text(text):
    return " ".join(text.lower().split())


class ArchitectureContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract = json.loads(CONTRACT_PATH.read_text(encoding="utf-8"))
        cls.architecture = ARCHITECTURE_PATH.read_text(encoding="utf-8")
        cls.normalized_architecture = normalize_text(cls.architecture)

    def test_canonical_object_is_4d_vtk_multiblock(self):
        self.assertEqual(self.contract["canonical_object"], "4d_vtk_multiblock")

    def test_required_invariants_are_documented(self):
        invariants = self.contract["mandatory_invariants"]
        self.assertGreaterEqual(len(invariants), 5)
        for invariant in invariants:
            with self.subTest(invariant=invariant["id"]):
                self.assertIn(invariant["id"], self.architecture)
                self.assertIn(
                    normalize_text(invariant["statement"]),
                    self.normalized_architecture,
                )

    def test_pipeline_layers_are_documented(self):
        for layer in self.contract["pipeline_layers"]:
            with self.subTest(layer=layer):
                self.assertIn(f"### `{layer}`", self.architecture)

    def test_simulation_ready_block_fields_are_documented(self):
        fields = self.contract["simulation_ready_block_required_fields"]
        self.assertIn("anatomy_id", fields)
        self.assertIn("provenance", fields)
        for field in fields:
            with self.subTest(field=field):
                self.assertIn(f"`{field}`", self.architecture)

    def test_decomposition_axes_are_documented(self):
        for axis in self.contract["decomposition_axes"]:
            with self.subTest(axis=axis):
                self.assertIn(f"`{axis}`", self.architecture)

    def test_fidelity_grades_are_documented(self):
        for grade in self.contract["fidelity_grades"]:
            with self.subTest(grade=grade):
                self.assertIn(f"`{grade}`", self.architecture)

    def test_solver_adapter_candidates_are_named(self):
        solvers = set(self.contract["candidate_solver_adapters"])
        expected = {"VMTK", "Gmsh", "OpenFOAM", "FEBio", "SOFA", "preCICE"}
        self.assertTrue(expected.issubset(solvers))


if __name__ == "__main__":
    unittest.main()
