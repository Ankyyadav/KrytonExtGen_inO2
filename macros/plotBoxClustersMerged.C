// plotBoxClustersMerged.C
//
// Fills per-sector per-ROC charge spectra and cluster-size plots for all 36
// sectors and 4 ROC types, then draws a single merged canvas for the requested
// selection.
//
// Usage (ACLiC recommended):
//   root -b -q 'plotBoxClustersMerged.C+("BoxClusters.root","IROC")'
//   root -b -q 'plotBoxClustersMerged.C+("BoxClusters.root","IROC",5)'    // sector 5
//   root -b -q 'plotBoxClustersMerged.C+("BoxClusters.root","OROC1")'
//   root -b -q 'plotBoxClustersMerged.C+("BoxClusters.root","ALL",-1,false)' // no QC
//
// rocSel:    "IROC" | "OROC1" | "OROC2" | "OROC3" | "OROC" | "ALL"
// sectorSel: -1 = all sectors merged on canvas (default); 0-35 = specific sector
// qualityCuts: true (default) applies AliceOldData.C sigma cuts
//
// Output ROOT file (always written):
//   kr_spectra.root  with directory structure:
//     sector_XX / IROC | OROC1 | OROC2 | OROC3  — per-sector per-ROC histograms
//       hCharge_SecXX_ROC  — TH1F charge spectrum
//       h2DSize_SecXX_ROC  — TH2F cluster size vs charge
//     Merged / IROC | OROC1 | OROC2 | OROC3 | ALL  — summed over all 36 sectors
//       hCharge_Merged_ROC  — TH1F merged charge spectrum
//       h2DSize_Merged_ROC  — TH2F merged cluster size vs charge
//
// Output PDF (canvas):
//   merged charge spectrum for the requested (rocSel, sectorSel) + Kr fit
//
// ROC row boundaries (O2 TPC, local pad row within sector):
//   IROC  rows  0 -  62  (63 rows)
//   OROC1 rows 63 -  96  (34 rows)
//   OROC2 rows 97 - 126  (30 rows)
//   OROC3 rows 127- 151  (25 rows)

#include "DataFormatsTPC/KrCluster.h"
#include "TFile.h"
#include "TTree.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TDirectory.h"
#include "TCanvas.h"
#include "TF1.h"
#include "TFitResult.h"
#include "TLine.h"
#include "TLatex.h"
#include "TSystem.h"
#include "TMath.h"
#include <vector>
#include <string>
#include <iostream>
#include <ctime>
#include <cstdio>

// ── ROC row boundaries ────────────────────────────────────────────────────────
static const int kIROC_LAST  =  62;
static const int kOROC1_LAST =  96;
static const int kOROC2_LAST = 126;
static const int kOROC3_LAST = 151;

static int getRocIndex(float meanRow)
{
  int r = TMath::Nint(meanRow);
  if (r <= kIROC_LAST)  return 0;
  if (r <= kOROC1_LAST) return 1;
  if (r <= kOROC2_LAST) return 2;
  if (r <= kOROC3_LAST) return 3;
  return -1;
}

static bool inRocSel(int rocIdx, const std::string& roc)
{
  if (roc == "IROC")  return rocIdx == 0;
  if (roc == "OROC1") return rocIdx == 1;
  if (roc == "OROC2") return rocIdx == 2;
  if (roc == "OROC3") return rocIdx == 3;
  if (roc == "OROC")  return rocIdx >= 1 && rocIdx <= 3;
  return true; // "ALL"
}

