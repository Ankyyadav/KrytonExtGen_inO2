#!/usr/bin/env bash
# run_kr_digi_clust.sh
#
# Runs the full post-simulation chain for 83mKr TPC calibration:
#   Step 1 — Digitizer: hits -> tpcdigits.root
#   Step 2 — Cluster finder: tpcdigits.root -> tpcBoxClusters.root
#
# Prerequisites (all produced by run_kr_batches.sh):
#   o2sim_Kine.root, o2sim_HitsTPC.root, o2sim_grp.root,
#   o2sim_geometry.root, o2sim_configuration.ini
#
# Usage:
#   ./run_kr_digi_clust.sh                         # run both steps
#   ./run_kr_digi_clust.sh --digi-only             # digitizer only
#   ./run_kr_digi_clust.sh --clust-only            # cluster finder only
#   ./run_kr_digi_clust.sh --lanes 8               # TPC lanes (default 4)
#   ./run_kr_digi_clust.sh --o2ppm 0               # O2 content in ppm (default 3)
#
# DigiMode options (Option A — quick fix for CM accumulation bug):
#   ./run_kr_digi_clust.sh --digi-mode ZeroSuppression
#       Skips common-mode correction entirely. Avoids the progressive charge
#       degradation seen with ZeroSuppressionCMCorr for >~100k events.
#   ./run_kr_digi_clust.sh --digi-mode ZeroSuppressionCMCorr   [default]
#       Applies CM correction. Correct for small event counts; accumulates
#       a growing baseline across events — do not use for >~50k events.
#
# Chunked digitization (Option B — correct fix for large event counts):
#   ./run_kr_digi_clust.sh --chunk-events 50000
#       Splits the input into chunks of 50000 events, digitizes each chunk
#       independently (fresh CM baseline per chunk), then merges tpcdigits.root.
#       Use when total events > ~50k with ZeroSuppressionCMCorr.
#   ./run_kr_digi_clust.sh --chunk-events 50000 --keep-chunks
#       Keep per-chunk digi_chunks/ directory for debugging.
#
# Continuous mode (Option C — investigation):
#   ./run_kr_digi_clust.sh --no-triggered --interaction-rate 50000
#       Removes --TPCtriggered, adds --interactionRate 50000 Hz.
#       Packs ~570 events per TF (~16/sector/TF) so the CM estimator
#       sees abundant signal per entry — avoids the sparse-triggered instability.
#       Physical justification: in a real Kr calibration run, the source
#       diffuses continuously; decays happen at a known average rate.

set -o pipefail

# ── Defaults ──────────────────────────────────────────────────────────────────
DIGI_ONLY=0
CLUST_ONLY=0
TPC_LANES=4
INI_FILE="krBoxCluster.largeBox.cuts.krMap.ini"
SHM_SIZE=$(( 12 << 30 )) # 12 GB default; DPL uses up to the system /dev/shm limit
O2_PPM=3
DIGI_MODE="ZeroSuppressionCMCorr"   # Option A: change to ZeroSuppression
CHUNK_EVENTS=0                       # Option B: 0 = no chunking
KEEP_CHUNKS=0
TRIGGERED=1                          # 1 = --TPCtriggered (default), 0 = continuous mode
INTERACTION_RATE=50000               # Hz; used only when TRIGGERED=0
EXTRA_KV=""                          # extra configKeyValues appended to DIGI_CONFIGKV

