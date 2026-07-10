#!/usr/bin/env bash
# run_kr_sim.sh — 83mKr TPC calibration simulation
#
# ARCHITECTURE:
#   GeneratorExternal always loads fileName via CLING (gROOT->ProcessLine).
#   Passing a .dylib directly fails because CLING can't see its symbols.
#   Solution: GeneratorKrDecayLoader.C is a tiny CLING macro that calls
#   gSystem->Load() on the compiled .dylib, then resolves the factory
#   function via dlsym. This gives us compiled performance with CLING compat.
#
# Setup: run ./build_kr_generator.sh once first.
# Then:  ./run_kr_sim.sh -n 1000

NEVENTS=5000
NWORKERS=4
ENGINE="TGeant4"
OUTPUT_PREFIX="o2sim_kr"
N_PER_EVENT=1000
ECUT_GEV=0.000001   # 1 keV default (CUTELE and CUTGAM)
PHYSLIST="FTFP_BERT_LIV"           # Geant4 physics list (Livermore EM — best for keV-scale Kr)
FIELD_KGAUSS=0                     # magnetic field in kGauss (0=no field, 5=nominal ALICE 0.5T)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOADER="${SCRIPT_DIR}/macros/GeneratorKrDecayLoader.C"
MACRO="${SCRIPT_DIR}/macros/GeneratorKrDecay.C"
LIB_DYLIB="/tmp/libGeneratorKrDecay.dylib"
LIB_SO="/tmp/libGeneratorKrDecay.so"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -n|--nevents)    NEVENTS="$2";       shift 2 ;;
    -w|--nworkers)   NWORKERS="$2";      shift 2 ;;
    --engine)        ENGINE="$2";        shift 2 ;;
    -o|--output)     OUTPUT_PREFIX="$2"; shift 2 ;;
    -d|--nPerEvent)     N_PER_EVENT="$2";   shift 2 ;;
    --ecut)             ECUT_GEV="$2";      shift 2 ;;
    --physlist)         PHYSLIST="$2";      shift 2 ;;
    --field)            FIELD_KGAUSS="$2";  shift 2 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

if ! command -v o2-sim &>/dev/null; then
  echo "ERROR: o2-sim not found. Load an O2 environment first."; exit 1
fi

# Choose generator source
if [[ -f "$LOADER" ]] && ([[ -f "$LIB_DYLIB" ]] || [[ -f "$LIB_SO" ]]); then
  GENERATOR_PATH="$LOADER"
  echo "[INFO] Using compiled library via loader macro (5 workers, stable)"
elif [[ -f "$MACRO" ]]; then
  GENERATOR_PATH="$MACRO"
  NWORKERS=2
  echo "[WARN] No compiled library — using interpreted macro (2 workers only)"
  echo "[WARN] Run ./build_kr_generator.sh for full performance"
else
  echo "ERROR: No generator found. Run ./build_kr_generator.sh first."; exit 1
fi

LOG_FILE="${OUTPUT_PREFIX}_sim.log"
> "$LOG_FILE"

pkill -f "o2-sim-hit-merger-runner" 2>/dev/null || true
pkill -f "o2-sim-primary-server"    2>/dev/null || true
pkill -f "o2-sim-device-runner"     2>/dev/null || true
pkill -f "o2-sim-worker"            2>/dev/null || true
sleep 2

# Clean up previous output files — the merger crashes if it tries to
# update a corrupt or incomplete ROOT file from a previous failed run
echo "[INFO] Cleaning up previous output files..." | tee -a "$LOG_FILE"
rm -f "${OUTPUT_PREFIX}_HitsTPC.root" \
      "${OUTPUT_PREFIX}_Kine.root" \
      "${OUTPUT_PREFIX}_MCHeader.root" \
      "${OUTPUT_PREFIX}_geometry.root" \
      "${OUTPUT_PREFIX}_geometry-aligned.root" \
      "${OUTPUT_PREFIX}_grp.root" \
      "${OUTPUT_PREFIX}_grpMagField.root" \
      "${OUTPUT_PREFIX}_grpecs.root" \
      "${OUTPUT_PREFIX}_grplhcif.root" \
      "${OUTPUT_PREFIX}_configuration.ini" \
      "${OUTPUT_PREFIX}_serverlog" \
      "${OUTPUT_PREFIX}_mergerlog" \
      "${OUTPUT_PREFIX}_workerlog"*
rm -f o2sim_Kine.root o2sim_HitsTPC.root o2sim_MCHeader.root \
      o2sim_grp.root o2sim_grpMagField.root o2sim_grpecs.root o2sim_grplhcif.root \
      o2sim_geometry.root o2sim_geometry-aligned.root o2sim_configuration.ini