// ── fitKrSpectrum: staged multi-Gaussian fit + exponential+erfc background ───
// Parameters [0..19]:
//   [0],[1]       expo background
//   [2..4]        erfc shoulder
//   [5..7]        9.4 keV Gaussian
//   [8..10]       12.6 keV Gaussian
//   [11..13]      29.1 keV Gaussian
//   [14..16]      32.2 keV Gaussian
//   [17..19]      41.6 keV Gaussian
static void fitKrSpectrum(TH1F* h, double mu41, const std::string& tag = "")
{
  const double ratio[5] = { 9.4/41.6, 12.6/41.6, 29.1/41.6, 32.2/41.6, 1.0 };
  const char*  lbl[5]   = { "T2#gamma 9.4 keV", "K#alpha 12.6 keV",
                             "29.1 keV", "32.2 keV", "41.6 keV (main)" };
  const int    col[5]   = { kOrange+2, kGreen+2, kMagenta, kCyan+2, kRed };

  double mu[5], sig0[5];
  for (int i = 0; i < 5; i++) { mu[i] = mu41*ratio[i]; sig0[i] = mu[i]*0.06; }

  const double xlo = mu41*0.11, xhi = mu41*1.20;

  TF1* fbg = new TF1("fbg_pre","expo", mu41*0.07, mu41*0.17);
  h->Fit(fbg,"RQN0");

  const double rlo[5] = { mu41*0.175,mu41*0.265,mu41*0.625,mu41*0.720,mu41*0.865 };
  const double rhi[5] = { mu41*0.280,mu41*0.365,mu41*0.760,mu41*0.835,mu41*1.180 };
  TF1* fg[5];
  for (int i = 0; i < 5; i++) {
    fg[i] = new TF1(Form("fg_pre%d",i),"gaus",rlo[i],rhi[i]);
    fg[i]->SetParameters(TMath::Max(h->GetBinContent(h->FindBin(mu[i])),1.),mu[i],sig0[i]);
    fg[i]->SetParLimits(0, 0., 1e9);                    // amplitude non-negative
    fg[i]->SetParLimits(1, mu[i]*0.90, mu[i]*1.10);     // mu: ±10%
    fg[i]->SetParLimits(2, 5., mu[i]*0.35);             // sigma: 5 ADC floor, generous upper
    h->Fit(fg[i],"RQN0");
  }

  TF1* total = new TF1("fitTotal",
    "exp([0]+[1]*x)"
    "+[2]*erfc((x-[3])/(sqrt(2.0)*[4]))"
    "+[5]*exp(-0.5*pow((x-[6])/[7],2))"
    "+[8]*exp(-0.5*pow((x-[9])/[10],2))"
    "+[11]*exp(-0.5*pow((x-[12])/[13],2))"
    "+[14]*exp(-0.5*pow((x-[15])/[16],2))"
    "+[17]*exp(-0.5*pow((x-[18])/[19],2))",
    xlo, xhi);
  total->SetNpx(3000);
  total->SetParameter(0, fbg->GetParameter(0));
  total->SetParameter(1, fbg->GetParameter(1));
  double erfc_seed = h->GetBinContent(h->FindBin(mu41*0.905))*0.4;
  total->SetParameter(2, erfc_seed > 1. ? erfc_seed : 1.);
  total->SetParameter(3, mu41*0.904);
  total->SetParameter(4, mu41*0.103);
  for (int i = 0; i < 5; i++) {
    total->SetParameter(5+3*i, TMath::Max(fg[i]->GetParameter(0),1.));
    total->SetParameter(6+3*i, fg[i]->GetParameter(1));
    total->SetParameter(7+3*i, TMath::Abs(fg[i]->GetParameter(2)));
  }
  total->SetParLimits(1,-0.02,0.);
  total->SetParLimits(2,0.,1e9);
  total->SetParLimits(3,mu41*0.55,mu41*1.10);
  total->SetParLimits(4,mu41*0.022,mu41*0.30);
  for (int i = 0; i < 5; i++) {
    total->SetParLimits(5+3*i, 0., 1e9);                // amplitude non-negative
    total->SetParLimits(6+3*i, mu[i]*0.90, mu[i]*1.10); // mu: ±10%
    total->SetParLimits(7+3*i, 5., mu[i]*0.35);         // sigma: 5 ADC floor, generous upper
  }
  total->SetLineColor(kBlack); total->SetLineWidth(2);
  TFitResultPtr r = h->Fit(total,"RS");

  TF1* fbg_draw = new TF1("fbg_draw",
    "exp([0]+[1]*x)+[2]*erfc((x-[3])/(sqrt(2.0)*[4]))", xlo, xhi);
  fbg_draw->SetParameters(total->GetParameter(0),total->GetParameter(1),
    total->GetParameter(2),total->GetParameter(3),total->GetParameter(4));
  fbg_draw->SetNpx(3000);
  fbg_draw->SetLineColor(kGray+1); fbg_draw->SetLineStyle(7); fbg_draw->SetLineWidth(2);
  fbg_draw->Draw("SAME");

  for (int i = 0; i < 5; i++) {
    TF1* gc = new TF1(Form("gc%d",i),"[0]*exp(-0.5*pow((x-[1])/[2],2))",xlo,xhi);
    gc->SetParameters(total->GetParameter(5+3*i),total->GetParameter(6+3*i),
                      total->GetParameter(7+3*i));
    gc->SetNpx(3000);
    gc->SetLineColor(col[i]); gc->SetLineStyle(2); gc->SetLineWidth(2);
    gc->Draw("SAME");
    double px = total->GetParameter(6+3*i), py = total->GetParameter(5+3*i);
    if (py > h->GetMaximum()*0.015) {
      TLatex* tx = new TLatex(px+mu41*0.008, py*0.65, lbl[i]);
      tx->SetTextSize(0.028); tx->SetTextColor(col[i]); tx->Draw();
    }
  }

  double chi2ndf = (r->Ndf()>0) ? r->Chi2()/r->Ndf() : -1.;
  printf("\n==== Kr-83m spectrum fit [%s] ====\n", h->GetTitle());
  printf("  41.6 keV seed : %.1f ADC\n", mu41);
  printf("  chi2/ndf      : %.2f\n", chi2ndf);
  printf("  %-22s  %14s  %12s  %8s\n","Peak","mu_fit [ADC]","sigma [ADC]","reso [%]");
  printf("  %-22s  %14s  %12s  %8s\n","----","------------","-----------","--------");
  for (int i = 0; i < 5; i++) {
    double mu_i    = total->GetParameter(6+3*i);
    double sigma_i = TMath::Abs(total->GetParameter(7+3*i));
    double reso    = (mu_i > 0.) ? 100.*sigma_i/mu_i : -1.;
    printf("  %-22s  %7.1f +/- %4.1f  %5.1f +/- %3.1f  %7.2f%%\n",
      lbl[i], mu_i, total->GetParError(6+3*i),
      sigma_i, total->GetParError(7+3*i), reso);
  }
  printf("==============================================\n\n");

  if (!tag.empty()) {
    const char* logFile = "kr_reso_log.txt";
    bool writeHeader = false;
    if (FILE* chk = fopen(logFile, "r")) { fclose(chk); } else { writeHeader = true; }
    FILE* logf = fopen(logFile, "a");
    if (logf) {
      if (writeHeader)
        fprintf(logf, "%-10s  %-42s  %-16s  %8s  %6s  %8s  %6s  %8s\n",
          "date", "tag", "peak", "mu_ADC", "mu_err", "sigma_ADC", "sig_err", "reso_pct");
      time_t now = time(nullptr);
      char datebuf[12];
      strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", localtime(&now));
      const char* peakKey[5] = {"9.4keV","12.6keV","29.1keV","32.2keV","41.6keV_main"};
      for (int i = 0; i < 5; i++) {
        double mu_i    = total->GetParameter(6+3*i);
        double sigma_i = TMath::Abs(total->GetParameter(7+3*i));
        double reso    = (mu_i > 0.) ? 100.*sigma_i/mu_i : -1.;
        fprintf(logf, "%-10s  %-42s  %-16s  %8.1f  %6.1f  %8.1f  %6.1f  %8.3f\n",
          datebuf, tag.c_str(), peakKey[i],
          mu_i, total->GetParError(6+3*i),
          sigma_i, total->GetParError(7+3*i), reso);
      }
      fclose(logf);
      printf("  → appended to %s\n\n", logFile);
    }
  }
}

