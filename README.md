# 83mKr TPC Calibration Simulation

End-to-end simulation of krypton-83m decays in the ALICE TPC, producing
digitized cluster spectra for calibration validation.

---

## Contents

```
run_kr_sim.sh            — single-run simulator
run_kr_batches.sh        — batch simulator: N independent runs of M events each
run_kr_digi_clust.sh     — digitizer + O2 cluster finder (default); --digi-only / --clust-only for individual steps
run_digi_from_batches.sh — digitizer for all batches produced by run_kr_batches.sh
build_kr_generator.sh    — compile the external generator (run once)
kr_g4config.in           — Geant4 macro with physics settings
krBoxCluster.largeBox.cuts.krMap.ini — cluster finder configuration
GainMap_2021-11-17_krypton_0.5T.root  — TPC gain map (flat for simulation)
o2_changes.txt           — summary of O2 source code changes and motivation
macros/
  GeneratorKrDecay.h/.cxx      — external generator source
  GeneratorKrDecayLoader.C     — CLING loader shim
  findKrBoxCluster.C           — cluster finder (reads glob of tpcdigits.root files)
  plotBoxClusters.C            — spectrum plotter + multi-Gaussian fit
  plotBoxClustersMerged.C      — merged/per-ROC spectrum plotter
  plotDplClusters.C            — per-sector DPL cluster spectrum plotter
local_o2_patches/        — patch scripts for O2 source files (apply once per build)
```

---

## Recommended Workflow

```bash
# 0. One-time: compile the generator
./build_kr_generator.sh

# Create a run directory and work from inside it
mkdir KrRun && cd KrRun

# 1. Simulate: 100 events, 5000 decays per event (500 000 total)
../run_kr_sim.sh -n 100 -d 5000

# 2. Digitize and run the O2 cluster finder in one step
../run_kr_digi_clust.sh

# 3. Plot the cluster spectrum (IROC, all sectors)
root -b -l -q '../macros/plotDplClusters.C'
```

The `-d` / `--nPerEvent` flag sets the number of Kr decays placed per time frame
(default 5000). Going above 5000 may hit the shared-memory limit of your machine
(`/dev/shm`). The default SHM size is set to 12 GB in `run_kr_digi_clust.sh`; if
your device has more memory you can override it with `--shm-size <bytes>` and
increase `-d` accordingly.

`run_kr_digi_clust.sh` runs digitization and the O2 TPC cluster finder together.
Pass `--digi-only` or `--clust-only` to run either step in isolation.

---

## Batch Workflow (Larger Production)

To accumulate more statistics than a single run allows, use batch mode.
Each batch is an independent simulation + digitization, avoiding any
shared-memory constraint from large `nPerEvent`.

```bash
mkdir KrRun && cd KrRun

# 1. Simulate 50 batches of 100 events × 5000 decays (25 M total decays)
../run_kr_batches.sh -n 100 -d 5000 -b 50

# 2. Digitize each batch independently
../run_digi_from_batches.sh

# 3. Plot — plotDplClusters.C reads the per-batch cluster files
root -b -l -q '../macros/plotDplClusters.C'
```

**Do not hadd tpcdigits.root files** — the TPC CommonMode branches expand from
~65 MB/batch to ~100 GB when recompressed by hadd.

---

## Bare Commands (no scripts)

The same simulation chain expressed as direct O2 commands, with every flag
spelled out. Useful for quick manual tests or when adapting the workflow.
Run from inside a fresh directory (e.g. `mkdir KrRun && cd KrRun`).