# ── Argument parsing ───────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
  case "$1" in
    --digi-only)      DIGI_ONLY=1;         shift ;;
    --clust-only)     CLUST_ONLY=1;        shift ;;
    --lanes)          TPC_LANES="$2";      shift 2 ;;
    --ini)            INI_FILE="$2";       shift 2 ;;
    --o2ppm)          O2_PPM="$2";         shift 2 ;;
    --digi-mode)        DIGI_MODE="$2";         shift 2 ;;
    --chunk-events)     CHUNK_EVENTS="$2";      shift 2 ;;
    --keep-chunks)      KEEP_CHUNKS=1;          shift ;;
    --no-triggered)     TRIGGERED=0;            shift ;;
    --interaction-rate) INTERACTION_RATE="$2";  shift 2 ;;
    --extra-kv)         EXTRA_KV="$2";          shift 2 ;;
    --shm-size)         SHM_SIZE="$2";          shift 2 ;;
    -h|--help)
      echo "Usage: $0 [--digi-only] [--clust-only] [--lanes N] [--o2ppm PPM]"
      echo "          [--digi-mode MODE] [--chunk-events N] [--keep-chunks]"
      echo "          [--no-triggered] [--interaction-rate HZ]"
      echo "  --digi-mode       ZeroSuppression or ZeroSuppressionCMCorr (default)"
      echo "  --chunk-events N  digitize in chunks of N events (0=no chunking)"
      echo "  --no-triggered    continuous mode (removes --TPCtriggered, adds --interactionRate)"
      echo "  --interaction-rate HZ  interaction rate in Hz for continuous mode (default 50000)"
      exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# Convert ppm → fraction
O2_FRAC=$(awk "BEGIN { printf \"%.2e\", ${O2_PPM} * 1e-6 }")

# Common configKeyValues — shared by all digitizer invocations
DIGI_CONFIGKV="TPCEleParam.ADCdynamicRange=700;TPCEleParam.ADCsaturation=8192;TPCEleParam.ChipGain=1;TPCEleParam.applyDeadMap=false;TPCEleParam.doCommonModePerPad=false;TPCEleParam.DigiMode=${DIGI_MODE};TPCDetParam.ExcludeFCGap=false;TPCDetParam.DriftTimeOffset=0;TPCGasParam.OxygenCont=${O2_FRAC}"
[[ -n "$EXTRA_KV" ]] && DIGI_CONFIGKV="${DIGI_CONFIGKV};${EXTRA_KV}"

# ── Checks ─────────────────────────────────────────────────────────────────────
check_file() {
  [[ -f "$1" ]] || { echo "[ERROR] Required file not found: $1"; exit 1; }
}

if [[ $CLUST_ONLY -eq 0 ]]; then
  check_file "o2sim_Kine.root"
  check_file "o2sim_HitsTPC.root"
  check_file "o2sim_grp.root"
  check_file "o2sim_geometry.root"
  check_file "o2sim_configuration.ini"
fi

if [[ $DIGI_ONLY -eq 0 ]]; then
  [[ $CLUST_ONLY -eq 1 ]] && check_file "tpcdigits.root"
  check_file "$INI_FILE"
fi

echo "========================================="
echo "  83mKr digitizer + cluster finder"
echo "========================================="
if [[ $CLUST_ONLY -eq 0 ]]; then
  echo "  Step 1: Digitizer"
  echo "  DigiMode    : ${DIGI_MODE}"
  echo "  O2 content  : ${O2_PPM} ppm (OxygenCont=${O2_FRAC})"
  if [[ $TRIGGERED -eq 1 ]]; then
    echo "  Mode        : triggered (--TPCtriggered)"
  else
    echo "  Mode        : continuous (--interactionRate ${INTERACTION_RATE} Hz)"
  fi
  if [[ $CHUNK_EVENTS -gt 0 ]]; then
    echo "  Chunking    : ${CHUNK_EVENTS} events/chunk (Option B)"
  fi
fi
if [[ $DIGI_ONLY -eq 0 ]]; then
  echo "  Step 2: Cluster finder (${TPC_LANES} lanes)"
  echo "  Cluster config: ${INI_FILE}"
fi
echo "========================================="

