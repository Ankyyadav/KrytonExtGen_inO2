// GeneratorKrDecay.cxx
// Compiled 83mKr TPC calibration generator for ALICE O2.

#include "GeneratorKrDecay.h"
#include "FairGenerator.h"
#include "TDatabasePDG.h"
#include "TParticle.h"
#include "TParticlePDG.h"
#include "TRandom.h"
#include "TMath.h"
#include <cmath>
#include <iostream>

// ── KrDecayTable ─────────────────────────────────────────────────────────

// ── 83mKr decay physics ──────────────────────────────────────────────────
//
// Two sequential transitions:
//   T1: 32.151 keV  (e−/γ ≈ 2000 → essentially 100% IC)
//       77% outer-shell (L/M/N) IC: CE + Auger cascade → full 32.151 keV local
//       23% K-shell IC: K-CE = 32.151 − 14.326 = 17.825 keV
//           34.8% K-Auger: KLL (10.484 keV) + residual cascade (3.842 keV)
//                          → full E_K = 14.326 keV re-deposited, total = 32.151 keV local
//           65.2% K-fluorescence: Kα (12.600 keV) escapes as photon
//                          L-Auger (1.726 keV) local (= E_K − Kα, energy conservation)
//                          → only 17.825 + 1.726 = 19.551 keV from T1 locally
//
//   T2: 9.396 keV  (e−/γ ≈ 20 → 95% IC, 5% γ)
//       95% L/M/N-shell IC: full 9.396 keV local via CE + Auger
//        5% γ emission: 9.396 keV photon escapes (may convert elsewhere)
//
// Six channels = T1-mode × T2-mode product.  T1-γ (0.05%) is neglected.
// All energies in GeV (ROOT convention; e.g. 30.230e-6 GeV = 30.230 keV).
//
// Expected local-deposit peaks:
//   41.6 keV (80.75%) — T1 outer-CE or T1 K-CE+K-Auger, T2-IC
//   32.2 keV  (4.25%) — T1 any-mode, T2-γ escapes
//   29.1 keV (14.25%) — T1 K-CE + Kα escapes + T2-IC
//   19.6 keV  (0.75%) — T1 K-CE + Kα escapes + T2-γ escapes
// Separate clusters from escaped photons:
//   12.6 keV (15.0%)  — Kα converting elsewhere in gas
//    9.4 keV  (5.0%)  — T2-γ converting elsewhere in gas
// ─────────────────────────────────────────────────────────────────────────

KrDecayTable::KrDecayTable()
{
  int i = 0;

  // Ch 0 (73.15%): T1 L-CE + T2 L-CE → 41.6 keV local
  // L-CE(T1)=30.230 keV, L-Auger(T1)=1.921 keV, L-CE(T2)=7.475 keV, L-Auger(T2)=1.921 keV
  channels[i] = {0.7315, 4, {{11,30.230e-6},{11,1.921e-6},{11,7.475e-6},{11,1.921e-6}}}; i++;

  // Ch 1 (3.85%): T1 L-CE + T2 γ → 32.2 keV local + γ(9.4 keV) separate cluster
  // L-CE(T1)=30.230 keV, L-Auger(T1)=1.921 keV, γ(T2)=9.396 keV
  channels[i] = {0.0385, 3, {{11,30.230e-6},{11,1.921e-6},{22,9.396e-6}}}; i++;

  // Ch 2 (7.60%): T1 K-CE + K-Auger(full) + T2 L-CE → 41.6 keV local
  // K-CE=17.825 keV, KLL-Auger=10.484 keV, residual-Auger=3.842 keV,
  // L-CE(T2)=7.475 keV, L-Auger(T2)=1.921 keV
  channels[i] = {0.0760, 5, {{11,17.825e-6},{11,10.484e-6},{11,3.842e-6},{11,7.475e-6},{11,1.921e-6}}}; i++;

  // Ch 3 (14.25%): T1 K-CE + K-fluorescence + T2 L-CE → 29.1 keV local + Kα(12.6 keV) separate
  // K-CE=17.825 keV, L-Auger-after-Kα=1.726 keV (=E_K−Kα, energy conservation),
  // Kα=12.600 keV (photon, escapes → separate cluster), L-CE(T2)=7.475 keV, L-Auger(T2)=1.921 keV
  channels[i] = {0.1425, 5, {{11,17.825e-6},{11,1.726e-6},{22,12.600e-6},{11,7.475e-6},{11,1.921e-6}}}; i++;

  // Ch 4 (0.75%): T1 K-CE + K-fluorescence + T2 γ → 19.6 keV local + Kα + T2-γ separate
  // K-CE=17.825 keV, L-Auger=1.726 keV, Kα=12.600 keV, γ(T2)=9.396 keV
  channels[i] = {0.0075, 4, {{11,17.825e-6},{11,1.726e-6},{22,12.600e-6},{22,9.396e-6}}}; i++;

  // Ch 5 (0.40%): T1 K-CE + K-Auger(full) + T2 γ → 32.2 keV local + γ(9.4 keV) separate
  // K-CE=17.825 keV, KLL=10.484 keV, residual=3.842 keV, γ(T2)=9.396 keV
  channels[i] = {0.0040, 4, {{11,17.825e-6},{11,10.484e-6},{11,3.842e-6},{22,9.396e-6}}}; i++;

  double sum = 0.;
  for (int j = 0; j < kNChannels; j++) sum += channels[j].fraction;
  double cum = 0.;
  for (int j = 0; j < kNChannels; j++) {
    cum += channels[j].fraction / sum;
    cumulative[j] = cum;
  }
}

