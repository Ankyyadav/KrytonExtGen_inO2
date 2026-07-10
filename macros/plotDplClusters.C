// plotDplClusters.C
//
// Reads tpcBoxClusters.root produced by o2-tpc-krypton-clusterer
// (run_kr_digi_clust.sh Step 2) and produces:
//   • 4-panel overview canvas (full spectrum, log scale, zoom, size vs charge)
//   • Merged spectrum with staged multi-Gaussian fit
//   • Per-sector per-ROC ROOT file with same directory structure as plotBoxClustersMerged
//   • Appended fit results in kr_reso_log.txt
//
// Usage (ACLiC recommended):
//   root -b -q 'plotDplClusters.C+("tpcBoxClusters.root")'
//   root -b -q 'plotDplClusters.C+("tpcBoxClusters.root","IROC")'
//   root -b -q 'plotDplClusters.C+("tpcBoxClusters.root","ALL",-1,true,"","nPE1000")'
//
// rocSel:      "IROC" | "OROC1" | "OROC2" | "OROC3" | "OROC" | "ALL"
// sectorSel:   -1 = all sectors merged (default); 0-35 = one sector
// qualityCuts: true (default) — AliceOldData-style sigma cuts
// outputPdf:   "" = auto-named kr_<roc>_<sec><tag>.pdf
// tag:         label for kr_reso_log.txt; "" = basename of run directory
//
// Output files (written in CWD, i.e. the run directory):
//   kr_dpl_spectra.root  — per-sector/ROC + Merged directory structure
//   kr_dpl_overview<tag>.pdf  — 4-panel overview canvas
//   kr_<roc>_<sec><tag>.pdf   — merged spectrum with fit
//   kr_reso_log.txt      — appended fit results (5 peaks × run)
//
// ROC row boundaries (O2 TPC, local pad row within sector):
//   IROC  rows   0 -  62  (63 rows)
//   OROC1 rows  63 -  96  (34 rows)
//   OROC2 rows  97 - 126  (30 rows)
//   OROC3 rows 127 - 151  (25 rows)

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
  return true; // ALL
}

