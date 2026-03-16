#include "linkanalysis/sixdof_core.h"
#include <iostream>
#include <cassert>
#include <cmath>
using namespace sixdof;
void testRK4() {
    State s; s.quat = qidentity(); s.omega = {0.3, -0.1, 0.2}; s.mass = 10;
    InertiaTensor I = inertiaDiag(5, 8, 6);
    auto fn = [](const State&, double) -> ForcesTorques { return {}; };
    double t = 0;
    for (int i = 0; i < 2000; i++) { s = rk4Step(s, I, 0.01, t, fn); t += 0.01; }
    double T0 = 0.5*(5*0.3*0.3+8*0.1*0.1+6*0.2*0.2);
    double T1 = 0.5*(I[0]*s.omega[0]*s.omega[0]+I[1]*s.omega[1]*s.omega[1]+I[2]*s.omega[2]*s.omega[2]);
    assert(std::abs(T1 - T0) / T0 < 1e-4);
    std::cout << "  6DOF energy conservation ✓\n";
}
int main() { std::cout << "=== linkanalysis 6DOF ===\n"; testRK4();
    std::cout << "All linkanalysis 6DOF tests passed.\n"; return 0; }
