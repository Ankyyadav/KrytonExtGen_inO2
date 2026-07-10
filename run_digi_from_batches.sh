#!/usr/bin/env bash
# run_digi_from_batches.sh
#
# Digitize each simulation batch produced by run_kr_batches.sh independently,
# giving each batch a fresh CM baseline estimator. Output (tpcdigits.root,
# log.digi) is written directly into the same batch subdirectory as the
# simulation files — no symlinks or separate directories needed.
#
# tpcdigits.root files are NOT merged — hadd inflates them from ~65 MB to
# ~100 GB due to TPC CommonMode branch recompression. Use findKrBoxCluster.C
# with the batch glob to cluster all batches in one pass.
#
# Usage (from scan/run directory):
#   ../run_digi_from_batches.sh [--batch-dir kr_batches]
#
# Options:
#   --batch-dir DIR   directory containing batch_* subdirs (default: kr_batches)

set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BATCH_DIR="kr_batches"
O2_PPM=3
DIGI_MODE="ZeroSuppressionCMCorr"
EXTRA_KV=""
SHM_SIZE=$(( 12 << 30 )) # 12 GB default; DPL uses up to the system /dev/shm limit

while [[ $# -gt 0 ]]; do
  case "$1" in
    --batch-dir)        BATCH_DIR="$2";   shift 2 ;;
    --o2ppm)            O2_PPM="$2";      shift 2 ;;
    --digi-mode)        DIGI_MODE="$2";   shift 2 ;;
    --extra-kv)         EXTRA_KV="$2";    shift 2 ;;
    --shm-size)         SHM_SIZE="$2";    shift 2 ;;
    -h|--help)
      sed -n '2,/^set -o/p' "$0" | grep '^#' | sed 's/^# \?//'; exit 0 ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
done

# ── Find batch subdirectories ─────────────────────────────────────────────────
mapfile -t BATCH_DIRS < <(ls -d "${BATCH_DIR}"/batch_*/ 2>/dev/null \
  | sort | while read -r d; do
    [[ -s "${d}o2sim_Kine.root" ]] && echo "$d"
  done)

N=${#BATCH_DIRS[@]}
if (( N == 0 )); then
  echo "ERROR: no valid batch_* subdirs found in ${BATCH_DIR}/"
  echo "       Each batch dir must contain o2sim_Kine.root"
  exit 1
fi

echo "================================================="
echo "  83mKr batch digitizer"
echo "================================================="
echo "  Batches     : ${N}"
echo "  Batch dir   : ${BATCH_DIR}/"
echo "  O2 content  : ${O2_PPM} ppm"
echo "  DigiMode    : ${DIGI_MODE}"
echo "================================================="

# ── Process each batch ─────────────────────────────────────────────────────────
for (( i=0; i<N; i++ )); do
  BDIR="${BATCH_DIRS[$i]%/}"          # strip trailing slash
  BNAME=$(basename "$BDIR")

  echo ""
  echo "[DIGI] ── Batch $((i+1))/${N}: ${BNAME} ──────────────────────────"

  if [[ -s "${BDIR}/tpcdigits.root" ]]; then
    echo "[DIGI]   Already done — skipping ($(du -sh "${BDIR}/tpcdigits.root" | cut -f1))"
    continue
  fi

  # All o2sim_* files are already in BDIR from the simulation step — just run.
  pushd "$BDIR" > /dev/null
  DIGI_ARGS=(--digi-only --o2ppm "$O2_PPM" --digi-mode "$DIGI_MODE" --shm-size "$SHM_SIZE")
  [[ -n "$EXTRA_KV" ]] && DIGI_ARGS+=(--extra-kv "$EXTRA_KV")
  "${SCRIPT_DIR}/run_kr_digi_clust.sh" "${DIGI_ARGS[@]}"
  DIGI_EXIT=$?
  popd > /dev/null

  if [[ -s "${BDIR}/tpcdigits.root" ]]; then
    echo "[DIGI]   OK — tpcdigits.root ready ($(du -sh "${BDIR}/tpcdigits.root" | cut -f1))"
  else
    echo "[ERROR] Batch $((i+1)) digitizer failed (exit=${DIGI_EXIT})"
    echo "        See ${BDIR}/log.digi"
    tail -8 "${BDIR}/log.digi"
    exit 1
  fi
done

echo ""
echo "[DIGI] All ${N} batches complete."
echo ""
echo "  Next step — cluster all batches in one pass:"
echo "    root -b -l -q '../macros/findKrBoxCluster.C+'"
