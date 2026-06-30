// fitKrSpectrum: staged multi-Gaussian fit + exponential+erfc background.
// All peak positions and limits are expressed relative to mu41 (the 41.6 keV
// peak found as the histogram maximum), so the fit adapts to any ADC scaling.
//
// Parameters [0..19]:
//   [0],[1]       expo background (ln(A), slope)
//   [2],[3],[4]   erfc shoulder (amp, center, width)
//   [5..7]        Gaussian: T2-gamma  9.4 keV  (~0.226 × mu41)
//   [8..10]       Gaussian: K-alpha  12.6 keV  (~0.303 × mu41)
//   [11..13]      Gaussian: 29.1 keV           (~0.700 × mu41)
//   [14..16]      Gaussian: 32.2 keV           (~0.774 × mu41)
//   [17..19]      Gaussian: main peak 41.6 keV (mu41)

void fitKrSpectrum(TH1F* h, double mu41)
{
  // Energy ratios relative to 41.6 keV
  const double ratio[5] = { 9.4/41.6, 12.6/41.6, 29.1/41.6, 32.2/41.6, 1.0 };
  const char*  lbl[5]   = {
    "T2#gamma 9.4 keV", "K#alpha 12.6 keV",
    "29.1 keV", "32.2 keV", "41.6 keV (main)"
  };
  const int col[5] = { kOrange+2, kGreen+2, kMagenta, kCyan+2, kRed };

  double mu[5], sig0[5];
  for (int i = 0; i < 5; i++) {
    mu[i]   = mu41 * ratio[i];
    sig0[i] = mu[i] * 0.06;
  }

  const double xlo = mu41 * 0.11;
  const double xhi = mu41 * 1.20;

  // ── Stage 1: seed background below satellite peaks ───────────────────────
  TF1* fbg = new TF1("fbg_pre", "expo", mu41*0.07, mu41*0.17);
  h->Fit(fbg, "RQN0");

  // ── Stage 2: pre-fit each Gaussian individually ──────────────────────────
  const double rlo[5] = {
    mu41*0.175, mu41*0.265, mu41*0.625, mu41*0.720, mu41*0.865
  };
  const double rhi[5] = {
    mu41*0.280, mu41*0.365, mu41*0.760, mu41*0.835, mu41*1.180
  };
  TF1* fg[5];
  for (int i = 0; i < 5; i++) {
    fg[i] = new TF1(Form("fg_pre%d", i), "gaus", rlo[i], rhi[i]);
    fg[i]->SetParameters(
      TMath::Max(h->GetBinContent(h->FindBin(mu[i])), 1.),
      mu[i], sig0[i]);
    fg[i]->SetParLimits(2, mu[i]*0.01, mu[i]*0.20);
    h->Fit(fg[i], "RQN0");
  }

  // ── Stage 3: global fit ──────────────────────────────────────────────────
  TF1* total = new TF1("fitTotal",
    "exp([0]+[1]*x)"
    " + [2]*erfc((x-[3])/(sqrt(2.0)*[4]))"
    " + [5]*exp(-0.5*pow((x-[6])/[7],2))"
    " + [8]*exp(-0.5*pow((x-[9])/[10],2))"
    " + [11]*exp(-0.5*pow((x-[12])/[13],2))"
    " + [14]*exp(-0.5*pow((x-[15])/[16],2))"
    " + [17]*exp(-0.5*pow((x-[18])/[19],2))",
    xlo, xhi);
  total->SetNpx(3000);

  // Seed from pre-fits
  total->SetParameter(0, fbg->GetParameter(0));
  total->SetParameter(1, fbg->GetParameter(1));
  double erfc_seed = h->GetBinContent(h->FindBin(mu41*0.905)) * 0.4;
  total->SetParameter(2, erfc_seed > 1. ? erfc_seed : 1.);
  total->SetParameter(3, mu41 * 0.904);
  total->SetParameter(4, mu41 * 0.103);
  for (int i = 0; i < 5; i++) {
    total->SetParameter(5 + 3*i, TMath::Max(fg[i]->GetParameter(0), 1.));
    total->SetParameter(6 + 3*i, fg[i]->GetParameter(1));
    total->SetParameter(7 + 3*i, TMath::Abs(fg[i]->GetParameter(2)));
  }

  // Limits
  total->SetParLimits(1, -0.02, 0.);
  total->SetParLimits(2, 0., 1e9);
  total->SetParLimits(3, mu41*0.55, mu41*1.10);
  total->SetParLimits(4, mu41*0.022, mu41*0.30);
  for (int i = 0; i < 5; i++) {
    total->SetParLimits(5 + 3*i, 0., 1e9);
    total->SetParLimits(6 + 3*i, mu[i]*0.85, mu[i]*1.15);
    total->SetParLimits(7 + 3*i, mu[i]*0.01, mu[i]*0.20);
  }

  total->SetLineColor(kBlack);
  total->SetLineWidth(2);
  TFitResultPtr r = h->Fit(total, "RS");

  // ── Draw background component ────────────────────────────────────────────
  TF1* fbg_draw = new TF1("fbg_draw",
    "exp([0]+[1]*x) + [2]*erfc((x-[3])/(sqrt(2.0)*[4]))",
    xlo, xhi);
  fbg_draw->SetParameters(
    total->GetParameter(0), total->GetParameter(1),
    total->GetParameter(2), total->GetParameter(3),
    total->GetParameter(4));
  fbg_draw->SetNpx(3000);
  fbg_draw->SetLineColor(kGray+1);
  fbg_draw->SetLineStyle(7);
  fbg_draw->SetLineWidth(2);
  fbg_draw->Draw("SAME");

  // ── Draw Gaussian components ─────────────────────────────────────────────
  for (int i = 0; i < 5; i++) {
    TF1* gc = new TF1(Form("gc%d", i),
      "[0]*exp(-0.5*pow((x-[1])/[2],2))", xlo, xhi);
    gc->SetParameters(
      total->GetParameter(5 + 3*i),
      total->GetParameter(6 + 3*i),
      total->GetParameter(7 + 3*i));
    gc->SetNpx(3000);
    gc->SetLineColor(col[i]);
    gc->SetLineStyle(2);
    gc->SetLineWidth(2);
    gc->Draw("SAME");

    double px = total->GetParameter(6 + 3*i);
    double py = total->GetParameter(5 + 3*i);
    if (py > h->GetMaximum() * 0.015) {
      TLatex* tx = new TLatex(px + mu41*0.008, py * 0.65, lbl[i]);
      tx->SetTextSize(0.028);
      tx->SetTextColor(col[i]);
      tx->Draw();
    }
  }

  // ── Console summary ──────────────────────────────────────────────────────
  double chi2ndf = (r->Ndf() > 0) ? r->Chi2() / r->Ndf() : -1.;
  printf("\n==== Kr-83m spectrum fit =================================\n");
  printf("  41.6 keV peak seed: %.1f ADC\n", mu41);
  printf("  chi2/ndf = %.1f / %d = %.2f\n", r->Chi2(), r->Ndf(), chi2ndf);
  printf("  Background: ln(A)=%.2f  slope=%.5f /ADC\n",
    total->GetParameter(0), total->GetParameter(1));
  printf("  Erfc shoulder: center=%.0f ADC  width=%.0f ADC\n",
    total->GetParameter(3), total->GetParameter(4));
  printf("\n  %-22s  %14s  %12s  %12s\n",
    "Peak", "mu_fit [ADC]", "sigma [ADC]", "mu_exp [ADC]");
  printf("  %-22s  %14s  %12s  %12s\n",
    "----", "------------", "-----------", "------------");
  for (int i = 0; i < 5; i++) {
    printf("  %-22s  %7.1f +/- %4.1f  %5.1f +/- %3.1f  %7.1f\n",
      lbl[i],
      total->GetParameter(6 + 3*i), total->GetParError(6 + 3*i),
      TMath::Abs(total->GetParameter(7 + 3*i)), total->GetParError(7 + 3*i),
      mu[i]);
  }
  printf("==========================================================\n\n");
}