# Copy loader macro to /tmp to handle spaces in path
SAFE_PATH="/tmp/GeneratorKrDecay_loader.C"
cp "$GENERATOR_PATH" "$SAFE_PATH"

# Physics: FTFP_BERT_LIV (Livermore EM) with custom g4config.
# Key settings in kr_g4config.in:
#   /mcPhysics/rangeCuts 0.0001 mm — TG4RegionsManager2 runs in energy-cut mode and
#                                     overrides this for named regions via simcuts.dat.
#                                     The actual 1 keV threshold for TPC drift gas comes
#                                     from Detector.cxx::SetSpecialPhysicsCuts() setting
#                                     CUTELE=1 keV BEFORE simcuts.dat is read (first-wins
#                                     rule in TG4GeometryManager). rangeCuts is kept because
#                                     it empirically improves resolution by ~0.9% (likely via
#                                     Livermore data-driven DEDX tables for delta-ray production).
#   /process/em/deexcitationIgnoreCut → Auger/fluorescence tracked regardless of production cut
#   /process/eLoss/StepFunction 0.2 0.1 mm → limits each step to 20% energy loss, fine steps
#                                below 0.1 mm; closes the gap to G4 standalone (8.0% vs 7.95%)
#   Optical commands removed  → FTFP_BERT_LIV has no optical physics; those commands
#                                caused "Batch is interrupted!!" skipping transportationWithMsc,
#                                skipUnknownParticles, and looper threshold commands.
#
# VMC cuts:  CUTELE=1 keV, CUTGAM=1 keV — global G4UserSpecialCuts kill threshold.
#            Backed by the SetSpecialPhysicsCuts override in Detector.cxx.
#            CUTGAM=1 keV is needed: Kα (12.6 keV) and T2-γ (9.4 keV) must propagate
#            to form separate clusters; a 10 keV CUTGAM from simcuts.dat would kill them.
# DRAY/stepFiltering: removed — both confirmed to have no effect on spectrum with
#            UseGeant4Edep=1 (delta rays are handled internally by Penelope EM).
export KR_N_PER_EVENT="${N_PER_EVENT}"
CV="GeneratorExternal.fileName=${SAFE_PATH};GeneratorExternal.funcName=GeneratorKrDecay();align-geom.mDetectors=none;G4.physicsmode=kUSER;G4.userPhysicsList=${PHYSLIST};G4.configMacroFile=${SCRIPT_DIR}/kr_g4config.in;GlobalSimProcs.CUTELE=${ECUT_GEV};GlobalSimProcs.CUTGAM=${ECUT_GEV};TPCDetParam.UseGeant4Edep=1;GenTPCLoopers.loopersVeto=true"

{
  echo "========================================="
  echo "  83mKr TPC calibration simulation"
  echo "========================================="
  echo "  Events   : ${NEVENTS}"
  echo "  Kr/event : ${N_PER_EVENT}  (KR_N_PER_EVENT=${N_PER_EVENT})"
  echo "  Workers  : ${NWORKERS}"
  echo "  Engine   : ${ENGINE}"
  echo "  Field    : ${FIELD_KGAUSS} kGauss"
  echo "  Generator: ${GENERATOR_PATH}"
  echo "  Output   : ${OUTPUT_PREFIX}"
  echo "  Log      : ${LOG_FILE}"
  echo "========================================="
} | tee -a "$LOG_FILE"

echo "+ o2-sim -g external -e ${ENGINE} -n ${NEVENTS} -m TPC --field ${FIELD_KGAUSS} --nworkers ${NWORKERS} -o ${OUTPUT_PREFIX} --configKeyValues ..." \
  | tee -a "$LOG_FILE"

# Route o2-sim output through awk to prepend HH:MM:SS timestamps to every line.
# FIFO keeps SIM_PID pointing at o2-sim itself, not the awk subprocess.
_LOG_FIFO=$(mktemp -u /tmp/kr_log_XXXXXX)
mkfifo "$_LOG_FIFO"
awk '{ print strftime("[%H:%M:%S]"), $0; fflush() }' "$_LOG_FIFO" >> "$LOG_FILE" &

o2-sim -g external -e "${ENGINE}" \
  -n "${NEVENTS}" \
  -m TPC \
  --field "${FIELD_KGAUSS}" \
  --nworkers "${NWORKERS}" \
  -o "${OUTPUT_PREFIX}" \
  --timestamp 1 \
  --configKeyValues "${CV}" \
  > "$_LOG_FIFO" 2>&1 &
SIM_PID=$!

# Monitor log for event completion (O2 writes "EVENT FINISHED : N" once per event).
# Print to terminal every 100 events; all detailed output goes to $LOG_FILE.
# Watchdog: if no new events for 5 minutes, kill any worker for this run using >80% CPU.
last_milestone=0
last_completed=0
last_progress_time=$(date +%s)
STALL_TIMEOUT=300

