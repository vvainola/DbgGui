// MIT License
//
// Copyright (c) 2022 vvainola
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "dbg_gui_wrapper.h"
#include "zero_crossing_freq_est.h"
#include "moving_average.h"
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

struct struct_with_arrays {
    double x[4];
    double y[4];
};
struct_with_arrays swa;

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


double test_freq = 50.3;
ZeroCrossingFreqEst freq_est{
    .dead_time = 1e-3f,
    .sampling_period = 500e-6f};

double timestamp = 0;
MovingAverage<2000> movavg;
double theta;

void t_500us();
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

    movavg.init(0, 2000 / test_freq);

    while (!gui.isClosed()) {
        gui.sample();
        timestamp += 10e-6;
        sfl = (float)timestamp;
        theta += 2 * PI * test_freq * 10e-6;
        theta = fmod(theta, 2 * PI);
        g::sine = sin(theta);
        g::abc.a = sin(theta);
        g::abc.b = sin(theta - 2.0 * PI / 3.0);
        g::abc.c = sin(theta - 4.0 * PI / 3.0);
        g::abc2.a = sin(theta + g::abc2_angle);
        g::abc2.b = sin(theta - 2.0 * PI / 3.0 + g::abc2_angle);
        g::abc2.c = sin(theta - 4.0 * PI / 3.0 + g::abc2_angle);
        g::xy = g::abc_to_xy(g::abc);
        g::xy2 = g::abc_to_xy(g::abc2);
        g::booli = g::xy2.x > 0.5;
        t_500us();
    }

    return 0;
}

void t_500us() {
    static int n = 50;
    if (n <= 0) {
        n = 50;
        
        estimateFreq(&freq_est, (float)g::abc.a);
        movavg.step((float)(g::xy.x));
        movavg.setLength(2000 / freq_est.out_estimated_freq);
    } 
    n--;
}