const KrDecayTable::Channel& KrDecayTable::sample() const
{
  double r = gRandom->Uniform();
  for (int i = 0; i < kNChannels; i++)
    if (r <= cumulative[i]) return channels[i];
  return channels[kNChannels-1];
}

// ── GeneratorKrDecay ─────────────────────────────────────────────────────

namespace o2 { namespace eventgen {

GeneratorKrDecay::GeneratorKrDecay()
  : Generator("KrDecay", "83mKr TPC calibration source"), mTable(nullptr) {}

GeneratorKrDecay::~GeneratorKrDecay() { delete mTable; }

Bool_t GeneratorKrDecay::Init()
{
  if (const char* env = std::getenv("KR_N_PER_EVENT")) {
    int n = std::atoi(env);
    if (n > 0) KrGenConfig::nPerEvent = n;
  }
  std::cout << "[GeneratorKrDecay] Init:"
            << " rInner="    << KrGenConfig::rInner
            << " rOuter="    << KrGenConfig::rOuter
            << " halfZ="     << KrGenConfig::halfZ
            << " nPerEvent=" << KrGenConfig::nPerEvent << "\n";

  mTable = new KrDecayTable();
  setPositionUnit(1.0); // coords in cm
  return Generator::Init();
}

Bool_t GeneratorKrDecay::generateEvent()
{
  mVertices.clear();
  // 1 cm safety margin inside field cage boundaries — avoids placing
  // electrons exactly on sector boundaries which can cause hit coordinate
  // transformation crashes in the merger when ROOT fills the TTree.
  const double rInner = KrGenConfig::rInner + 1.0;
  const double rOuter = KrGenConfig::rOuter - 1.0;
  const double halfZ  = KrGenConfig::halfZ  - 1.0;
  const double r2Min = rInner * rInner;
  const double r2Max = rOuter * rOuter;
  for (int i = 0; i < KrGenConfig::nPerEvent; ++i) {
    double r   = std::sqrt(gRandom->Uniform(r2Min, r2Max));
    double phi = gRandom->Uniform(0., TMath::TwoPi());
    double z   = gRandom->Uniform(-halfZ, halfZ);
    mVertices.push_back({{ r*std::cos(phi), r*std::sin(phi), z }});
  }
  return kTRUE;
}

Bool_t GeneratorKrDecay::importParticles()
{
  mParticles.clear();
  // Reserve before any push_back to prevent std::vector reallocation.
  // TParticle inherits from TObject (ROOT memory pool) and is not safe
  // to move-construct via std::vector reallocation on macOS arm64 —
  // ROOT's TStorage bookkeeping gets corrupted, causing malloc failures
  // ~50-100 events later. Reserving eliminates all reallocations.
  mParticles.reserve(KrGenConfig::nPerEvent * KrDecayTable::kNChannels);

  const int status = krO2EncodedStatus(1, 0);

  for (size_t iv = 0; iv < mVertices.size(); ++iv) {
    double vx = mVertices[iv][0];
    double vy = mVertices[iv][1];
    double vz = mVertices[iv][2];

    const KrDecayTable::Channel& ch = mTable->sample();
    for (int ip = 0; ip < ch.nProducts; ++ip) {
      int    pdg  = ch.products[ip].pdg;
      double eKin = ch.products[ip].eKin;
      if (eKin < 0.1e-6) continue;

      double mass = (pdg == 11) ? 0.000511 : 0.0;
      double E    = eKin + mass;
      double pmag = std::sqrt(std::max(0., E*E - mass*mass));
      double cosT = gRandom->Uniform(-1., 1.);
      double sinT = std::sqrt(1. - cosT*cosT);
      double phi  = gRandom->Uniform(0., TMath::TwoPi());

      TParticle part(pdg, status, -1, -1, -1, -1,
                     pmag*sinT*std::cos(phi),
                     pmag*sinT*std::sin(phi),
                     pmag*cosT,
                     E, vx, vy, vz, 0.);
      mParticles.push_back(part);
      // kToBeDone=BIT(16), kPrimary=BIT(17)
      // Must be set AFTER push_back — copy constructor resets fBits
      mParticles.back().SetBit(BIT(16));
      mParticles.back().SetBit(BIT(17));
    }
  }
  return kTRUE;
}

}} // namespace o2::eventgen

// Factory function - extern "C" so dlsym can find it
// Named GeneratorKrDecayCreate to distinguish from the CLING-callable wrapper
extern "C" FairGenerator* GeneratorKrDecayCreate()
{
  return new o2::eventgen::GeneratorKrDecay();
}