```bash
# nPerEvent is passed to the generator via environment variable
export KR_N_PER_EVENT=5000

# Simulation — 100 events × 5000 decays
o2-sim \
  -m TPC \
  -n 100 \
  -g external \
  --field 0 \
  --nworkers 4 \
  --configKeyValues \
    "GeneratorExternal.fileName=/path/to/macros/GeneratorKrDecayLoader.C;\
GeneratorExternal.funcName=GeneratorKrDecay();\
align-geom.mDetectors=none;\
G4.physicsmode=kUSER;\
G4.userPhysicsList=FTFP_BERT_LIV;\
G4.configMacroFile=/path/to/kr_g4config.in;\
GlobalSimProcs.CUTELE=1e-6;\
GlobalSimProcs.CUTGAM=1e-6;\
TPCDetParam.UseGeant4Edep=1;\
GenTPCLoopers.loopersVeto=true;\
SimCutParams.stepFiltering=false" \
  -o o2sim_kr \
  &> log.sim

# Digitization
o2-sim-digitizer-workflow \
  --TPCtriggered \
  --shm-segment-size $(( 12 << 30 )) \
  --configKeyValues \
    "TPCEleParam.ADCdynamicRange=700;\
TPCEleParam.ADCsaturation=8192;\
TPCEleParam.ChipGain=1;\
TPCEleParam.DigiMode=ZeroSuppressionCMCorr;\
TPCEleParam.applyDeadMap=false;\
TPCEleParam.doCommonModePerPad=false;\
TPCDetParam.ExcludeFCGap=false;\
TPCDetParam.DriftTimeOffset=0;\
TPCGasParam.OxygenCont=3e-06" \
  -b \
  &> log.digi

# Cluster finding (O2 TPC reco workflow)
o2-tpc-reco-workflow \
  -b \
  --session default \
  --shm-segment-size $(( 12 << 30 )) \
  --input-type digitizer \
  --output-type clusters,tracks \
  --disable-mc \
  --configKeyValues "align-geom.mDetectors=none" \
  &> log.clust

# Plot
root -b -l -q '/path/to/macros/plotDplClusters.C'
```

`KR_N_PER_EVENT` is read by `GeneratorKrDecay` at startup via `std::getenv`.
Going above 5000 may hit the `/dev/shm` limit; increase `--shm-segment-size`
proportionally if your machine allows it.

---

## Physics: 83mKr Decay Channels

Kr-83m decays via internal transition emitting conversion electrons (CE)
and associated X-rays / Auger electrons. The dominant channels and their
total visible energies are:

| Channel                     | Energy (keV) | Expected ADC* | Branching |
|-----------------------------|-------------|---------------|-----------|
| L-CE + T2-gamma             | 41.6        | ~2709         | ~73%      |
| L-CE + T2-gamma (K-esc)     | 32.2        | ~2097         | ~4%       |
| L-CE + K-CE                 | 29.1        | ~1894         | ~14%      |
| K-alpha fluorescence        | 12.6        | ~820          | ~15%      |
| T2-gamma satellite          |  9.4        | ~611          | ~5%       |

*ADC at ADCdynamicRange=700 mV, ADCsaturation=8192, ChipGain=1.

The box cluster finder (`KrBoxClusterFinder`) captures all products from
a single decay vertex into one cluster. The Q_tot distribution should
show these five discrete peaks.

---

## Common Mode Accumulation

`DigiMode=ZeroSuppressionCMCorr` runs a per-sector CM estimator that
accumulates a running baseline across all events in one digitizer invocation.
With only ~1 Kr decay per sector per TF (nPerEvent = 1) the estimator has too
little signal to converge and oscillates, causing digit charges to degrade from
~131 ADC/channel to ~3 ADC after ~30 000 events.

**This is not an issue with nPerEvent ≥ 500.** Each TF contains 500+ decays
spread across all sectors, giving the CM estimator sufficient signal to converge
immediately and remain stable throughout the run. Batch splitting was required
only for the nPerEvent = 1 use case.

For reference, approaches that do **not** work for the nPerEvent = 1 case:
- `--start-value-enumeration` / `--end-value-enumeration`: not honoured by
  SimReader in the current O2 build.
- Continuous readout mode: produces a single 50M-timebin TF; CM still
  accumulates across the whole TF.