// ── fitKrSpectrum: staged multi-Gaussian + exponential+erfc background ────────
// Parameters [0..19]:
//   [0],[1]     expo background (ln A, slope)
//   [2..4]      erfc shoulder (amp, centre, width)
//   [5..7]       9.4 keV Gaussian
//   [8..10]     12.6 keV Gaussian
//   [11..13]    29.1 keV Gaussian
//   [14..16]    32.2 keV Gaussian
//   [17..19]    41.6 keV Gaussian (main)
static void fitKrSpectrum(TH1F* h, double mu41, const std::string& tag = "")
{
  const double ratio[5] = { 9.4/41.6, 12.6/41.6, 29.1/41.6, 32.2/41.6, 1.0 };
  const char*  lbl[5]   = { "T2#gamma 9.4 keV", "K#alpha 12.6 keV",
                             "29.1 keV", "32.2 keV", "41.6 keV (main)" };
  const int    col[5]   = { kOrange+2, kGreen+2, kMagenta, kCyan+2, kRed };

  double mu[5], sig0[5];
  for (int i = 0; i < 5; i++) { mu[i] = mu41*ratio[i]; sig0[i] = mu[i]*0.08; }

  // Amplitude at the 41.6 keV seed bin (≈ histogram maximum).
  // Satellite peaks (9.4, 12.6, 29.1, 32.2 keV) are each ~1/8 of the main peak;
  // we allow [1/10, 1/5] of amp41.  The 41.6 keV peak itself gets [0.5, 2.0].
  double amp41 = TMath::Max(h->GetBinContent(h->FindBin(mu41)), 1.);
  const double alo[5] = { amp41/10., amp41/10., amp41/10., amp41/10., amp41*0.50 };
  const double ahi[5] = { amp41/ 5., amp41/ 5., amp41/ 5., amp41/ 5., amp41*2.00 };

  // Sigma limits: lower bound prevents single-bin spikes; upper bound scales
  // with expected 1/sqrt(E) resolution degradation toward lower energies.
  const double slo[5] = { mu[0]*0.05, mu[1]*0.05, mu[2]*0.04, mu[3]*0.04, mu[4]*0.03 };
  const double shi[5] = { mu[0]*0.30, mu[1]*0.28, mu[2]*0.25, mu[3]*0.25, mu[4]*0.20 };

  const double xlo = mu41*0.11, xhi = mu41*1.20;

  TF1* fbg = new TF1("fbg_pre","expo", mu41*0.07, mu41*0.17);
  h->Fit(fbg,"RQN0");

  const double rlo[5] = { mu41*0.175,mu41*0.265,mu41*0.625,mu41*0.720,mu41*0.865 };
  const double rhi[5] = { mu41*0.280,mu41*0.365,mu41*0.760,mu41*0.835,mu41*1.180 };
  TF1* fg[5];
  for (int i = 0; i < 5; i++) {
    fg[i] = new TF1(Form("fg_pre%d",i),"gaus",rlo[i],rhi[i]);
    fg[i]->SetParameters(TMath::Max(h->GetBinContent(h->FindBin(mu[i])),1.),mu[i],sig0[i]);
    fg[i]->SetParLimits(0, alo[i], ahi[i]);
    fg[i]->SetParLimits(1, mu[i]*0.90, mu[i]*1.10);
    fg[i]->SetParLimits(2, slo[i], shi[i]);
    h->Fit(fg[i],"RQN0");
  }

  // erfc centre and width are tied to the 41.6 keV peak (p[18], p[19])
  // rather than being free parameters.  p[3] and p[4] are fixed to 0 (unused).
  TF1* total = new TF1("fitTotal",
    [](double* x, double* p) -> double {
      double val = TMath::Exp(p[0] + p[1]*x[0]);
      val += p[2] * TMath::Erfc((x[0] - p[18]) / (TMath::Sqrt2() * p[19]));
      for (int i = 0; i < 5; i++)
        val += p[5+3*i] * TMath::Gaus(x[0], p[6+3*i], p[7+3*i], false);
      return val;
    }, xlo, xhi, 20);
  total->SetNpx(3000);
  total->SetParameter(0, fbg->GetParameter(0));
  total->SetParameter(1, fbg->GetParameter(1));
  double erfc_seed = h->GetBinContent(h->FindBin(mu41*0.905))*0.4;
  total->SetParameter(2, erfc_seed > 1. ? erfc_seed : 1.);
  total->FixParameter(3, 0.);  // unused: erfc centre tied to p[18]
  total->FixParameter(4, 0.);  // unused: erfc width  tied to p[19]
  for (int i = 0; i < 5; i++) {
    // Clamp pre-fit amplitude into [alo, ahi] before seeding the global fit
    double aSeed = TMath::Min(TMath::Max(fg[i]->GetParameter(0), alo[i]), ahi[i]);
    total->SetParameter(5+3*i, aSeed);
    total->SetParameter(6+3*i, fg[i]->GetParameter(1));
    total->SetParameter(7+3*i, TMath::Abs(fg[i]->GetParameter(2)));
  }
  total->SetParLimits(1,-0.02,0.);
  total->SetParLimits(2,0.,1e9);
  for (int i = 0; i < 5; i++) {
    total->SetParLimits(5+3*i, alo[i], ahi[i]);
    total->SetParLimits(6+3*i, mu[i]*0.90, mu[i]*1.10);
    total->SetParLimits(7+3*i, slo[i], shi[i]);
  }
  total->SetLineColor(kBlack); total->SetLineWidth(2);
  TFitResultPtr r = h->Fit(total,"RS");

  TF1* fbg_draw = new TF1("fbg_draw",
    "exp([0]+[1]*x)+[2]*erfc((x-[3])/(sqrt(2.0)*[4]))", xlo, xhi);
  fbg_draw->SetParameters(total->GetParameter(0), total->GetParameter(1),
    total->GetParameter(2),
    total->GetParameter(18),   // erfc centre = 41.6 keV mu
    total->GetParameter(19));  // erfc width  = 41.6 keV sigma
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
    const char* logFile = "kr_reso_log_dpl.txt";
    bool writeHeader = false;
    if (FILE* chk = fopen(logFile,"r")) { fclose(chk); } else { writeHeader = true; }
    FILE* logf = fopen(logFile,"a");
    if (logf) {
      if (writeHeader)
        fprintf(logf,"%-10s  %-38s  %8s  %8s  %8s  %8s  %8s  %8s  %8s\n",
          "date","tag",
          "R_9.4%","R_12.6%","R_29.1%","R_32.2%","R_41.6%",
          "mu_41.6","sig_41.6");
      time_t now = time(nullptr);
      char datebuf[12];
      strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", localtime(&now));
      double reso[5];
      for (int i = 0; i < 5; i++) {
        double mu_i    = total->GetParameter(6+3*i);
        double sigma_i = TMath::Abs(total->GetParameter(7+3*i));
        reso[i] = (mu_i > 0.) ? 100.*sigma_i/mu_i : -1.;
      }
      double mu41f  = total->GetParameter(6+3*4);
      double sig41f = TMath::Abs(total->GetParameter(7+3*4));
      fprintf(logf,"%-10s  %-38s  %8.3f  %8.3f  %8.3f  %8.3f  %8.3f  %8.1f  %8.1f\n",
        datebuf, tag.c_str(),
        reso[0],reso[1],reso[2],reso[3],reso[4],
        mu41f, sig41f);
      fclose(logf);
      printf("  → appended to %s\n\n", logFile);
    }
  }
}

