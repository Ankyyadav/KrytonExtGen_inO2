#include "TCanvas.h"
#include "TFile.h"
#include "TChain.h"
#include "TGrid.h"

#include "DataFormatsTPC/KrCluster.h"
#include "TPCReconstruction/KrBoxClusterFinder.h"
#include "DataFormatsTPC/Digit.h"
#include "TPCBase/Utils.h"

#include <array>
#include <iostream>
#include <tuple>
#include <vector>
#include "TSystem.h"
//#endif

void findKrBoxCluster(std::string inputFile = "tpcdigits.root", std::string outputFile = "BoxClusters.root", std::string_view gainMapFile = "../GainMap_2021-11-17_krypton_0.5T.root", long long maxEvents = -1, int lastTimeBin = 70000)
{
  ULong_t runRead = -1;
  uint32_t run = 0;
  uint32_t firstOrbit = 0;
  uint32_t tfCounter = 0;

  std::string files_cmd = "ls " + inputFile;
  TString sfiles = gSystem->GetFromPipe(TString::Format("%s", files_cmd.c_str()));
  std::unique_ptr<TObjArray> arr(sfiles.Tokenize("\n"));
  auto tree = new TChain("o2sim","o2sim");
  for (const auto ofile : *arr) 
        tree->AddFile(ofile->GetName());
  
  long long nEntries = tree->GetEntries();
  std::cout << "The Tree has " << nEntries << " Entries." << std::endl;
  if (maxEvents > 0 && maxEvents < nEntries) {
    nEntries = maxEvents;
    std::cout << "Processing only first " << nEntries << " entries." << std::endl;
  }

  // Initialize File for later writing
  TFile* fOut = new TFile(outputFile.c_str(), "RECREATE");
  TTree* tClusters = new TTree("Clusters", "Clusters");

  // Create KrBoxClusterFinder object, memory is only allocated once
  auto clFinder = std::make_unique<o2::tpc::KrBoxClusterFinder>();
  auto& clusters = clFinder->getClusters();
  // Create a Branch for each sector:
  tClusters->Branch("cls", &clusters);
  tClusters->Branch("run", &run);
  tClusters->Branch("firstOrbit", &firstOrbit);
  tClusters->Branch("tfCounter", &tfCounter);

  std::array<std::vector<o2::tpc::Digit>*, 36> digitizedSignal;
  for (size_t iSec = 0; iSec < digitizedSignal.size(); ++iSec) 
  {
    digitizedSignal[iSec] = nullptr;
    tree->SetBranchAddress(Form("TPCDigit_%zu", iSec), &digitizedSignal[iSec]);
  }
  if (tree->GetBranch("run"))        tree->SetBranchAddress("run", &runRead);
  if (tree->GetBranch("firstOrbit")) tree->SetBranchAddress("firstOrbit", &firstOrbit);
  if (tree->GetBranch("tfCounter"))  tree->SetBranchAddress("tfCounter", &tfCounter);

  if (gainMapFile.size())
    clFinder->loadGainMapFromFile(gainMapFile);

  // clFinder->setMinNumberOfNeighbours(0);
  // clFinder->setMinQTreshold(0);
  clFinder->setMaxTimes(lastTimeBin);

  // Now everything can get processed
  // Loop over all events
  int  entryDisp = (nEntries >= 100) ? (int)(nEntries/100) : 1;
  for (int iEvent = 0; iEvent < nEntries; ++iEvent)
  {
    if ((iEvent+1)%(entryDisp) == 0)
        std::cout << iEvent + 1 << "/" << nEntries << std::endl;
    tree->GetEntry(iEvent);
    run = uint32_t(runRead);

    for (int i = 0; i < 36; i++) 
    {
      auto sector = digitizedSignal[i];
      if (sector->size() == 0) 
        continue;
        
      auto& digits = *sector;
      std::sort(digits.begin(), digits.end(), [](const auto& a, const auto& b)
      {
        if (a.getTimeStamp() < b.getTimeStamp())
          return true;

        if (a.getTimeStamp() == b.getTimeStamp())
        {
          if (a.getRow() < b.getRow())
            return true;
          else if (a.getRow() == b.getRow())
            return a.getPad() < b.getPad();
        }
        return false;
      });

      // The rolling window breaks as soon as all digits are loaded, leaving the
      // center mMaxClusterSizeTime+1 bins behind the last digit.  For compact
      // clusters (typical span 3-5 bins) the peak is never reached as the center,
      // so ~93% of events are missed.  A sentinel digit at T_last+4 (ADC=0) delays
      // the break by exactly mMaxClusterSizeTime+1 iterations so the center sweeps
      // through every real time bin including the peak.
      const int lastT = digits.back().getTimeStamp();
      digits.emplace_back(digits.back().getCRU(), 0.f, digits.back().getRow(),
                          digits.back().getPad(), lastT + 4);
      clFinder->loopOverSector(*sector, i);
      digits.pop_back(); // remove sentinel so the TTree branch data is restored
    }
    // Fill Tree
    tClusters->Fill();
    clusters.clear();
  }
  // Write Tree to file
  fOut->Write();
  fOut->Close();
  return;
}

/*
int main()
{
  findKrBoxCluster();
  return 0;
}
*/
/*

auto c = o2::tpc::utils::buildChain("cat /tmp/run510439", "o2sim", "o2sim");
    const auto& Clusterer = KrBoxClusterFinder::instance();
    Clusterer.loadGainMapFromFile("GainMap_2021-11-17_krypton_0.5T.root");
    
    std::array<std::vector<o2::tpc::Digit> *, 36> digits;
    unsigned int firstOrbit;
    unsigned int tfCounter;
    for (int iSector = 0; iSector < 36; iSector++)
    {
        digits[iSector] = nullptr;
        c->SetBranchAddress(Form("TPCDigit_%d", iSector), &digits[iSector]);
    }
    c->SetBranchAddress("firstOrbit", &firstOrbit);
    c->SetBranchAddress("tfCounter", &tfCounter);

    int nEvents = c->GetEntries();
    
    for (Long64_t iEvent = 0; iEvent < nEvents; ++iEvent)
    {
        std::cout << iEvent + 1 << " / " << nEvents << std::endl;
        std::cout << "First Orbit: " << firstOrbit << std::endl;
        std::cout << "TF Counter : " << tfCounter << std::endl;

        c->GetEntry(iEvent);
        for (const std::vector<o2::tpc::Digit> *vecDigits : digits)
        {
            for (const auto &di : *vecDigits)
            {                           
                int meanRow = round(di.getRow());
                int meanPad = round(di.getPad());               
            }
        }
    } 
    
*/