- Merging tpcdigits.root with hadd: ROOT inflates CommonMode branches from
  ~65 MB/batch to ~100 GB total.

---

## O2 Source Changes

Three files are modified in `Detectors/TPC/`. Local patch copies are in
`local_o2_patches/` for reference; the user applies them to the O2 installation.

### `base/include/TPCBase/ParameterDetector.h`
Adds one field to `ParameterDetector`:
```cpp
bool UseGeant4Edep = false;
```
Opt-in flag, off by default — the standard simulation chain is unaffected.

### `simulation/include/TPCSimulation/Detector.h`
Declares the `SetSpecialPhysicsCuts()` override.

### `simulation/src/Detector.cxx`

**ProcessHits():** wraps the Bethe-Bloch block in an `else` branch:
```cpp
if (detParam.UseGeant4Edep) {
    numberOfElectrons = static_cast<int>(fMC->Edep() / gasParam.Wion);
} else {
    // ... existing Bethe-Bloch + NA49 collision sampling ...
}
```

**SetSpecialPhysicsCuts():** adds a TPC subclass override that sets 1 keV
cuts on TPC drift gas mediums *before* calling the base class (which reads
`simcuts.dat`):
```cpp
void Detector::SetSpecialPhysicsCuts()
{
  if (detParam.UseGeant4Edep) {
    auto& matmgr = o2::base::MaterialManager::Instance();
    for (int med : {(int)kDriftGas1, (int)kDriftGas2, (int)kCO2}) {
      matmgr.SpecialCut(GetName(), med, o2::base::ECut::kCUTELE, 1e-6f);
      matmgr.SpecialCut(GetName(), med, o2::base::ECut::kCUTGAM, 1e-6f);
      matmgr.SpecialCut(GetName(), med, o2::base::ECut::kDCUTE,  1e-6f);
      matmgr.SpecialCut(GetName(), med, o2::base::ECut::kBCUTE,  1e-6f);
    }
  }
  o2::base::Detector::SetSpecialPhysicsCuts(); // reads simcuts.dat AFTER
}
```

**Why `simcuts.dat` is not modified:**
`simcuts.dat` (installed at `$O2_ROOT/share/Detectors/TPC/simulation/data/`)
hardcodes `CUTELE = 1e-5 GeV = 10 keV` for TPC drift gas mediums 1, 2 and 3
(`kDriftGas1`, `kDriftGas2`, `kCO2`). This 10 keV floor kills Kr decay products
before they can be tracked: the K-shell conversion electron at 9.4 keV, L-CE
T2 at 7.475 keV, and Auger electrons at 1.921 keV all fall below 10 keV.

`TG4GeometryManager` honours the **first** `Gstpar` call for each
(medium, parameter) pair — subsequent calls are silently ignored. By calling
`matmgr.SpecialCut` with 1 keV *before* the base class reads `simcuts.dat`,
our 1 keV wins and `simcuts.dat` is effectively bypassed for these mediums
without needing to edit or copy the installed file.

Confirmed via the worker log: before the fix the log showed
`Energy thresholds: e- 10 keV` for `TPC_Ne-CO2-N-2`; after the fix it shows
`Energy thresholds: e- 1 keV`, confirming that Auger electrons at 1.921 keV
are now tracked rather than killed at birth.

### Why it works

`fMC->Edep()` returns Geant4's actual continuous energy loss for that
transport step, computed by the **Livermore EM** physics model
(`FTFP_BERT_LIV`). Livermore uses EADL/EEDL data-driven cross-sections
designed for accurate electron transport from a few eV upwards — exactly
the regime of Kr decay electrons (9–42 keV). The total ionisation per
cluster then correctly reflects the decay channel energy, producing the
expected discrete peaks. Additionally, `/process/eLoss/StepFunction 0.2 0.1 mm`
ensures enough ionisation steps are produced per track that individual
digit charges are small and the cluster charge distribution is narrow,
bringing the O2 simulation resolution (8.01%) to within 0.1 pp of the
G4 standalone reference (7.95%).

