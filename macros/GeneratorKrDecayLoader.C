// GeneratorKrDecayLoader.C
//
// CLING-interpreted wrapper that loads the compiled libGeneratorKrDecay
// and calls its factory function. This is the file passed to o2-sim as
// GeneratorExternal.fileName; the compiled library does the real work.
//
// Run ./build_kr_generator.sh before using this.

#include "FairGenerator.h"
#include <iostream>
// dlfcn.h is available on Linux and macOS; needed for the dlopen/dlsym
// fallback when TSystem::DynFindSymbol can't see the symbol (macOS RTLD
// scoping issue with two-level namespaces).
#include <dlfcn.h>

FairGenerator* GeneratorKrDecay()
{
  // Load the compiled library (gSystem->Load is idempotent if already loaded)
  TString lib = "/tmp/libGeneratorKrDecay.dylib";
  if (gSystem->AccessPathName(lib)) {  // returns true if file does NOT exist
    lib = "/tmp/libGeneratorKrDecay.so";
  }
  if (gSystem->AccessPathName(lib)) {
    std::cerr << "[KrLoader] ERROR: neither .dylib nor .so found in /tmp.\n"
              << "[KrLoader] Run ./build_kr_generator.sh first.\n";
    return nullptr;
  }

  int ret = gSystem->Load(lib);
  if (ret < 0) {
    std::cerr << "[KrLoader] ERROR: gSystem->Load(" << lib << ") failed (ret="
              << ret << ").\n"
              << "[KrLoader] Run ./build_kr_generator.sh first.\n";
    return nullptr;
  }
  std::cout << "[KrLoader] Loaded compiled library: " << lib << "\n";

  typedef FairGenerator* (*FactoryFn)();
  FactoryFn factory = nullptr;

  // Primary path: TSystem::DynFindSymbol (works reliably on Linux).
  Func_t sym = gSystem->DynFindSymbol("*", "GeneratorKrDecayCreate");
  if (sym) {
    factory = reinterpret_cast<FactoryFn>(sym);
    std::cout << "[KrLoader] Symbol resolved via DynFindSymbol.\n";
  } else {
    // Fallback: POSIX dlopen/dlsym with RTLD_GLOBAL so the symbol is
    // promoted into the global namespace. Needed on macOS where
    // two-level namespace prevents DynFindSymbol from seeing symbols
    // loaded by a previous dlopen without RTLD_GLOBAL.
    std::cout << "[KrLoader] DynFindSymbol failed; trying dlopen/dlsym fallback.\n";
    void* handle = dlopen(lib.Data(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
      std::cerr << "[KrLoader] ERROR: dlopen failed: " << dlerror() << "\n";
      return nullptr;
    }
    void* rawSym = dlsym(handle, "GeneratorKrDecayCreate");
    if (!rawSym) {
      std::cerr << "[KrLoader] ERROR: dlsym(GeneratorKrDecayCreate) failed: "
                << dlerror() << "\n";
      dlclose(handle);
      return nullptr;
    }
    // Converting a data pointer from dlsym to a function pointer is
    // technically UB in C++, but is universally supported on POSIX
    // platforms and is the standard pattern for plugin loaders.
    *reinterpret_cast<void**>(&factory) = rawSym;
    std::cout << "[KrLoader] Symbol resolved via dlsym fallback.\n";
  }

  return factory();
}
