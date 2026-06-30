// GeneratorKrDecay.h
// Header for compiled 83mKr TPC calibration generator.
// Compile with: build_kr_generator.sh

#pragma once

#include "Generators/Generator.h"
#include <array>
#include <vector>

// O2 status encoding from MCGenProperties.h
// bits 0-8: hepmc(9), bits 9-18: gen(10), bits 19-28: reserved(10), bits 29-31: sentinel=5
inline int krO2EncodedStatus(int hepmc, int gen = 0)
{
  return (5 << 29) | ((gen & 0x3FF) << 9) | (hepmc & 0x1FF);
}

namespace KrGenConfig {
  inline double rInner    = 83.5;
  inline double rOuter    = 246.5;  // TPC outermost pad row outer edge ~247 cm; stay inside
  inline double halfZ     = 249.7;
  inline int    nPerEvent = 1;
}

struct KrProduct { int pdg; double eKin; };

class KrDecayTable {
public:
  struct Channel {
    double    fraction;
    int       nProducts;
    KrProduct products[6];  // max 5 used; 6 for safety
  };
  // Six physically motivated channels (T1 mode × T2 mode):
  //   T1: 77% L/M/N-CE, 23% K-CE (of which 65.2% K-fluorescence, 34.8% K-Auger)
  //   T2: 95% L-CE, 5% gamma
  static const int kNChannels = 6;
  Channel  channels[kNChannels];
  double   cumulative[kNChannels];
  KrDecayTable();
  const Channel& sample() const;
};

namespace o2 { namespace eventgen {

class GeneratorKrDecay : public Generator
{
 public:
  GeneratorKrDecay();
  ~GeneratorKrDecay() override;
  Bool_t Init() override;
  Bool_t generateEvent() override;
  Bool_t importParticles() override;

 private:
  KrDecayTable*                     mTable;
  std::vector<std::array<double,3>> mVertices;
  // No ClassDefOverride — avoids needing a ROOT dictionary (rootcint).
  // The base class Generator already has ClassDef; omitting it here is
  // safe for a compiled external generator plugin.
};

}} // namespace o2::eventgen

// The compiled library exports GeneratorKrDecayCreate (extern "C") as its
// factory entry point. The loader resolves it via DynFindSymbol/dlsym at
// runtime — no header declaration needed here, and having one would
// conflict with the o2::eventgen::GeneratorKrDecay constructor.
