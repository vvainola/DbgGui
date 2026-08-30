// This TU carries the full DWARF for FwdDeclInner because it defines methods
// for that class. The other TU (symbols_test.cpp) only uses the layout, so
// GCC's limited-debug-info default leaves FwdDeclInner forward-declared
// there.
#include "fwd_decl_types.h"

FwdDeclInner::~FwdDeclInner() = default;
int  FwdDeclInner::getA() const { return a; }
void FwdDeclInner::setA(int v) { a = v; }

CacheFwdDeclType::~CacheFwdDeclType() = default;
collision_a::Node::~Node() = default;
collision_b::Node::~Node() = default;

enum class CrossTuEnum : int {
    CrossTuA = 10,
    CrossTuB = 20
};

CrossTuEnum getCrossTuEnumValue() {
    return CrossTuEnum::CrossTuB;
}