---

## External Generator

### `macros/GeneratorKrDecay.h/.cxx`

Implements a `FairGenerator` subclass that:
1. Places one Kr-83m atom at rest at a random position uniformly
   distributed in the TPC active volume.
2. Samples the decay channel using the known branching ratios.
3. Emits the resulting particles (CEs, Auger electrons, fluorescence
   photons) as primary tracks for Geant4 to transport.

### `macros/GeneratorKrDecayLoader.C`

O2's `GeneratorExternal` loads the generator via CLING
(`gROOT->ProcessLine`). CLING can interpret `.C` macros but cannot
directly see symbols in compiled `.so` libraries. The loader is a thin
CLING macro that:
1. Calls `gSystem->Load("/tmp/libGeneratorKrDecay.so")` to load the
   compiled library so its symbols become available to the linker.
2. Uses `dlsym` to resolve the factory function by name.
3. Returns the constructed generator object to `GeneratorExternal`.

This gives compiled performance (5+ simulation workers) while remaining
compatible with the CLING-based `GeneratorExternal` interface.

Build the `.so` once with:
```bash
./build_kr_generator.sh
```

---

## Simulation Configuration

### Physics list: `FTFP_BERT_LIV`

Selected via `G4.physicsmode=kUSER` and `G4.userPhysicsList=FTFP_BERT_LIV`.
The Livermore electromagnetic model uses EADL/EEDL data-driven cross-sections
for accurate treatment of electrons and photons at keV scale, outperforming
Penelope (PEN) for Kr decay energies (9–42 keV). See `o2_changes.txt` for
the full parameter scan that determined this choice.

### `kr_g4config.in` — Geant4 macro settings

| Command | Value | Reason |
|---------|-------|--------|
| `/mcPhysics/rangeCuts` | 0.000004 mm (4 nm) | Production threshold → 1.69 keV in TPC gas. Below the Auger energy (1.921 keV), so all Kr primaries are tracked. Default 100 nm gives a 10 keV threshold which kills T2-CE and Auger electrons entirely. |
| `/process/em/fluo` + `fluoBearden` | true | Fluorescence photons produced using Bearden atomic data |
| `/process/em/auger` + `augerCascade` | true | Full Auger cascade chain tracked |
| `/process/em/deexcitationIgnoreCut` | true | Track de-excitation products even below the production cut |
| `/process/eLoss/StepFunction` | 0.2 0.1 mm | Limits each step to at most 20% energy loss (dRoverR=0.2) and enforces fine steps in the last 0.1 mm of range. This is the single largest improvement found: 10.37% → 8.06% resolution on the 41.6 keV peak. Both parameters are synergistic — combined gain is ~10× larger than either alone. Hits file grows from ~188 MB to ~307 MB per run. |
| `/process/em/transportationWithMsc` | Disabled | Required for ALICE geometry (broken since Geant4 10.2) |

### `--configKeyValues` for simulation

| Key | Value | Reason |
|-----|-------|--------|
| `TPCDetParam.UseGeant4Edep` | 1 | Use Geant4 Edep instead of Bethe-Bloch (see above) |
| `G4.physicsmode` | kUSER | Enable custom physics list |
| `G4.userPhysicsList` | FTFP_BERT_LIV | Livermore EM for accurate low-energy electrons (EADL/EEDL data-driven) |
| `G4.configMacroFile` | kr_g4config.in | Load custom Geant4 settings above |
| `GlobalSimProcs.DRAY` | 1 | Enable delta-ray production; δ-rays above 1.69 keV get explicit tracks |
| `GlobalSimProcs.CUTELE` | 1e-6 (MeV = 1 keV) | Kill secondary electrons below 1 keV |
| `GlobalSimProcs.CUTGAM` | 1e-6 | Kill photons below 1 keV |
| `SimCutParams.stepFiltering` | false | Do not filter out short steps |
| `GenTPCLoopers.loopersVeto` | true | Kill looping tracks before they accumulate spurious hits |

