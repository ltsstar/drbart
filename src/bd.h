#ifndef GUARD_bd_h
#define GUARD_bd_h

#include <tuple>

#include "rng.h"
#include "info.h"
#include "tree.h"

#ifdef MPIBART
bool bd(tree& x, xinfo& xi, pinfo& pi, RNG& gen, size_t numslaves);
#else
bool bd(tree& x, xinfo& xi, dinfo& di, pinfo& pi, RNG& gen);
std::tuple<bool, bool> bdprec(tree& x, xinfo& xi, dinfo& di, pinfo& pi, RNG& gen);
std::tuple<bool, bool> bdhet(tree& x, xinfo& xi, dinfo& di, double* phi, pinfo& pi, RNG& gen);
bool bd_rj(tree& x, xinfo& xi, dinfo& di, pinfo& pi, RNG& gen);
#endif

#endif
