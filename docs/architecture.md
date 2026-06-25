# Digital Patient Infrastructure Architecture

Working name: Digital Patient Infrastructure. Short repo-name candidates include
`digital-patient-infra`, `patient-twin-infra`, `PopTwin`, and
`PopulationTwin`.

This project is a framework for turning 4D hybrid human phantoms into
simulation-ready, physics-annotated, AI-trainable digital patient datasets.
The framework starts from VTK multiblock data, not from any single solver. VTK
is the data spine; solver cases, repaired meshes, reduced-order graphs, and
results are derived views that must attach back to the same canonical object.

## Core Idea

Large language models became practical because the internet provided a massive,
loosely standardized text corpus. Whole-body spatiotemporal physics does not
have an equivalent corpus. This project aims to create one by compiling
synthetic 4D anatomy into repeatable simulation cases, collecting the computed
results, and storing those results in a consistent whole-body VTK hierarchy.

The first goal is not one perfect whole-body simulation. The first goal is a
repeatable pipeline that can produce many trustworthy partial simulations, each
with enough provenance to be useful for population-scale learning.

## Architecture Contract

The machine-readable version of these invariants lives in
`docs/architecture_contract.json` and is checked by
`tests/test_architecture_contract.py`.

- `canonical_vtk_spine`: the 4D VTK multiblock object is the canonical data
  object for anatomy, motion, derived geometry, solver inputs, solver outputs,
  and provenance.
- `volume_truth_first`: labeled voxel image data is the source of anatomical
  truth; surfaces and meshes are derived, validated, and repairable.
- `stable_anatomy_identity`: anatomy labels and block identities must be stable
  across timesteps and population variants.
- `solver_outputs_return_home`: every solver output must map back to the VTK
  hierarchy with units, timestep, anatomy id, solver id, and provenance.
- `progressive_fidelity`: the framework must support reduced-order, local
  high-fidelity, coupled multiphysics, and AI-surrogate datasets side by side.
- `automated_quality_gates`: mesh quality, topology, boundary labels, units,
  and result provenance need automated tests before data is accepted.

## Canonical Object

The canonical object is a time-indexed VTK scene:

```text
patient_or_phantom.pvd
  time_001/scene.vtm
    activity.vti
    attenuation.vti
    label_image.vti
    anatomy surfaces...
    derived solver domains...
    simulation results...
  time_002/scene.vtm
  ...
```

Each timestep should be loadable by VTK/ParaView, but the hierarchy should also
be machine-readable enough to drive automated simulation.

## Decomposition Axes

The framework decomposes the body along several axes at once:

- `spatial_region`: head, thorax, abdomen, pelvis, left arm, right arm, left
  leg, right leg.
- `anatomical_system`: vascular, cardiac, airway, skeletal, muscle, organ,
  neural, skin, connective tissue.
- `connected_component`: vessel branch, organ lobe, chamber, bone, muscle
  group, airway branch.
- `physics_domain`: fluid lumen, deformable wall, rigid or articulated solid,
  poroelastic tissue, contact pair, FSI interface, thermal/electrical region.
- `solver_partition`: the portion exported to a specific solver or HPC task.
- `learning_view`: point cloud, mesh graph, voxel crop, centerline graph,
  reduced-order network, or time-series tensor.

## Simulation-Ready Block Metadata

Each simulation-ready block should eventually provide:

- `anatomy_id`: stable label or registry id.
- `time_index`: integer timestep plus physical time if available.
- `source_paths`: source VTI/VTP/VTM paths.
- `source_truth`: voxel label id, derived surface id, or external annotation.
- `physics_tags`: fluid, solid, FSI, contact, rigid, thermal, electrical,
  poroelastic, reduced_order.
- `geometry_quality`: watertightness, manifold status, holes,
  self-intersections, boundary loops, volume agreement with labels.
- `interfaces`: inlet, outlet, wall, contact, FSI, symmetry, fixed support,
  load surface.
- `mesh_assets`: surface mesh, volume mesh, boundary mesh, centerline graph,
  reduced-order graph.
- `materials`: density, viscosity, elastic model, fiber direction, damping,
  permeability, conductivity.
- `boundary_conditions`: pressure, flow, displacement, force, contact, thermal,
  electrical, or coupled interface definitions.
- `solver_targets`: solver adapter names and generated case paths.
- `results`: pressure, velocity, wall shear stress, displacement, strain,
  stress, temperature, electrophysiology, or derived biomarkers.