# ── Helper: run one digitizer instance in the current directory ────────────────
_run_digi_here() {
  # Runs o2-sim-digitizer-workflow in CWD, streams progress, returns exit code.
  local label="${1:-}"
  local nevents_hint="${2:-0}"

  rm -f tpcdigits.root o2simdigitizerworkflow_configuration.ini

  local triggered_flag=""
  local rate_flag=""
  if [[ $TRIGGERED -eq 1 ]]; then
    triggered_flag="--TPCtriggered"
  else
    rate_flag="--interactionRate ${INTERACTION_RATE}"
  fi

  o2-sim-digitizer-workflow \
    $triggered_flag \
    $rate_flag \
    --tpc-lanes "$TPC_LANES" \
    --configKeyValues "$DIGI_CONFIGKV" \
    --shm-segment-size $SHM_SIZE \
    -b \
    &> log.digi &
  local pid=$!

  # "TPC: Event time" fires once per TF per lane (including zero-digit TFs that
  # "Flushed [^0]" would miss). Dividing by TPC_LANES gives true TF count.
  # TF count is NOT comparable to sim-event count: in triggered mode, 1000 sim
  # events with 1000 decays/event produce ~5x more TFs than sim events.
  local last_tf=0 start
  start=$(date +%s)
  while kill -0 "$pid" 2>/dev/null; do
    sleep 15
    local cnt_tf elapsed rate_str
    cnt_tf=$(grep -c "TPC: Event time" log.digi 2>/dev/null || true)
    cnt_tf=$(( TPC_LANES > 0 ? ${cnt_tf:-0} / TPC_LANES : ${cnt_tf:-0} ))
    elapsed=$(( $(date +%s) - start ))
    if (( cnt_tf > last_tf )); then
      if (( elapsed > 0 )); then
        rate_str=$(awk "BEGIN{printf \"%.1f\", ${cnt_tf}/${elapsed}}")
      else
        rate_str="?"
      fi
      printf "[DIGI]%s %ds | %d TFs  (%s TF/s)\n" \
        "${label:+ $label}" $elapsed $cnt_tf "$rate_str"
      last_tf=$cnt_tf
    fi
  done
  wait "$pid"
  local rc=$?

  local elapsed final_tf
  elapsed=$(( $(date +%s) - start ))
  final_tf=$(grep -c "TPC: Event time" log.digi 2>/dev/null || true)
  final_tf=$(( TPC_LANES > 0 ? ${final_tf:-0} / TPC_LANES : ${final_tf:-0} ))
  printf "[DIGI]%s Finished in %ds | %d TFs digitized (exit=%d)\n" \
    "${label:+ $label}" $elapsed "${final_tf}" $rc
  return $rc
}

