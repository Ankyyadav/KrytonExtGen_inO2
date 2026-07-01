# 83mKr TPC Calibration Simulation

End-to-end simulation of krypton-83m decays in the ALICE TPC, producing
digitized cluster spectra for calibration validation.

---

## Contents

```
run_kr_sim.sh            — single-run simulator (small tests only, see CM note)
run_kr_batches.sh        — batch simulator: N independent runs of M events each
run_kr_digi_clust.sh     — digitizer + optional cluster finder (single run)
run_digi_from_batches.sh — digitizer for all batches produced by run_kr_batches.sh
build_kr_generator.sh    — compile the external generator (run once)
kr_g4config.in           — Geant4 macro with physics settings
GainMap_2021-11-17_krypton_0.5T.root  — TPC gain map (flat for simulation)
macros/
  GeneratorKrDecay.h/.cxx      — external generator source
  GeneratorKrDecayLoader.C     — CLING loader shim
  findKrBoxCluster.C           — cluster finder (reads glob of tpcdigits.root files)
  plotBoxClusters.C            — spectrum plotter + multi-Gaussian fit
  plotBoxClustersMerged.C      — merged/per-ROC spectrum plotter
```

---

## Recommended Workflow (Batch Mode)

The digitizer's common-mode estimator accumulates a baseline across all events
in a run. With sparse Kr events (~1 decay/sector/TF) this baseline drifts and
the digit charge degrades after ~30 000 events. **Events must be processed in
batches** so each batch starts with a fresh baseline.

Working configuration: **40 batches of 25 000 events** (1 M total).

```bash
# 0. One-time: compile the generator
./build_kr_generator.sh

# Create a scan directory and run from inside it
mkdir scan_1M && cd scan_1M

# 1. Simulate 40 batches of 25k events each (no merging)
../run_kr_batches.sh -e 25000 -b 40

# 2. Digitize each batch independently (fresh CM baseline per batch)
#    reads from kr_batches/, writes tpcdigits.root into kr_batches/batch_*/
../run_digi_from_batches.sh

# 3. Find clusters across all batches in one pass → BoxClusters.root
root -b -l -q '../macros/findKrBoxCluster.C+("kr_batches/batch_*/tpcdigits.root","BoxClusters.root","../GainMap_2021-11-17_krypton_0.5T.root")'

# 4. Plot spectrum
root -b -l -q '../macros/plotBoxClustersMerged.C("BoxClusters.root")'
```

After step 2, per-batch tpcdigits.root files are in `digi_batches/batch_*/`.
**Do not hadd tpcdigits.root files** — the TPC CommonMode branches expand from
~65 MB/batch to ~100 GB when recompressed by hadd. The cluster finder reads
multiple files directly via TChain (the `batch_*/tpcdigits.root` glob).

---

## Small-Scale Test (Single Run)

For quick checks with a few thousand events where CM drift is not yet a problem:

```bash
mkdir test && cd test
../run_kr_sim.sh -n 5000
../run_kr_digi_clust.sh --digi-only
root -b -l -q '../macros/findKrBoxCluster.C+("tpcdigits.root","BoxClusters.root","../GainMap_2021-11-17_krypton_0.5T.root")'
root -b -l -q '../macros/plotBoxClustersMerged.C("BoxClusters.root")'
```

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

## Common Mode Accumulation Constraint

`DigiMode=ZeroSuppressionCMCorr` runs a per-sector CM estimator that
accumulates a running baseline across all events in one digitizer invocation.
With only ~1 Kr event per sector per TF the estimator has too little signal to
converge and oscillates, causing digit charges to degrade from ~131 ADC/channel
(correct) to ~3 ADC after ~30 000 events. The first 5 000 events from any run
produce the correct spectrum.

**Fix:** run the digitizer in separate invocations of ≤25 000 events each.
`run_digi_from_batches.sh` does this automatically using batches produced by
`run_kr_batches.sh`. Each invocation starts with a fresh baseline.

