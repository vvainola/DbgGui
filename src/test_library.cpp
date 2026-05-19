// MIT License
//
// Copyright (c) 2025 vvainola
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

// Shared library with various global variables for testing symbol reading.
// Contains classes, structs, bitfields, enums, arrays, and nested types.

#include <cstdint>

// ---- Enums ----

enum Color {
    Red = 0,
    Green = 1,
    Blue = 2,
    Yellow = 3
};

enum class Direction : int32_t {
    North = 0,
    South = 1,
    East = 2,
    West = 3
};

// ---- Structs ----

struct Point2D {
    float x;
    float y;
};

struct Point3D {
    double x;
    double y;
    double z;
};

// ---- Bitfields ----

struct StatusFlags {
    uint32_t enabled    : 1;
    uint32_t mode       : 3;
    uint32_t priority   : 4;
    uint32_t error_code : 8;
    uint32_t reserved   : 16;
};

union StatusRegister {
    uint32_t raw;
    StatusFlags flags;
};

// ---- Nested struct ----

struct MotorController {
    double speed;
    double torque;
    int32_t direction;
    Point2D position;
    StatusRegister status;
};

// ---- Class with members ----

class PIDController {
  public:
    double kp;
    double ki;
    double kd;
    double setpoint;
    double integral;
    double previous_error;
    int32_t enabled;
};

// ---- Global variables with default values ----

// Primitive types
int32_t lib_int32 = 42;
uint32_t lib_uint32 = 100;
int64_t lib_int64 = -123456789;
uint64_t lib_uint64 = 987654321;
float lib_float = 3.14f;
double lib_double = 2.71828;
int16_t lib_int16 = -1234;
uint16_t lib_uint16 = 5678;
int8_t lib_int8 = -42;
uint8_t lib_uint8 = 200;

// Enums
Color lib_color = Green;
Direction lib_direction = Direction::East;

// Structs
Point2D lib_point2d = {1.5f, 2.5f};
Point3D lib_point3d = {10.0, 20.0, 30.0};

// Bitfield
StatusRegister lib_status = {0};

// Nested struct
MotorController lib_motor = {
  .speed = 1500.0,
  .torque = 12.5,
  .direction = 1,
  .position = {100.0f, 200.0f},
  .status = {0},
};

// Class instance
PIDController lib_pid = {};

// Arrays
double lib_array[4] = {1.0, 2.0, 3.0, 4.0};
int32_t lib_matrix[3][3] = {
  {1, 2, 3},
  {4, 5, 6},
  {7, 8, 9}};
Point2D lib_points[3] = {
  {0.0f, 0.0f},
  {1.0f, 1.0f},
  {2.0f, 2.0f}};

// Pointers
double* lib_double_ptr = &lib_double;
double* lib_null_ptr = nullptr;

// Init function to set up bitfield values and class members
// that are easier to set in code
struct LibInitializer {
    LibInitializer() {
        lib_status.flags.enabled = 1;
        lib_status.flags.mode = 5;
        lib_status.flags.priority = 10;
        lib_status.flags.error_code = 0xAB;

        lib_motor.status.flags.enabled = 1;
        lib_motor.status.flags.mode = 3;

        lib_pid.kp = 1.0;
        lib_pid.ki = 0.1;
        lib_pid.kd = 0.01;
        lib_pid.setpoint = 100.0;
        lib_pid.integral = 0.0;
        lib_pid.previous_error = 0.0;
        lib_pid.enabled = 1;
    }
};

static LibInitializer lib_initializer;