// ── Main ─────────────────────────────────────────────────────────────────────
void plotBoxClustersMerged(
    const char* inputFile   = "BoxClusters.root",
    const char* rocSel      = "ALL",
    int         sectorSel   = -1,
    bool        qualityCuts = true,
    const char* outputPdf   = "")
{
  gSystem->Load("libO2TPCReconstruction");
  gSystem->Load("libO2DataFormatsTPC");

  // ── Validate inputs ───────────────────────────────────────────────────────
  std::string roc(rocSel);
  for (auto& c : roc) c = toupper(c);
  if (roc != "IROC" && roc != "OROC1" && roc != "OROC2" &&
      roc != "OROC3" && roc != "OROC"  && roc != "ALL") {
    std::cout << "Invalid rocSel '" << rocSel
              << "'. Choose: IROC OROC1 OROC2 OROC3 OROC ALL" << std::endl;
    return;
  }
  if (sectorSel < -1 || sectorSel > 35) {
    std::cout << "Invalid sectorSel " << sectorSel
              << ". Use -1 (all) or 0-35." << std::endl;
    return;
  }

  // ── Histogram setup ───────────────────────────────────────────────────────
  static const char* kRocNames[4] = { "IROC", "OROC1", "OROC2", "OROC3" };
  const int    nBins = 400;
  const double xMax  = 6000.;
  const double binW  = xMax / nBins; // 15 ADC

  TH1F* hCharge[36][4];
  TH2F* h2DSize[36][4];

  for (int s = 0; s < 36; s++) {
    for (int r = 0; r < 4; r++) {
      const char* rn = kRocNames[r];
      hCharge[s][r] = new TH1F(
        Form("hCharge_Sec%02d_%s", s, rn),
        Form("Sector %d -- %s;Total cluster charge (ADC counts);Number of entries per %.0f ADC",
             s, rn, binW),
        nBins, 0., xMax);
      hCharge[s][r]->SetDirectory(nullptr);

      h2DSize[s][r] = new TH2F(
        Form("h2DSize_Sec%02d_%s", s, rn),
        Form("Sector %d -- %s;Total cluster charge (ADC counts);Cluster size (digits)",
             s, rn),
        200, 0., xMax, 120, 0., 120.);
      h2DSize[s][r]->SetDirectory(nullptr);
    }
  }

  // Merged histogram for the canvas
  std::string mergeTitle = (sectorSel >= 0)
    ? Form("Sector %d -- %s", sectorSel, roc.c_str())
    : Form("All sectors -- %s", roc.c_str());

  TH1F* hMerged = new TH1F("hMerged",
    Form("%s;Total cluster charge (ADC counts);Number of entries per %.0f ADC",
         mergeTitle.c_str(), binW),
    nBins, 0., xMax);
  hMerged->SetDirectory(nullptr);

  // ── Open file and detect branch format ────────────────────────────────────
  auto f = TFile::Open(inputFile);
  if (!f || f->IsZombie()) {
    std::cout << "Cannot open " << inputFile << std::endl; return;
  }
  auto t = (TTree*)f->Get("Clusters");
  if (!t) { std::cout << "No 'Clusters' tree in " << inputFile << std::endl; return; }

  enum Format { FORMAT_MIXED, FORMAT_PERSEC, FORMAT_DPL };
  Format fmt;
  if      (t->GetBranch("TPCBoxCluster_0")) fmt = FORMAT_DPL;
  else if (t->GetBranch("cls_0"))           fmt = FORMAT_PERSEC;
  else if (t->GetBranch("cls"))             fmt = FORMAT_MIXED;
  else {
    std::cout << "Unrecognised branch layout in " << inputFile << std::endl; return;
  }

  const char* fmtName[] = { "mixed-cls (KrCluster::sector)", "per-sector cls_N", "DPL TPCBoxCluster_N" };
  std::cout << "Format   : " << fmtName[fmt] << std::endl;
  std::cout << "ROC sel  : " << roc << " (canvas)" << std::endl;
  std::cout << "Sector   : " << (sectorSel<0 ? "all" : Form("%d",sectorSel)) << " (canvas)" << std::endl;
  std::cout << "QC cuts  : " << (qualityCuts ? "ON (AliceOldData)" : "OFF") << std::endl;

  // ── Quality cut lambda ────────────────────────────────────────────────────
  auto passQC = [&](const o2::tpc::KrCluster& c) -> bool {
    if (!qualityCuts) return true;
    return (c.sigmaTime > 0.1f && c.sigmaTime < 1.8f) &&
           (c.sigmaRow  > 0.2f && c.sigmaRow  < 0.6f + c.totCharge/4000.f) &&
           (c.sigmaPad  > 0.1f && c.sigmaPad  < 1.2f);
  };

  // ── Fill lambda — fills per-sector/ROC histos and hMerged ─────────────────
  long long nTotal = 0, nMerged = 0;

  auto fillCluster = [&](const o2::tpc::KrCluster& c, int sec) {
    ++nTotal;
    if (!passQC(c)) return;
    int rocIdx = getRocIndex(c.meanRow);
    if (sec < 0 || sec > 35 || rocIdx < 0) return;
    hCharge[sec][rocIdx]->Fill(c.totCharge);
    h2DSize[sec][rocIdx]->Fill(c.totCharge, (double)c.size);
    if (inRocSel(rocIdx, roc) && (sectorSel < 0 || sec == sectorSel)) {
      hMerged->Fill(c.totCharge);
      ++nMerged;
    }
  };

  // ── Loop over entries ─────────────────────────────────────────────────────
  if (fmt == FORMAT_MIXED) {
    std::vector<o2::tpc::KrCluster>* cls = nullptr;
    t->SetBranchAddress("cls", &cls);
    for (Long64_t ev = 0; ev < t->GetEntries(); ++ev) {
      t->GetEntry(ev);
      if (!cls) continue;
      for (auto& c : *cls) fillCluster(c, (int)c.sector);
    }

  } else if (fmt == FORMAT_PERSEC) {
    std::vector<o2::tpc::KrCluster>* secCls[36] = {};
    for (int s = 0; s < 36; s++)
      t->SetBranchAddress(Form("cls_%d", s), &secCls[s]);
    for (Long64_t ev = 0; ev < t->GetEntries(); ++ev) {
      t->GetEntry(ev);
      for (int s = 0; s < 36; s++) {
        if (!secCls[s]) continue;
        for (auto& c : *secCls[s]) fillCluster(c, s);
      }
    }

  } else {
    std::vector<o2::tpc::KrCluster>* secCls[36] = {};
    for (int s = 0; s < 36; s++)
      t->SetBranchAddress(Form("TPCBoxCluster_%d", s), &secCls[s]);
    for (Long64_t ev = 0; ev < t->GetEntries(); ++ev) {
      t->GetEntry(ev);
      for (int s = 0; s < 36; s++) {
        if (!secCls[s]) continue;
        for (auto& c : *secCls[s]) fillCluster(c, s);
      }
    }
  }

  printf("Total clusters read : %lld\n", nTotal);
  printf("In canvas selection : %lld (%.1f%%)\n",
    nMerged, nTotal > 0 ? 100.*nMerged/nTotal : 0.);

  // ── Build Merged/ histograms (sum over all sectors) ──────────────────────
  TH1F* hMgCharge[4];
  TH2F* hMgSize[4];
  for (int r = 0; r < 4; r++) {
    hMgCharge[r] = new TH1F(
      Form("hCharge_Merged_%s", kRocNames[r]),
      Form("All sectors -- %s;Total cluster charge (ADC counts);Number of entries per %.0f ADC",
           kRocNames[r], binW),
      nBins, 0., xMax);
    hMgCharge[r]->SetDirectory(nullptr);
    hMgSize[r] = new TH2F(
      Form("h2DSize_Merged_%s", kRocNames[r]),
      Form("All sectors -- %s;Total cluster charge (ADC counts);Cluster size (digits)",
           kRocNames[r]),
      200, 0., xMax, 120, 0., 120.);
    hMgSize[r]->SetDirectory(nullptr);
    for (int s = 0; s < 36; s++) {
      hMgCharge[r]->Add(hCharge[s][r]);
      hMgSize[r]->Add(h2DSize[s][r]);
    }
  }
  TH1F* hMgChargeAll = new TH1F("hCharge_Merged_ALL",
    Form("All sectors -- ALL ROCs;Total cluster charge (ADC counts);Number of entries per %.0f ADC", binW),
    nBins, 0., xMax);
  hMgChargeAll->SetDirectory(nullptr);
  TH2F* hMgSizeAll = new TH2F("h2DSize_Merged_ALL",
    "All sectors -- ALL ROCs;Total cluster charge (ADC counts);Cluster size (digits)",
    200, 0., xMax, 120, 0., 120.);
  hMgSizeAll->SetDirectory(nullptr);
  for (int r = 0; r < 4; r++) {
    hMgChargeAll->Add(hMgCharge[r]);
    hMgSizeAll->Add(hMgSize[r]);
  }

  // ── Save all per-sector/ROC histograms to ROOT file ───────────────────────
  auto out = TFile::Open("kr_spectra.root", "RECREATE");
  for (int s = 0; s < 36; s++) {
    auto* secDir = out->mkdir(Form("sector_%02d", s));
    for (int r = 0; r < 4; r++) {
      auto* rocDir = secDir->mkdir(kRocNames[r]);
      rocDir->cd();
      hCharge[s][r]->Write();
      h2DSize[s][r]->Write();
    }
  }
  auto* mrgDir = out->mkdir("Merged");
  for (int r = 0; r < 4; r++) {
    auto* mrgRocDir = mrgDir->mkdir(kRocNames[r]);
    mrgRocDir->cd();
    hMgCharge[r]->Write();
    hMgSize[r]->Write();
  }
  auto* mrgAllDir = mrgDir->mkdir("ALL");
  mrgAllDir->cd();
  hMgChargeAll->Write();
  hMgSizeAll->Write();
  out->cd();
  hMerged->Write("hMerged");
  out->Close();
  printf("Saved kr_spectra.root\n");

  // ── Canvas: merged spectrum for requested selection ───────────────────────
  if (nMerged == 0) {
    std::cout << "No clusters in canvas selection — PDF not written." << std::endl;
    return;
  }

  // Find 41.6 keV peak (histogram maximum above 1000 ADC)
  double mu41 = -1.;
  {
    int mBin = hMerged->FindBin(1000.);
    for (int b = mBin+1; b <= hMerged->GetNbinsX(); b++)
      if (hMerged->GetBinContent(b) > hMerged->GetBinContent(mBin)) mBin = b;
    mu41 = hMerged->GetBinCenter(mBin);
  }
  const bool goodSpectrum = (mu41 >= 1200.);
  printf("41.6 keV seed peak  : %.0f ADC%s\n", mu41,
    goodSpectrum ? " [OK]" : " [WRONG SPECTRUM]");

  // Extract tag from inputFile: BoxClusters_TAG.root → _TAG, else empty
  std::string tag;
  {
    std::string inStr(inputFile);
    auto stem = inStr.substr(inStr.rfind('/')+1);
    const std::string prefix = "BoxClusters";
    if (stem.size() > prefix.size() + 5 && stem.substr(0, prefix.size()) == prefix) {
      tag = stem.substr(prefix.size());
      if (tag.size() > 5) tag = tag.substr(0, tag.size()-5); // strip ".root"
    }
  }

  // Determine PDF name
  std::string pdfName;
  if (std::string(outputPdf).empty()) {
    std::string secStr = (sectorSel<0) ? "allsec" : Form("s%02d", sectorSel);
    pdfName = Form("kr_%s_%s%s.pdf", roc.c_str(), secStr.c_str(), tag.c_str());
    for (auto& c : pdfName) c = tolower(c);
  } else {
    pdfName = outputPdf;
  }

  TCanvas* cv = new TCanvas("cv", mergeTitle.c_str(), 1100, 700);
  cv->SetLeftMargin(0.10);
  cv->SetRightMargin(0.05);
  cv->SetBottomMargin(0.12);

  hMerged->SetLineColor(kBlue+1);
  hMerged->SetLineWidth(2);
  hMerged->Draw("HIST");

  if (goodSpectrum) {
    fitKrSpectrum(hMerged, mu41, tag);
  } else {
    const double ratio[5] = { 9.4/41.6,12.6/41.6,29.1/41.6,32.2/41.6,1.0 };
    const int    pcol[5]  = { kOrange+2,kGreen+2,kMagenta,kCyan+2,kRed };
    for (int i = 0; i < 5; i++) {
      double xexp = mu41 * ratio[i];
      if (xexp < 50 || xexp > 5900) continue;
      TLine* l = new TLine(xexp, 0, xexp, hMerged->GetMaximum()*0.8);
      l->SetLineColor(pcol[i]); l->SetLineStyle(3); l->SetLineWidth(1); l->Draw();
    }
    TLatex* msg = new TLatex(0.15, 0.85,
      Form("WRONG SPECTRUM: max at %.0f ADC (expected > 1200)", mu41));
    msg->SetNDC(); msg->SetTextColor(kRed); msg->SetTextSize(0.038); msg->Draw();
  }

  TLatex* tlab = new TLatex(0.12, 0.92, Form("#bf{%s}", mergeTitle.c_str()));
  tlab->SetNDC(); tlab->SetTextSize(0.038); tlab->Draw();
  TLatex* nlab = new TLatex(0.88, 0.92, Form("N_{sel}=%lld", nMerged));
  nlab->SetNDC(); nlab->SetTextSize(0.033); nlab->SetTextAlign(31); nlab->Draw();

  cv->SaveAs(pdfName.c_str());
  printf("Saved %s\n", pdfName.c_str());
}