# ── Step 1: Digitizer ──────────────────────────────────────────────────────────
if [[ $CLUST_ONLY -eq 0 ]]; then
  echo ""
  echo "[DIGI] Cleaning up stale FairMQ SHM segments..."
  find /dev/shm -maxdepth 1 -name 'fmq_*' -user "$(id -un)" -delete 2>/dev/null && true
  echo "[DIGI] Starting digitizer..."

  NEVENTS=$(root -b -q -l o2sim_Kine.root \
    -e 'auto t=(TTree*)gDirectory->Get("o2sim"); cout<<(t?t->GetEntries():-1)<<endl;' \
    2>/dev/null | tail -1)
  NEVENTS=${NEVENTS:-0}
  (( NEVENTS <= 0 )) && NEVENTS=0
  [[ "$NEVENTS" -gt 0 ]] && echo "[DIGI] Expecting ${NEVENTS} events"

  # ── Option B: Chunked digitization ──────────────────────────────────────────
  if [[ $CHUNK_EVENTS -gt 0 && $NEVENTS -gt 0 ]]; then
    N_CHUNKS=$(( (NEVENTS + CHUNK_EVENTS - 1) / CHUNK_EVENTS ))
    echo "[DIGI] Chunked mode: ${NEVENTS} events → ${N_CHUNKS} chunks of ≤${CHUNK_EVENTS}"

    CHUNK_BASE="./digi_chunks"
    mkdir -p "$CHUNK_BASE"
    CHUNK_DIGI_FILES=()
    PARENT_DIR="$(pwd)"

    for (( chunk=0; chunk<N_CHUNKS; chunk++ )); do
      FIRST=$(( chunk * CHUNK_EVENTS ))
      CHUNK_N=$(( NEVENTS - FIRST ))
      [[ $CHUNK_N -gt $CHUNK_EVENTS ]] && CHUNK_N=$CHUNK_EVENTS
      LAST=$(( FIRST + CHUNK_N - 1 ))

      CDIR="${CHUNK_BASE}/chunk_$(printf '%04d' $chunk)"
      mkdir -p "$CDIR"

      chunk_start=$(date +%s)
      echo ""
      echo "[DIGI] ── Chunk $((chunk+1))/${N_CHUNKS}  (ev ${FIRST}–${LAST}) ──────────────────"

      # CopyTree Kine.root and HitsTPC.root for this chunk's event range.
      # SimReader determines event count from the local Kine.root entry count,
      # so we do NOT symlink configuration.ini or MCHeader (which carry the full 500k count).
      local kine_in="${PARENT_DIR}/o2sim_Kine.root"
      local hits_in="${PARENT_DIR}/o2sim_HitsTPC.root"
      echo "[DIGI]   CopyTree Kine  events ${FIRST}–${LAST}..."
      root -b -l -q \
        -e "TFile* fi=TFile::Open(\"${kine_in}\"); TFile* fo=new TFile(\"${CDIR}/o2sim_Kine.root\",\"RECREATE\"); TTree* t=(TTree*)fi->Get(\"o2sim\"); t->CopyTree(\"\",\"\",${CHUNK_N},${FIRST}); fo->Write(); fo->Close(); fi->Close();" \
        > /dev/null 2>&1
      echo "[DIGI]   CopyTree HitsTPC events ${FIRST}–${LAST}..."
      root -b -l -q \
        -e "TFile* fi=TFile::Open(\"${hits_in}\"); TFile* fo=new TFile(\"${CDIR}/o2sim_HitsTPC.root\",\"RECREATE\"); TTree* t=(TTree*)fi->Get(\"o2sim\"); t->CopyTree(\"\",\"\",${CHUNK_N},${FIRST}); fo->Write(); fo->Close(); fi->Close();" \
        > /dev/null 2>&1

      # Verify CopyTree produced the right entry count
      local chunk_kine_n
      chunk_kine_n=$(root -b -q -l "${CDIR}/o2sim_Kine.root" \
        -e 'auto t=(TTree*)gDirectory->Get("o2sim"); cout<<(t?t->GetEntries():-1)<<endl;' \
        2>/dev/null | tail -1)
      echo "[DIGI]   Chunk Kine has ${chunk_kine_n} entries (expected ${CHUNK_N})"

      # Symlink geometry, GRP, polya from parent — but NOT configuration.ini or MCHeader
      for f in o2sim_grp.root o2sim_grpMagField.root o2sim_grpecs.root \
               o2sim_grplhcif.root o2sim_geometry.root o2sim_geometry-aligned.root \
               tpc_polya.root; do
        chunk_src=$(realpath "${PARENT_DIR}/$f" 2>/dev/null)
        [[ -n "$chunk_src" ]] && ln -sf "$chunk_src" "${CDIR}/$f" 2>/dev/null
      done

      echo "[DIGI]   Digitizing ${CHUNK_N} events..."

      # Run digitizer inside chunk directory (SimReader reads entry count from Kine.root)
      pushd "$CDIR" > /dev/null
      _run_digi_here "chunk$((chunk+1))/${N_CHUNKS}" "$CHUNK_N"
      CHUNK_EXIT=$?
      popd > /dev/null

      chunk_elapsed=$(( $(date +%s) - chunk_start ))
      if [[ -s "${CDIR}/tpcdigits.root" ]]; then
        echo "[DIGI]   Chunk $((chunk+1))/${N_CHUNKS} complete in ${chunk_elapsed}s"
        CHUNK_DIGI_FILES+=("${CDIR}/tpcdigits.root")
      else
        echo "[ERROR] Chunk $((chunk+1)) digitizer failed. See ${CDIR}/log.digi"
        tail -10 "${CDIR}/log.digi"
        exit 1
      fi
    done

    echo "[DIGI] Merging ${#CHUNK_DIGI_FILES[@]} chunk files..."
    hadd -f tpcdigits.root "${CHUNK_DIGI_FILES[@]}"

    [[ $KEEP_CHUNKS -eq 0 ]] && rm -rf "$CHUNK_BASE"

  # ── Single-run digitization (default / Option A with different mode) ─────────
  else
    _run_digi_here "" "$NEVENTS"
    DIGI_EXIT=$?
  fi

  if [[ ! -s tpcdigits.root ]]; then
    echo "[ERROR] Digitizer failed or produced no output."
    [[ -f log.digi ]] && tail -20 log.digi
    exit 1
  fi

  N_DIGI=$(root -b -q -l tpcdigits.root \
    -e 'auto t=(TTree*)gDirectory->Get("o2sim"); cout<<(t?t->GetEntries():-1)<<endl;' \
    2>/dev/null | tail -1)
  echo "[DIGI] tpcdigits.root: ${N_DIGI} TF entries"
  ls -lh tpcdigits.root
