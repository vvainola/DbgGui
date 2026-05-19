#pragma once

// A class with virtual methods triggers GCC's limited-debug-info: full DWARF
// for the class is emitted only in the translation unit that carries the
// vtable (the one that first defines a non-inline virtual). Other TUs that
// only use the layout (no method definitions) get a DW_AT_declaration=1
// forward declaration in their DWARF. That is the scenario the dbg_symbols
// walker must handle when resolving member types.
struct FwdDeclInner {
    int    a;
    double b;
    virtual ~FwdDeclInner();
    virtual int  getA() const;
    virtual void setA(int v);
};

struct FwdDeclOuter {
    FwdDeclInner inner;
    int          outer_value;
};