while kill -0 "$SIM_PID" 2>/dev/null; do
  sleep 5
  completed=$(grep -c "EVENT FINISHED" "$LOG_FILE" 2>/dev/null || true)
  completed=${completed:-0}

  milestone=$(( (completed / 100) * 100 ))
  if (( milestone > last_milestone && milestone > 0 )); then
    echo "[PROGRESS] ${milestone} / ${NEVENTS} events simulated"
    last_milestone=$milestone
  fi

  if (( completed > last_completed )); then
    last_completed=$completed
    last_progress_time=$(date +%s)
  fi

  # Only activate watchdog after first event completes
  if (( last_completed > 0 )); then
    stalled=$(( $(date +%s) - last_progress_time ))
    if (( stalled > STALL_TIMEOUT )); then
      echo "[WARN] No new events for ${stalled}s — killing high-CPU workers" | tee -a "$LOG_FILE"
      ps aux | awk -v topology="o2simtopology_${SIM_PID}" \
        '$0 ~ "o2-sim-device-runner" && $0 ~ topology && $3 > 80 {print $2}' | \
        while read -r stuck_pid; do
          echo "[WARN] Killed stuck worker PID ${stuck_pid}" | tee -a "$LOG_FILE"
          kill "$stuck_pid" 2>/dev/null
        done
      last_progress_time=$(date +%s)
    fi
  fi
done

wait "$SIM_PID"
SIM_EXIT=$?
rm -f "$_LOG_FIFO"

completed=$(grep -c "EVENT FINISHED" "$LOG_FILE" 2>/dev/null || true)
completed=${completed:-0}
echo "[PROGRESS] Done — ${completed} / ${NEVENTS} events simulated. Full log: ${LOG_FILE}"

if [[ $SIM_EXIT -ne 0 ]]; then
  if [[ -s "${OUTPUT_PREFIX}_HitsTPC.root" && -s "${OUTPUT_PREFIX}_Kine.root" ]]; then
    echo "[INFO] Non-zero exit but output files exist." | tee -a "$LOG_FILE"
    echo "[INFO] Check: root -l ${OUTPUT_PREFIX}_HitsTPC.root" | tee -a "$LOG_FILE"
    echo "[INFO]        o2sim->Draw(\"TPCHitsShiftedSector0.mHitsEVctr\")" | tee -a "$LOG_FILE"
  else
    echo "[ERROR] Output files missing. Check ${OUTPUT_PREFIX}_serverlog." | tee -a "$LOG_FILE"
    exit 1
  fi
fi

# Post-processing: set up the names the digitizer expects (mirrors run_kr_batches.sh)
echo "" | tee -a "$LOG_FILE"
echo "[POST] Setting up digitizer inputs..." | tee -a "$LOG_FILE"

# GRP/geometry/configuration carry no per-event data — copy with the o2sim_ prefix
for SUFFIX in grp.root grpMagField.root grpecs.root grplhcif.root \
              geometry.root geometry-aligned.root configuration.ini; do
  SRC="${OUTPUT_PREFIX}_${SUFFIX}"
  DST="o2sim_${SUFFIX}"
  if [[ -f "$SRC" ]]; then
    cp -f "$SRC" "$DST"
    echo "[POST]   Copied   : ${DST}" | tee -a "$LOG_FILE"
  fi
done

# Physics files: symlink so originals are preserved
ln -sf "${OUTPUT_PREFIX}_Kine.root"    o2sim_Kine.root
ln -sf "${OUTPUT_PREFIX}_HitsTPC.root" o2sim_HitsTPC.root
echo "[POST]   Symlinked: o2sim_Kine.root    -> ${OUTPUT_PREFIX}_Kine.root" | tee -a "$LOG_FILE"
echo "[POST]   Symlinked: o2sim_HitsTPC.root -> ${OUTPUT_PREFIX}_HitsTPC.root" | tee -a "$LOG_FILE"
if [[ -f "${OUTPUT_PREFIX}_MCHeader.root" ]]; then
  ln -sf "${OUTPUT_PREFIX}_MCHeader.root" o2sim_MCHeader.root
  echo "[POST]   Symlinked: o2sim_MCHeader.root -> ${OUTPUT_PREFIX}_MCHeader.root" | tee -a "$LOG_FILE"
fi

echo "" | tee -a "$LOG_FILE"
echo "[POST] Files ready for digitizer:" | tee -a "$LOG_FILE"
ls -lh o2sim_Kine.root o2sim_HitsTPC.root o2sim_grp.root \
        o2sim_geometry.root o2sim_configuration.ini 2>/dev/null | tee -a "$LOG_FILE"
