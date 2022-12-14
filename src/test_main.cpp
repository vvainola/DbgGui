#include "dbg_gui_wrapper.h"
#include <thread>

struct Vector_ABC {
    double a;
    double b;
    double c;
};
constexpr double PI = 3.141592652;
const double SQRT3 = sqrt(3.0);

namespace g {
uint32_t u32 = 4;
uint32_t& u32_ref = u32;
float f32;
double f64;
double sine;

struct XY {
    double x;
    double y;
};

XY xy;
XY xy2;

Vector_ABC abc;
Vector_ABC abc2;
double abc2_angle = 10. * PI / 180.;

void func(){};
void (*funcp)() = &func;

struct A {
    void hello(){};
    int m_a = 2;

    void (*m_ap)() = &func;

    static void f(){};
};
A a;

struct B {
    double m_b = 1;
    A a;

    void (*m_ap)() = A::f;
};
B b;

struct C {
    A a;
    B b;
    float m_c = 0;
    B m_d[3];
};

struct D : public C {
    float m_e;
};

bool booli;
long g_long = 123;
C a_struct;
C* p_struct = &a_struct;
int const* const p_int = &p_struct->a.m_a;
float const* const p_float = &p_struct->m_c;
double const* const p_double = &p_struct->b.m_b;
float* p_null = nullptr;

C array[50];
C array2[45];
D d;

enum EnumWithNeg {
    first = -1,
    second = 1,
    third = 3,
    value_with_long_name = 4,
};
EnumWithNeg enum_with_neg = first;

union BitField3 {
    uint32_t u32;
    struct {
        uint32_t b0 : 7;
        uint32_t b9 : 17;
    } b;
};
BitField3 bitfield3;

union BitField {
    uint16_t u16;
    struct {
        bool b1 : 1;
        bool b2 : 1;
        bool b3 : 1;
        bool b4 : 1;
        bool b5 : 1;
        bool b6 : 1;
        bool b7 : 1;
        bool b8 : 1;
        bool b9 : 1;
        bool b10 : 1;
        bool b11 : 1;
        bool b12 : 1;
        bool b13 : 1;
        bool b14 : 1;
        bool b15 : 1;
        bool b16 : 1;
    } b;
};
BitField bitfield;

union BitField2 {
    uint16_t u16;
    struct {
        uint16_t b0 : 1;
        uint16_t b1 : 1;
        uint16_t b2 : 3;
        uint16_t b5 : 3;
        uint16_t b8 : 2;
        uint16_t b10 : 1;
        uint16_t b11 : 2;
        uint16_t b13 : 1;
    } b;
};
BitField2 bitfield2;

XY abc_to_xy(Vector_ABC const& in) {
    return XY{
        2.0 / 3.0 * in.a - 1.0 / 3.0 * in.b - 1.0 / 3.0 * in.c,
        SQRT3 / 3.0 * in.b - SQRT3 / 3.0 * in.c};
}

Vector_ABC xy_to_abc(XY in) {
    return Vector_ABC{
        in.x,
        -0.5 * in.x + 0.5 * SQRT3 * in.y,
        -0.5 * in.x - 0.5 * SQRT3 * in.y};
}
} // namespace g


double timestamp = 0;
int main(int, char**) {
    static float sfl;
    DbgGuiWrapper gui(10e-6);
    gui.addScalar(&g::f64, "group 2", "g_f64");
    gui.addScalar(&g::f32, "group 1", "g_f32_2");
    gui.addScalar(&g::f32, "group 1", "g_f32_1");
    gui.addScalar(&g::u32, "group 2", "g_u32_1");
    gui.addScalar(&g::f64, "group 2", "g_a64");
    gui.addScalar(&g::sine, "group 2", "sine");
    gui.addVector(&g::xy.x, &g::xy.y, "group 4", "xy1");
    gui.addVector(&g::xy2.x, &g::xy2.y, "group 3", "xy2");
    gui.addVector(&g::xy.x, &g::xy.y, "group 3", "xy1");
    gui.startUpdateLoop();

    while (!gui.isClosed()) {
        gui.sample();
        timestamp += 10e-6;
        sfl = (float)timestamp;
        g::sine = sin(10. * 2 * PI * timestamp);
        g::abc.a = sin(10. * 2 * PI * timestamp);
        g::abc.b = sin(10. * 2 * PI * timestamp - 2.0 * PI / 3.0);
        g::abc.c = sin(10. * 2 * PI * timestamp - 4.0 * PI / 3.0);
        g::abc2.a = sin(10. * 2 * PI * timestamp + g::abc2_angle);
        g::abc2.b = sin(10. * 2 * PI * timestamp - 2.0 * PI / 3.0 + g::abc2_angle);
        g::abc2.c = sin(10. * 2 * PI * timestamp - 4.0 * PI / 3.0 + g::abc2_angle);
        g::xy = g::abc_to_xy(g::abc);
        g::xy2 = g::abc_to_xy(g::abc2);
        g::booli = g::xy2.x > 0.5;
    }

    return 0;
}