- `provenance`: tool versions, commit hash, command line, solver settings,
  convergence metrics, runtime, hardware, and QA status.

## Pipeline Layers

### `ingest_4d_vtk`

Read a PVD/VTM/VTI/VTP phantom output and produce an inventory of timesteps,
blocks, labels, image grids, surfaces, units, and bounds.

### `anatomy_registry`

Create a stable anatomy vocabulary. This is the equivalent of a tokenizer for
the physics corpus. It maps names, label ids, synonyms, systems, and body
regions into persistent ids.

### `geometry_repair`

Compare surfaces against voxel truth. Repair or regenerate surfaces when they
are non-manifold, not watertight, self-intersecting, or inconsistent with the
label image.

### `topology_decomposition`

Break complex structures into simulation units: vascular branches, inlet/outlet
caps, vessel walls, heart chambers, myocardium layers, bones, joints, muscles,
organs, and contact interfaces.

### `mesh_generation`

Generate solver-appropriate meshes. Examples include tetrahedral solid meshes,
boundary-layer CFD meshes, shell meshes, centerline graphs, and voxel crops.

### `physics_case_builder`

Attach materials, boundary conditions, coupling interfaces, time controls, and
solver-specific configuration files.

### `solver_adapters`

Export and run cases for open-source solvers. Candidate adapters include VMTK,
Gmsh, OpenFOAM, SimVascular/svMultiPhysics, FEBio, SOFA, FEniCS, MFEM, MOOSE,
CalculiX, Elmer, SU2, and preCICE.

### `result_ingestion`

Read solver outputs, validate units and convergence, map fields back to the VTK
multiblock hierarchy, and write result blocks or arrays beside the source
anatomy.

### `corpus_builder`

Create AI-ready training records from the canonical VTK data spine. Supported
views should include voxel tensors, point clouds, surface meshes, centerline
graphs, reduced-order networks, and timestep sequences.

## Physics Domains

- Blood flow: 0D/1D vascular networks, local 3D CFD, wall shear stress,
  pressure, flow splits, pulsatile boundary conditions.
- Cardiac mechanics: chamber surfaces, myocardium layers, valve interfaces,
  active/passive tissue models, FSI targets.
- Airflow and lung mechanics: airway branches, lobes, pressure/flow, tissue
  expansion during inhale/exhale.
- Skeletal and joint mechanics: bones as rigid or deformable solids, cartilage
  contact, joint loads, posture variants.
- Muscle mechanics: deformable solids, fiber directions, activation fields,
  stress/strain, attachment constraints.
- Organ mechanics: organ deformation, capsule constraints, contact against
  neighboring anatomy.
- Thermal/electrical fields: optional domains for dosimetry, ablation,
  stimulation, electrophysiology, and heat transfer.

## Fidelity Grades

- `grade_0_anatomy`: labels, images, surfaces, motion, QA only.
- `grade_1_reduced_order`: 0D/1D graphs and lumped models.
- `grade_2_local_3d`: local CFD/FEM on selected anatomy.
- `grade_3_coupled_multiphysics`: FSI, contact, or coupled fields.
- `grade_4_hybrid_whole_body`: whole-body reduced-order graph plus selected
  high-fidelity regions.
- `grade_5_ai_surrogate`: learned model predictions with uncertainty and links
  back to training simulations.

## Initial Milestones

1. Add a 4D VTK inventory command that scans a PVD/VTM phantom and emits JSON.
2. Add an anatomy registry file built from the inventory.
3. Add topology and geometry QA reports for each surface block.
4. Add a simulation-readiness manifest for selected domains.
5. Add the first solver adapter around a narrow target, likely vascular branch
   preprocessing through VMTK/Gmsh or a small OpenFOAM/FEBio/SOFA case.
6. Map one solver result back into the VTK hierarchy with provenance.
7. Generalize the manifest and tests before adding more solvers.

## Testing Strategy

Tests should protect the architecture, not just individual functions.

- Contract tests keep this document and `architecture_contract.json` aligned.
- Inventory tests should run on tiny synthetic VTK fixtures before large
  phantoms.
- Registry tests should verify stable anatomy ids across reordered or missing
  blocks.
- Geometry tests should detect non-manifold faces, open boundaries,
  self-intersections, missing caps, and voxel/surface mismatch.
- Manifest tests should reject simulation cases with missing units, materials,
  boundary conditions, or provenance fields.
- Adapter tests should generate minimal solver cases without requiring the full
  solver stack.
- Result-ingestion tests should verify that computed arrays map back to the
  correct anatomy id and timestep.