Approaches that do **not** work:
- `--start-value-enumeration` / `--end-value-enumeration`: these DPL flags are
  not honoured by SimReader in the current O2 build.
- Continuous readout mode (`--interactionRate`): produces a single 50M-timebin
  TF; the CM estimator still accumulates across the whole TF.
- Merging tpcdigits.root from batches with hadd: ROOT inflates the CommonMode
  branches from ~65 MB/batch to ~100 GB total.

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
transport step, computed by the **Penelope EM** physics model
(`FTFP_BERT_PEN`). Penelope is designed for accurate electron transport
from ~100 eV upwards — exactly the regime of Kr decay electrons. The
total ionisation per cluster then correctly reflects the decay channel
energy, producing the expected discrete peaks.

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

### Physics list: `FTFP_BERT_PEN`

Selected via `G4.physicsmode=kUSER` and `G4.userPhysicsList=FTFP_BERT_PEN`.
The Penelope electromagnetic model provides accurate treatment of
low-energy electrons and photons, replacing the standard EM tables.

### `kr_g4config.in` — Geant4 macro settings

| Command | Value | Reason |
|---------|-------|--------|
| `/mcPhysics/rangeCuts` | 0.000004 mm (4 nm) | Production threshold → 1.69 keV in TPC gas. Below the Auger energy (1.921 keV), so all Kr primaries are tracked. Default 100 nm gives a 10 keV threshold which kills T2-CE and Auger electrons entirely. |
| `/process/em/fluo` + `fluoBearden` | true | Fluorescence photons produced using Bearden atomic data |
| `/process/em/auger` + `augerCascade` | true | Full Auger cascade chain tracked |
| `/process/em/deexcitationIgnoreCut` | true | Track de-excitation products even below the production cut |
| `/process/eLoss/StepFunction` | 1.0 1 mm | Allow up to 100% energy loss per step; fine steps only in last 1 mm of range. Electrons with range < 1 mm (all Kr primaries below ~30 keV) are transported in one compact step → high per-digit ADC, clean cluster identification. |
| `/process/em/transportationWithMsc` | Disabled | Required for ALICE geometry (broken since Geant4 10.2) |

### `--configKeyValues` for simulation

| Key | Value | Reason |
|-----|-------|--------|
| `TPCDetParam.UseGeant4Edep` | 1 | Use Geant4 Edep instead of Bethe-Bloch (see above) |
| `G4.physicsmode` | kUSER | Enable custom physics list |
| `G4.userPhysicsList` | FTFP_BERT_PEN | Penelope EM for accurate low-energy electrons |
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

### `macros/findKrBoxCluster.C`

Reads one or more `tpcdigits.root` files via TChain (glob patterns accepted),
runs `KrBoxClusterFinder`, writes all clusters to `BoxClusters.root` in branch
`"cls"` (all 36 sectors combined). Gain map applied if the file exists.

Single-run (default — reads `tpcdigits.root` in the current directory):
```bash
root -b -l -q '../macros/findKrBoxCluster.C+'
```

Batch mode (pass the glob explicitly):
```bash
root -b -l -q '../macros/findKrBoxCluster.C+("kr_batches/batch_*/tpcdigits.root","BoxClusters.root","../GainMap_2021-11-17_krypton_0.5T.root")'
```

Compiled defaults: `MaxClusterSizeTime=3`, `MaxClusterSizePadIROC=5`,
`MaxClusterSizeRowIROC=3`, `QThresholdMax=30`, `QThreshold=1`,
`MinNumberOfNeighbours=2`, `setMaxTimes(70000)`.

### `macros/plotBoxClustersMerged.C`

Reads `"cls"` branch, supports per-ROC and per-sector selection. By default
plots merged IROC spectrum. Options: `IROC`, `OROC1`, `OROC2`, `OROC3`, or
a specific sector number.