fi

# ── Step 2: Cluster finder ─────────────────────────────────────────────────────
if [[ $DIGI_ONLY -eq 0 ]]; then
  echo ""
  echo "[CLUST] Starting cluster finder (${TPC_LANES} lanes)..."

  check_file "o2simdigitizerworkflow_configuration.ini"

  rm -f tpcBoxClusters.root

  o2-tpc-reco-workflow \
    -b --session default --shm-segment-size $SHM_SIZE \
    --disable-mc \
    --input-type digits \
    --output-type "digits,disable-writer" \
    --configKeyValues "align-geom.mDetectors=none" \
    --disable-ctp-lumi-request \
    | o2-tpc-krypton-clusterer \
    -b --session default --shm-segment-size $SHM_SIZE \
    --lanes "$TPC_LANES" \
    --severity info \
    --configFile "$INI_FILE" \
    | o2-dpl-run \
    -b --session default --shm-segment-size $SHM_SIZE \
    --run \
    &> log.clust

  CLUST_EXIT=$?

  if [[ ! -s tpcBoxClusters.root ]]; then
    echo "[ERROR] Cluster finder failed or produced no output. See log.clust"
    tail -20 log.clust
    exit 1
  fi

  echo "[CLUST] Done. tpcBoxClusters.root produced (exit=${CLUST_EXIT})"
  ls -lh tpcBoxClusters.root

  cat > /tmp/kr_quick_count.C << 'EOF'
void kr_quick_count() {
  gSystem->Load("libO2TPCReconstruction");
  gSystem->Load("libO2DataFormatsTPC");
  auto f = TFile::Open("tpcBoxClusters.root");
  auto t = (TTree*)f->Get("Clusters");
  if (!t) { cout << "No Clusters tree found" << endl; return; }
  Long64_t total = 0;
  for (int s = 0; s < 36; s++) {
    std::vector<o2::tpc::KrCluster> *cl = nullptr;
    t->SetBranchAddress(Form("TPCBoxCluster_%d", s), &cl);
    for (Long64_t ev = 0; ev < t->GetEntries(); ev++) {
      t->GetEntry(ev);
      if (cl) total += cl->size();
    }
    t->ResetBranchAddresses();
  }
  cout << "Total Kr clusters: " << total << endl;
  cout << "Tree entries (timeframes): " << t->GetEntries() << endl;
}
EOF
  root -b -q /tmp/kr_quick_count.C 2>/dev/null | grep -E "Total|Tree entries"
fi

echo ""
echo "========================================="
echo "  Done."
[[ $CLUST_ONLY -eq 0 ]] && echo "  Digits  : tpcdigits.root       (log: log.digi)"
[[ $DIGI_ONLY  -eq 0 ]] && echo "  Clusters: tpcBoxClusters.root  (log: log.clust)"
echo "========================================="
