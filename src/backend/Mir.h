#pragma once

// **The IR's registers** - see docs/OPTIMIZER-IR.md. A pseudo register is
// numbered above every physical one; the passes before allocation see pseudos
// wherever nothing pins a value to the register it happens to be in.

#include "OptFlow.h"

#include <vector>

namespace mir {

constexpr int kFirstPseudo = opt::kPhysical;
inline bool isPseudo(int id) { return id >= kFirstPseudo; }

// **A web is one value's life in one register**: every definition joined with
// every use it reaches. Those the ABI does not pin become pseudos.
struct Webs {
    std::vector<int> home;      // the register each pseudo was found in
    int count() const { return static_cast<int>(home.size()); }
};
Webs buildWebs(opt::Stream &s, opt::Flow &f, const opt::Convention &c);

// Every pseudo named in the stream given its register: `colour[p - kFirstPseudo]`.
void assign(opt::Stream &s, const std::vector<int> &colour);

}
