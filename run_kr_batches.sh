#!/usr/bin/env bash
# run_kr_batches.sh
#
# Simulate Kr-83m decays in independent batches. Each batch gets its own
# subdirectory containing all o2sim_* output files. No merging is done —
# the digitizer (run_digi_from_batches.sh) processes each batch independently
# so the CM baseline resets between batches.
#
# Usage (from scan/run directory):
#   ../run_kr_batches.sh -e 25000 -b 40
#
# Options:
#   -e|--events N       events per batch (default: 25000)
#   -b|--batches N      number of batches (default: 40)
#   --batch-dir DIR     output directory (default: kr_batches)

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EVENTS_PER_BATCH=25000
N_BATCHES=40
BATCH_DIR="kr_batches"
N_PER_EVENT=1000
ECUT_GEV=0.000001          # 1 keV default (CUTELE and CUTGAM)
PHYSLIST="FTFP_BERT_LIV"   # Geant4 physics list (Livermore EM — best for keV-scale Kr)
FIELD_KGAUSS=0             # magnetic field in kGauss (0=no field, 5=nominal ALICE 0.5T)

while [[ $# -gt 0 ]]; do
  case "$1" in
    -e|--events)    EVENTS_PER_BATCH="$2"; shift 2 ;;
    -b|--batches)   N_BATCHES="$2";        shift 2 ;;
    --batch-dir)    BATCH_DIR="$2";        shift 2 ;;
    --nPerEvent)    N_PER_EVENT="$2";      shift 2 ;;
    --ecut)         ECUT_GEV="$2";         shift 2 ;;
    --physlist)     PHYSLIST="$2";         shift 2 ;;
    --field)        FIELD_KGAUSS="$2";     shift 2 ;;
    -h|--help)
      sed -n '2,/^set -o/p' "$0" | grep '^#' | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

TOTAL_EVENTS=$(( N_BATCHES * EVENTS_PER_BATCH ))

# ── Environment ───────────────────────────────────────────────────────────────
export FAIRMQ_TRANSPORT=zeromq
export JALIEN_TOKEN_CERT=""
export X509_USER_PROXY=""
unset DYLD_INSERT_LIBRARIES

if ! command -v o2-sim &>/dev/null; then
  echo "ERROR: o2-sim not found. Enter an O2 alienv first."; exit 1
fi

# Generator loader copied to /tmp — path with spaces breaks CLING's LoadMacro
LOADER_SRC="${SCRIPT_DIR}/macros/GeneratorKrDecayLoader.C"
GENERATOR_PATH="/tmp/GeneratorKrDecay_loader.C"
[[ -f "$LOADER_SRC" ]] || { echo "ERROR: loader not found at ${LOADER_SRC}"; exit 1; }
cp "$LOADER_SRC" "$GENERATOR_PATH"

export KR_N_PER_EVENT="${N_PER_EVENT}"
CV="GeneratorExternal.fileName=${GENERATOR_PATH};GeneratorExternal.funcName=GeneratorKrDecay();align-geom.mDetectors=none;G4.physicsmode=kUSER;G4.userPhysicsList=${PHYSLIST};G4.configMacroFile=${SCRIPT_DIR}/kr_g4config.in;GlobalSimProcs.CUTELE=${ECUT_GEV};GlobalSimProcs.CUTGAM=${ECUT_GEV};TPCDetParam.UseGeant4Edep=1;GenTPCLoopers.loopersVeto=true"

mkdir -p "$BATCH_DIR"

echo "========================================="
echo "  83mKr batch simulation"
echo "========================================="
echo "  Batches        : ${N_BATCHES}"
echo "  Events / batch : ${EVENTS_PER_BATCH}"
echo "  Total events   : ${TOTAL_EVENTS}"
echo "  Kr/event       : ${N_PER_EVENT}"
echo "  Physics list   : ${PHYSLIST}"
echo "  Field          : ${FIELD_KGAUSS} kGauss"
echo "  eCut           : ${ECUT_GEV} GeV"
echo "  Batch dir      : ${BATCH_DIR}/"
echo "========================================="

