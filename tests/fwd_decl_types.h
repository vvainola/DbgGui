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

// Cross-TU forward-declared enum. Only the forward declaration appears in
// this header; the complete definition lives in fwd_decl_types.cpp. The
// global below is defined in symbols_test.cpp, the TU that sees only the
// forward declaration. This forces the PDB to emit an LF_ENUM forward
// reference for the global's type index, which the symbol walker must
// resolve to the full definition to read the enumerator field list.
enum class CrossTuEnum : int;

// Returns a CrossTuEnum value. Implemented in fwd_decl_types.cpp where the
// enum is complete; the body references an enumerator, forcing MSVC to emit
// the full definition alongside the forward reference in the merged PDB.
CrossTuEnum getCrossTuEnumValue();
