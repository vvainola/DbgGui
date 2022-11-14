#include "debug_gui.h"
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

XY<double> xy;
XY<double> xy2;

Vector_ABC abc;

void func(){};
void (*funcp)() = &func;

struct A {
    void hello(){};
    int m_a = 2;

    void (*m_ap)() = &func;
};
A a;

struct B {
    double m_b = 1;
    A a;
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
float const* const p_float = &p_struct->m_c;
float* p_null = nullptr;

C array[50];
D d;

enum EnumWithNeg {
    first = -1,
    second = 1,
    third = 3,
    value_with_long_name = 4,
};
EnumWithNeg enum_with_neg = first;

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

} // namespace g

XY<double> abc_to_xy(Vector_ABC const& in) {
    return XY<double>{
        2.0 / 3.0 * in.a - 1.0 / 3.0 * in.b - 1.0 / 3.0 * in.c,
        SQRT3 / 3.0 * in.b - SQRT3 / 3.0 * in.c};
}

Vector_ABC xy_to_abc(XY<double> in) {
    return Vector_ABC{
        in.x,
        -0.5 * in.x + 0.5 * SQRT3 * in.y,
        -0.5 * in.x - 0.5 * SQRT3 * in.y};
}

int main(int, char**) {
    static float sfl;
    DbgGui gui;
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

    double t = 0;
    while (!gui.isClosed()) {
        gui.sample(t);
        t += 10e-6;
        sfl = (float)t;
        g::sine = sin(10. * 2 * PI * t);
        g::abc.a = sin(10. * 2 * PI * t);
        g::abc.b = sin(10. * 2 * PI * t - 2.0 * PI / 3.0);
        g::abc.c = sin(10. * 2 * PI * t - 4.0 * PI / 3.0);
        g::xy = abc_to_xy(g::abc);
        g::xy2.x = g::xy.x * 1.1;
        g::xy2.y = g::xy.y * 1.1;
        g::booli = g::xy2.x > 0.5;
    }

    return 0;
}