---

## Digitizer Configuration

The digitizer is run via `run_digi_from_batches.sh` (batch mode) or
`run_kr_digi_clust.sh --digi-only` (single run). The equivalent bare command:

```bash
o2-sim-digitizer-workflow --TPCtriggered \
  --configKeyValues "TPCEleParam.ADCdynamicRange=700;
                     TPCEleParam.ADCsaturation=8192;
                     TPCEleParam.ChipGain=1;
                     TPCEleParam.DigiMode=ZeroSuppressionCMCorr;
                     TPCEleParam.applyDeadMap=false;
                     TPCEleParam.doCommonModePerPad=false;
                     TPCDetParam.ExcludeFCGap=false;
                     TPCDetParam.DriftTimeOffset=0" \
  -b -- --shm-segment-size 0
```

| Parameter | Value | Reason |
|-----------|-------|--------|
| `ADCdynamicRange` | 700 mV | ALICE Run 3 SAMPA dynamic range |
| `ADCsaturation` | 8192 | 13-bit ADC (Run 3) |
| `ChipGain` | 1 | Effective front-end gain; GEM amplification absorbed into calibration |
| `DigiMode` | ZeroSuppressionCMCorr | ZS mode that adds noise **before** the threshold comparison. Plain `ZeroSuppression` compares raw signal against threshold with no noise, cutting too many edge timebins and reducing cluster size ~2.5×, destroying the discrete peak structure. `ZeroSuppressionCMCorr` matches the behaviour of the pre-2025 O2 binary where the ZS check included noise fluctuations. |
| `applyDeadMap` | false | No dead-channel map in simulation |
| `doCommonModePerPad` | false | No common-mode correction |
| `ExcludeFCGap` | false | Include field-cage gap region |
| `DriftTimeOffset` | 0 | No drift time offset |

**ADC conversion:**
```
ADC/electron = ElectronCharge × 1e15 × ChipGain × ADCsaturation / ADCdynamicRange
             = 1.602e-19 × 1e15 × 1 × 8192 / 700
             ≈ 1.876e-3 ADC per post-GEM electron
```

**Zero suppression threshold:** 3 × noise ≈ 3 ADC (default noise map = 1 ADC/pad).

---

## Cluster Finder

### O2 cluster finder (default)

`run_kr_digi_clust.sh` (without flags) runs both the digitizer and the O2 TPC
cluster finder (`o2-tpc-reco-workflow`) in one pipeline. The output is read
directly by `plotDplClusters.C`.

```bash
../run_kr_digi_clust.sh                # digi + O2 cluster finder (default)
../run_kr_digi_clust.sh --digi-only    # digitization only → tpcdigits.root
../run_kr_digi_clust.sh --clust-only   # cluster finding only (needs tpcdigits.root)
```

### `macros/plotDplClusters.C`

Reads the O2 cluster output and plots the charge spectrum per sector and per ROC.
Run from inside the KrRun directory (no arguments needed for the default output file):

```bash
root -b -l -q '../macros/plotDplClusters.C'
```

### Alternative: Kr box cluster finder

If the O2 cluster finder is not available or a cross-check is needed, the
standalone `KrBoxClusterFinder` can be used instead. It reads `tpcdigits.root`
directly (produced by `--digi-only`) and writes clusters to `BoxClusters.root`.

```bash
# Requires tpcdigits.root (run --digi-only first)
root -b -l -q '../macros/findKrBoxCluster.C+("tpcdigits.root","BoxClusters.root","../GainMap_2021-11-17_krypton_0.5T.root")'
root -b -l -q '../macros/plotBoxClustersMerged.C("BoxClusters.root")'
```

Cluster finder settings are read from `krBoxCluster.largeBox.cuts.krMap.ini`
(key parameters: `MaxClusterSizeTime=10`, `OxygenCont=3e-06`).