// ── Main ─────────────────────────────────────────────────────────────────────
void plotDplClusters(   
 	const char* inputFile   = "tpcBoxClusters.root",
    const char* rocSel      = "ALL",
    int         sectorSel   = -1,
    bool        qualityCuts = true,
    const char* outputPdf   = "",
    const char* userTag     = "")
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
    std::cout << "Invalid sectorSel " << sectorSel << ". Use -1 (all) or 0-35." << std::endl;
    return;
  }

  // ── Tag: user-supplied or basename of run directory ───────────────────────
  std::string tag(userTag);
  if (tag.empty()) {
    char cwdbuf[4096] = {};
    if (getcwd(cwdbuf, sizeof(cwdbuf))) {
      std::string cwd(cwdbuf);
      auto slash = cwd.rfind('/');
      if (slash != std::string::npos) tag = cwd.substr(slash+1);
    }
  }

  // ── Histogram setup ───────────────────────────────────────────────────────
  static const char* kRocNames[4] = { "IROC","OROC1","OROC2","OROC3" };
  const int    nBins = 400;
  const double xMax  = 6000.;
  const double binW  = xMax / nBins; // 15 ADC

  TH1F* hCharge[36][4];
  TH2F* h2DSize[36][4];

  for (int s = 0; s < 36; s++) {
    for (int r = 0; r < 4; r++) {
      hCharge[s][r] = new TH1F(
        Form("hCharge_Sec%02d_%s",s,kRocNames[r]),
        Form("Sector %d -- %s;Total cluster charge (ADC counts);Entries / %.0f ADC",
             s, kRocNames[r], binW),
        nBins, 0., xMax);
      hCharge[s][r]->SetDirectory(nullptr);
      h2DSize[s][r] = new TH2F(
        Form("h2DSize_Sec%02d_%s",s,kRocNames[r]),
        Form("Sector %d -- %s;Total cluster charge (ADC counts);Cluster size (digits)",
             s, kRocNames[r]),
        200, 0., xMax, 120, 0., 120.);
      h2DSize[s][r]->SetDirectory(nullptr);
    }
  }

  // Overview histograms (all sectors, all ROCs)
  TH1F* hFull = new TH1F("hFull",
    "Kr DPL clusters (all);Q_{tot} [ADC];Entries / 15 ADC", nBins, 0., xMax);
  hFull->SetDirectory(nullptr);
  TH1F* hZoom = new TH1F("hZoom",
    "Kr DPL clusters — zoom;Q_{tot} [ADC];Entries / 10 ADC",
    300, 500., 3500.);
  hZoom->SetDirectory(nullptr);
  TH1F* hSize = new TH1F("hSize",
    "Kr DPL cluster size;Cluster size (digits);Entries",
    100, 0., 100.);
  hSize->SetDirectory(nullptr);
  TH2F* h2All = new TH2F("h2All",
    "Cluster size vs charge (all);Q_{tot} [ADC];Cluster size (digits)",
    300, 0., xMax, 60, 0., 60.);
  h2All->SetDirectory(nullptr);

  // Merged histogram for the fit canvas (selected ROC + sector)
  std::string mergeTitle = (sectorSel >= 0)
    ? Form("Sector %d -- %s", sectorSel, roc.c_str())
    : Form("All sectors -- %s", roc.c_str());
  TH1F* hMerged = new TH1F("hMerged",
    Form("%s;Total cluster charge (ADC counts);Entries / %.0f ADC",
         mergeTitle.c_str(), binW),
    nBins, 0., xMax);
  hMerged->SetDirectory(nullptr);

  // ── Open input file ───────────────────────────────────────────────────────
  auto f = TFile::Open(inputFile);
  if (!f || f->IsZombie()) {
    std::cout << "Cannot open " << inputFile << std::endl; return;
  }
  auto t = (TTree*)f->Get("Clusters");
  if (!t) {
    std::cout << "No 'Clusters' tree in " << inputFile << std::endl; return;
  }
  if (!t->GetBranch("TPCBoxCluster_0")) {
    std::cout << "Expected DPL branches (TPCBoxCluster_N) not found in "
              << inputFile << std::endl;
    std::cout << "For BoxClusters.root (findKrBoxCluster output), use plotBoxClustersMerged.C" << std::endl;
    return;
  }

  std::cout << "Input    : " << inputFile << std::endl;
  std::cout << "TFs      : " << t->GetEntries() << std::endl;
  std::cout << "ROC sel  : " << roc << std::endl;
  std::cout << "Sector   : " << (sectorSel<0 ? "all" : Form("%d",sectorSel)) << std::endl;
  std::cout << "QC cuts  : " << (qualityCuts ? "ON" : "OFF") << std::endl;
  std::cout << "Tag      : " << (tag.empty() ? "(none)" : tag) << std::endl;

  // ── Quality cut lambda ────────────────────────────────────────────────────
  auto passQC = [&](const o2::tpc::KrCluster& c) -> bool {
    if (!qualityCuts) return true;
    return (c.sigmaTime > 0.1f && c.sigmaTime < 1.8f) &&
           (c.sigmaRow  > 0.2f && c.sigmaRow  < 0.6f + c.totCharge/4000.f) &&
           (c.sigmaPad  > 0.1f && c.sigmaPad  < 1.2f);
  };

  // ── Read all 36 branches ──────────────────────────────────────────────────
  std::vector<o2::tpc::KrCluster>* secCls[36] = {};
  for (int s = 0; s < 36; s++)
    t->SetBranchAddress(Form("TPCBoxCluster_%d",s), &secCls[s]);

  long long nTotal = 0, nQCPass = 0, nMerged = 0;

  for (Long64_t ev = 0; ev < t->GetEntries(); ++ev) {
    t->GetEntry(ev);
    for (int s = 0; s < 36; s++) {
      if (!secCls[s]) continue;
      for (auto& c : *secCls[s]) {
        ++nTotal;
        if (!passQC(c)) continue;
        int rocIdx = getRocIndex(c.meanRow);
        if (rocIdx < 0) continue;
        ++nQCPass;
        hCharge[s][rocIdx]->Fill(c.totCharge);
        h2DSize[s][rocIdx]->Fill(c.totCharge, (double)c.size);
        hFull->Fill(c.totCharge);
        hZoom->Fill(c.totCharge);
        hSize->Fill((double)c.size);
        h2All->Fill(c.totCharge, (double)c.size);
        if (inRocSel(rocIdx, roc) && (sectorSel < 0 || s == sectorSel)) {
          hMerged->Fill(c.totCharge);
          ++nMerged;
        }
      }
    }
  }

  printf("Total clusters read : %lld\n", nTotal);
  printf("QC-passed clusters  : %lld (%.1f%%)\n",
    nQCPass, nTotal > 0 ? 100.*nQCPass/nTotal : 0.);
  printf("In canvas selection : %lld\n", nMerged);

  // ── Build Merged/ histograms ──────────────────────────────────────────────
  TH1F* hMgCharge[4];
  TH2F* hMgSize[4];
  for (int r = 0; r < 4; r++) {
    hMgCharge[r] = new TH1F(
      Form("hCharge_Merged_%s",kRocNames[r]),
      Form("All sectors -- %s;Total cluster charge (ADC counts);Entries / %.0f ADC",
           kRocNames[r], binW),
      nBins, 0., xMax);
    hMgCharge[r]->SetDirectory(nullptr);
    hMgSize[r] = new TH2F(
      Form("h2DSize_Merged_%s",kRocNames[r]),
      Form("All sectors -- %s;Total cluster charge (ADC counts);Cluster size (digits)",
           kRocNames[r]),
      200, 0., xMax, 120, 0., 120.);
    hMgSize[r]->SetDirectory(nullptr);
    for (int s = 0; s < 36; s++) {
      hMgCharge[r]->Add(hCharge[s][r]);
      hMgSize[r]->Add(h2DSize[s][r]);
    }
  }
  TH1F* hMgAll = new TH1F("hCharge_Merged_ALL",
    Form("All sectors -- ALL;Total cluster charge (ADC counts);Entries / %.0f ADC", binW),
    nBins, 0., xMax);
  hMgAll->SetDirectory(nullptr);
  TH2F* hMgSzAll = new TH2F("h2DSize_Merged_ALL",
    "All sectors -- ALL;Total cluster charge (ADC counts);Cluster size (digits)",
    200, 0., xMax, 120, 0., 120.);
  hMgSzAll->SetDirectory(nullptr);
  for (int r = 0; r < 4; r++) { hMgAll->Add(hMgCharge[r]); hMgSzAll->Add(hMgSize[r]); }

  // ── Save ROOT file ────────────────────────────────────────────────────────
  auto out = TFile::Open("kr_dpl_spectra.root","RECREATE");
  for (int s = 0; s < 36; s++) {
    auto* secDir = out->mkdir(Form("sector_%02d",s));
    for (int r = 0; r < 4; r++) {
      auto* rd = secDir->mkdir(kRocNames[r]);
      rd->cd();
      hCharge[s][r]->Write();
      h2DSize[s][r]->Write();
    }
  }
  auto* mrgDir = out->mkdir("Merged");
  for (int r = 0; r < 4; r++) {
    auto* rd = mrgDir->mkdir(kRocNames[r]);
    rd->cd();
    hMgCharge[r]->Write();
    hMgSize[r]->Write();
  }
  auto* mrgAll = mrgDir->mkdir("ALL");
  mrgAll->cd();
  hMgAll->Write(); hMgSzAll->Write();
  out->cd();
  hFull->Write(); hZoom->Write(); hSize->Write(); h2All->Write(); hMerged->Write("hMerged");
  out->Close();
  printf("Saved kr_dpl_spectra.root\n");

  // ── Find 41.6 keV peak ────────────────────────────────────────────────────
  double mu41 = -1.;
  {
    int mBin = hFull->FindBin(1000.);
    for (int b = mBin+1; b <= hFull->GetNbinsX(); b++)
      if (hFull->GetBinContent(b) > hFull->GetBinContent(mBin)) mBin = b;
    mu41 = hFull->GetBinCenter(mBin);
  }
  const bool goodSpectrum = (mu41 >= 1200.);
  printf("41.6 keV seed peak  : %.0f ADC%s\n", mu41,
    goodSpectrum ? " [OK]" : " [WRONG SPECTRUM]");

  // ── Canvas 1: 4-panel overview ────────────────────────────────────────────
  const double ratio[5] = { 9.4/41.6,12.6/41.6,29.1/41.6,32.2/41.6,1.0 };
  const char*  plbl[5]  = {
    "T2#gamma 9.4 keV","K#alpha 12.6 keV","29.1 keV","32.2 keV","41.6 keV"
  };
  const char*  slbl[5]  = { "T2#gamma","K#alpha","29.1 keV","32.2 keV","41.6 keV" };
  const int    pcol[5]  = { kOrange+2,kGreen+2,kMagenta,kCyan+2,kRed };

  auto markLine = [](TH1* h, double x, const char* label, int color) {
    TLine* l = new TLine(x, 0, x, h->GetMaximum()*0.85);
    l->SetLineColor(color); l->SetLineStyle(2); l->SetLineWidth(2); l->Draw();
    TLatex* tx = new TLatex(x+15, h->GetMaximum()*0.50, label);
    tx->SetTextSize(0.033); tx->SetTextColor(color); tx->Draw();
  };

  TCanvas* cov = new TCanvas("cov","Kr DPL Cluster Overview",1400,900);
  cov->Divide(2,2);

  // Pad 1: linear full spectrum
  cov->cd(1); gPad->SetLogy(0);
  hFull->SetLineColor(kBlue+1);
  hFull->SetFillColorAlpha(kBlue+1,0.3);
  hFull->Draw("HIST");
  if (goodSpectrum)
    for (int i = 0; i < 5; i++) markLine(hFull, mu41*ratio[i], plbl[i], pcol[i]);
  else
    markLine(hFull, mu41, "max (wrong spectrum)", kRed);

  // Pad 2: log full spectrum
  cov->cd(2); gPad->SetLogy(1);
  hFull->Draw("HIST");
  if (goodSpectrum)
    for (int i = 0; i < 5; i++) markLine(hFull, mu41*ratio[i], slbl[i], pcol[i]);
  else
    markLine(hFull, mu41, "max", kRed);

  // Pad 3: zoom around satellite peaks
  cov->cd(3);
  hZoom->SetLineColor(kBlue+1);
  hZoom->SetFillColorAlpha(kBlue+1,0.3);
  hZoom->Draw("HIST");
  if (goodSpectrum)
    for (int i = 0; i < 5; i++) {
      double xp = mu41*ratio[i];
      if (xp >= 500. && xp <= 3500.) markLine(hZoom, xp, slbl[i], pcol[i]);
    }

  // Pad 4: cluster size vs charge (2D)
  cov->cd(4); gPad->SetRightMargin(0.13); gPad->SetLogz(1);
  h2All->Draw("COLZ");

  std::string overviewPdf = Form("kr_dpl_overview%s.pdf",
    tag.empty() ? "" : ("_"+tag).c_str());
  cov->SaveAs(overviewPdf.c_str());
  printf("Saved %s\n", overviewPdf.c_str());

  // ── Canvas 2: merged spectrum with fit ────────────────────────────────────
  if (nMerged == 0) {
    std::cout << "No clusters in canvas selection — fit canvas not written." << std::endl;
    return;
  }

  std::string pdfName;
  if (std::string(outputPdf).empty()) {
    std::string secStr = (sectorSel<0) ? "allsec" : Form("s%02d",sectorSel);
    pdfName = Form("kr_dpl_%s_%s%s.pdf",
      roc.c_str(), secStr.c_str(), tag.empty() ? "" : ("_"+tag).c_str());
    for (auto& c : pdfName) c = tolower(c);
  } else {
    pdfName = outputPdf;
  }

  TCanvas* cfit = new TCanvas("cfit", mergeTitle.c_str(), 1100, 700);
  cfit->SetLeftMargin(0.10);
  cfit->SetRightMargin(0.05);
  cfit->SetBottomMargin(0.12);

  hMerged->SetLineColor(kBlue+1);
  hMerged->SetLineWidth(2);
  hMerged->Draw("HIST");

  if (goodSpectrum) {
    fitKrSpectrum(hMerged, mu41, tag);
  } else {
    for (int i = 0; i < 5; i++) {
      double xexp = mu41*ratio[i];
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

  cfit->SaveAs(pdfName.c_str());
  printf("Saved %s\n", pdfName.c_str());
}