void plotBoxClusters(const char* inputFile = "BoxClusters.root")
{
  gSystem->Load("libO2TPCReconstruction");
  gSystem->Load("libO2DataFormatsTPC");

  auto f = TFile::Open(inputFile);
  if (!f || f->IsZombie()) { cout << "Cannot open " << inputFile << endl; return; }
  auto t = (TTree*)f->Get("Clusters");
  if (!t) { cout << "No Clusters tree" << endl; return; }
  cout << "Tree entries (timeframes): " << t->GetEntries() << endl;

  TH1F *hFull = new TH1F("hFull",
    "Kr cluster Q_{tot};Q_{tot} [ADC];Clusters / 15 ADC",
    400, 0, 6000);
  TH1F *hZoom = new TH1F("hZoom",
    "Kr cluster Q_{tot} peak region;Q_{tot} [ADC];Clusters / 10 ADC",
    300, 500, 3500);
  TH1F *hFit = new TH1F("hFit",
    "Kr cluster Q_{tot} (fit);Q_{tot} [ADC];Clusters / 15 ADC",
    400, 0., 6000.);
  TH1F *hSize = new TH1F("hSize",
    "Kr cluster size;N_{digits};Clusters",
    100, 0, 100);
  TH2F *h2 = new TH2F("h2",
    "Cluster size vs charge;Q_{tot} [ADC];N_{digits}",
    300, 0, 4500, 60, 0, 60);

  std::vector<o2::tpc::KrCluster>* cls = nullptr;
  t->SetBranchAddress("cls", &cls);

  Long64_t total = 0;
  for (Long64_t ev = 0; ev < t->GetEntries(); ++ev) {
    t->GetEntry(ev);
    if (!cls) continue;
    for (auto& c : *cls) {
      hFull->Fill(c.totCharge);
      hZoom->Fill(c.totCharge);
      hFit ->Fill(c.totCharge);
      hSize->Fill(c.size);
      h2   ->Fill(c.totCharge, c.size);
      total++;
    }
  }

  // ── Find 41.6 keV peak as histogram maximum above 1000 ADC ───────────────
  double mu41 = -1.;
  {
    int mBin = hFull->FindBin(1000.);
    for (int b = mBin + 1; b <= hFull->GetNbinsX(); b++)
      if (hFull->GetBinContent(b) > hFull->GetBinContent(mBin)) mBin = b;
    mu41 = hFull->GetBinCenter(mBin);
  }

  const bool goodSpectrum = (mu41 >= 2500.);

  cout << "Total clusters : " << total << endl;
  cout << "Mean Q_tot     : " << hFull->GetMean() << " ADC" << endl;
  cout << "Mean size      : " << hSize->GetMean() << " digits" << endl;
  if (goodSpectrum) {
    cout << "41.6 keV peak  : " << mu41 << " ADC  [OK]" << endl;
  } else {
    cout << "WRONG SPECTRUM : max peak at " << mu41
         << " ADC (expected > 2500 ADC for 41.6 keV)" << endl;
  }

  // Peak positions relative to mu41
  const double ratio[5] = { 9.4/41.6, 12.6/41.6, 29.1/41.6, 32.2/41.6, 1.0 };
  const char*  plbl[5]  = {
    "T2#gamma 9.4 keV", "K#alpha 12.6 keV",
    "29.1 keV", "32.2 keV", "41.6 keV (dominant)"
  };
  const int pcol[5] = { kOrange+2, kGreen+2, kMagenta, kCyan+2, kRed };
  double mpos[5];
  for (int i = 0; i < 5; i++) mpos[i] = mu41 * ratio[i];

  auto mark = [](TH1* h, double x, const char* label, int color) {
    TLine* l = new TLine(x, 0, x, h->GetMaximum() * 0.85);
    l->SetLineColor(color); l->SetLineStyle(2); l->SetLineWidth(2); l->Draw();
    TLatex* tx = new TLatex(x + 15, h->GetMaximum() * 0.50, label);
    tx->SetTextSize(0.033); tx->SetTextColor(color); tx->Draw();
  };

  // ── Canvas 1: overview panels ────────────────────────────────────────────
  TCanvas* c1 = new TCanvas("c1", "Kr Box Cluster Spectra", 1400, 900);
  c1->Divide(2, 2);

  c1->cd(1); gPad->SetLogy(0);
  hFull->SetLineColor(kBlue+1);
  hFull->SetFillColorAlpha(kBlue+1, 0.3);
  hFull->Draw("HIST");
  if (goodSpectrum) {
    for (int i = 0; i < 5; i++) mark(hFull, mpos[i], plbl[i], pcol[i]);
  } else {
    mark(hFull, mu41, "max (wrong spectrum)", kRed);
  }

  c1->cd(2); gPad->SetLogy(1);
  hFull->Draw("HIST");
  if (goodSpectrum) {
    const char* slbl[5] = {"T2#gamma","K#alpha","29.1 keV","32.2 keV","41.6 keV"};
    for (int i = 0; i < 5; i++) mark(hFull, mpos[i], slbl[i], pcol[i]);
  } else {
    mark(hFull, mu41, "max", kRed);
  }

  c1->cd(3);
  hZoom->SetLineColor(kBlue+1);
  hZoom->SetFillColorAlpha(kBlue+1, 0.3);
  hZoom->Draw("HIST");
  if (goodSpectrum) {
    const char* slbl[5] = {"T2#gamma","K#alpha","29.1 keV","32.2 keV","41.6 keV"};
    for (int i = 0; i < 5; i++) mark(hZoom, mpos[i], slbl[i], pcol[i]);
  } else {
    mark(hZoom, mu41, "max (wrong spectrum)", kRed);
  }

  c1->cd(4);
  gPad->SetRightMargin(0.13);
  h2->SetOption("COLZ");
  h2->Draw("COLZ");
  gPad->SetLogz(1);

  c1->SaveAs("kr_box_plots.pdf");
  cout << "Saved kr_box_plots.pdf" << endl;

  // ── Canvas 2: peak fit ───────────────────────────────────────────────────
  TCanvas* c2 = new TCanvas("c2", "Kr-83m peak fit", 1100, 700);
  gPad->SetLeftMargin(0.10);
  gPad->SetRightMargin(0.05);
  hFit->SetLineColor(kBlue+1);
  hFit->SetFillColorAlpha(kBlue+1, 0.25);
  hFit->Draw("HIST");
  if (goodSpectrum) {
    fitKrSpectrum(hFit, mu41);
  } else {
    TLatex* msg = new TLatex(0.15, 0.85,
      Form("WRONG SPECTRUM: max at %.0f ADC (expected > 2500)", mu41));
    msg->SetNDC();
    msg->SetTextColor(kRed);
    msg->SetTextSize(0.04);
    msg->Draw();
  }
  c2->SaveAs("kr_fit.pdf");
  cout << "Saved kr_fit.pdf" << endl;

  // ── Write to ROOT file ───────────────────────────────────────────────────
  auto out = TFile::Open("kr_box_plots.root", "RECREATE");
  hFull->Write(); hZoom->Write(); hFit->Write();
  hSize->Write(); h2->Write();
  c1->Write(); c2->Write();
  out->Close();
}
