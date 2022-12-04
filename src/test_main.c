#include "dbg_gui_wrapper.h"
#include <math.h>

#define PI 3.14159265358979323846 // pi

typedef struct {
    double a;
    double b;
    double c;
} Vector_ABC;

uint32_t u32 = 4;
float f32;
double f64;
double sine;

Vector_ABC abc;

void func(){};
void (*funcp)() = &func;

typedef struct {
    int m_a;
} A;
A a;

typedef struct {
    double m_b;
    A a;
} B;
B b;

typedef struct {
    A a;
    B b;
    float m_c;
    B m_d[3];
} C;

long g_long = 123;
C a_struct;
C* p_struct = &a_struct;
float* p_null = NULL;

C array[50];
C array2[45];

typedef enum {
    first = -1,
    second = 1,
    third = 3,
    value_with_long_name = 4,
} EnumWithNeg;
EnumWithNeg enum_with_neg;

typedef union  {
    uint16_t u16;
    struct {
        uint16_t b1 : 1;
        uint16_t b2 : 1;
        uint16_t b3 : 1;
        uint16_t b4 : 1;
        uint16_t b5 : 1;
        uint16_t b6 : 1;
        uint16_t b7 : 1;
        uint16_t b8 : 1;
        uint16_t b9 : 1;
        uint16_t b10 : 1;
        uint16_t b11 : 1;
        uint16_t b12 : 1;
        uint16_t b13 : 1;
        uint16_t b14 : 1;
        uint16_t b15 : 1;
        uint16_t b16 : 1;
    } b;
} BitField;
BitField bitfield;

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    DbgGui_addScalar_f64(&f64, "group 2", "g_f64");
    DbgGui_addScalar_f64(&f64, "group 2", "g_f64");
    DbgGui_addScalar_f32(&f32, "group 1", "g_f32_2");
    DbgGui_addScalar_f32(&f32, "group 1", "g_f32_1");
    DbgGui_addScalar_u32(&u32, "group 2", "g_u32_1");
    DbgGui_addScalar_f64(&f64, "group 2", "g_a64");
    DbgGui_addScalar_f64(&sine, "group 2", "sine");
    DbgGui_startUpdateLoop();

    double t = 0;
    while (!DbgGui_isClosed()) {
        DbgGui_sample(t);
        t += 10e-6;
        sine = sin(10. * 2 * PI * t);
    }

    return 0;
}