# ── Validation helper ─────────────────────────────────────────────────────────
validate_batch() {
  local bdir="$1"
  [[ -s "${bdir}/o2sim_Kine.root" ]]    || return 1
  [[ -s "${bdir}/o2sim_HitsTPC.root" ]] || return 1
  local n
  n=$(root -b -q -l "${bdir}/o2sim_Kine.root" \
    -e 'auto t=(TTree*)gDirectory->Get("o2sim"); cout<<(t?t->GetEntries():-1)<<endl;' \
    2>/dev/null | tail -1)
  [[ "$n" =~ ^[0-9]+$ ]] && [[ "$n" -eq "$EVENTS_PER_BATCH" ]]
}

# ── Simulation loop ───────────────────────────────────────────────────────────
SUCCEEDED=0
FAILED=0

for (( i=1; i<=N_BATCHES; i++ )); do
  BDIR="${BATCH_DIR}/batch_$(printf '%04d' $i)"
  mkdir -p "$BDIR"

  if validate_batch "$BDIR"; then
    echo "[SKIP] Batch ${i}/${N_BATCHES} already complete."
    (( SUCCEEDED++ ))
    continue
  fi

  # Remove partial outputs from a previous failed attempt
  rm -f "${BDIR}"/o2sim_*.root "${BDIR}"/o2sim_*.ini \
        "${BDIR}"/serverlog "${BDIR}"/mergerlog "${BDIR}"/workerlog* \
        "${BDIR}"/simlog

  echo -n "[RUN ] Batch ${i}/${N_BATCHES} (${EVENTS_PER_BATCH} events)... "

  # Run inside the batch directory so all o2sim_* files land there directly
  pushd "$BDIR" > /dev/null
  o2-sim -g external -e TGeant4 \
    -n "$EVENTS_PER_BATCH" \
    -m TPC \
    --field "${FIELD_KGAUSS}" \
    --nworkers 1 \
    --CCDBUrl http://alice-ccdb.cern.ch \
    --configKeyValues "$CV" \
    --timestamp 1 \
    > simlog 2>&1
  SIM_EXIT=$?
  popd > /dev/null

  if validate_batch "$BDIR"; then
    echo "OK  (exit=${SIM_EXIT})"
    (( SUCCEEDED++ ))
    # Remove non-essential log files to save space
    rm -f "${BDIR}"/serverlog "${BDIR}"/mergerlog "${BDIR}"/workerlog* \
          "${BDIR}"/o2sim_proc-cut.dat
  else
    local_n=0
    if [[ -s "${BDIR}/o2sim_Kine.root" ]]; then
      local_n=$(root -b -q -l "${BDIR}/o2sim_Kine.root" \
        -e 'auto t=(TTree*)gDirectory->Get("o2sim"); cout<<(t?t->GetEntries():0)<<endl;' \
        2>/dev/null | tail -1)
      [[ "$local_n" =~ ^[0-9]+$ ]] || local_n=0
    fi
    echo "FAIL (exit=${SIM_EXIT}, events=${local_n})"
    (( FAILED++ ))
  fi

  if (( i % 10 == 0 )); then
    echo "--- Progress: ${i}/${N_BATCHES} | ${SUCCEEDED} OK | ${FAILED} failed ---"
  fi
done

echo ""
echo "========================================="
echo "  Simulation complete"
echo "  Succeeded : ${SUCCEEDED} / ${N_BATCHES}"
echo "  Failed    : ${FAILED} / ${N_BATCHES}"
echo "  Good events: $(( SUCCEEDED * EVENTS_PER_BATCH ))"
echo "========================================="
echo ""
echo "  Next step — digitize each batch:"
echo "    ../run_digi_from_batches.sh"
