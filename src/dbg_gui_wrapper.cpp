#include "dbg_gui_wrapper.h"
#include "dbg_gui.h"
#include <memory>

static std::unique_ptr<DbgGui> gui;

void addScalarToGui(ValueSource src, const char* group, const char* name) {
    assert(gui);
    gui->addScalar(src, group, name);
}

void addVectorToGui(ValueSource x, ValueSource y, const char* group, const char* name) {
    assert(gui);
    gui->addVector(x, y, group, name);
}

void DbgGui_addScalar_u8(uint8_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_u16(uint16_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}
void DbgGui_addScalar_u32(uint32_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_u64(uint64_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_i8(int8_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_i16(int16_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_i32(int32_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_i64(int64_t* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_f32(float* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addScalar_f64(double* src, const char* group, const char* name) {
    addScalarToGui(src, group, name);
}

void DbgGui_addVector_f32(float* x, float* y, const char* group, const char* name) {
    addVectorToGui(x, y, group, name);
}

void DbgGui_addVector_f64(double* x, double* y, const char* group, const char* name) {
    addVectorToGui(x, y, group, name);
}

void DbgGui_create(double sampling_time) {
    gui = std::make_unique<DbgGui>(sampling_time);
}

void DbgGui_startUpdateLoop(void) {
    assert(gui);
    gui->startUpdateLoop();
}

void DbgGui_sample(void) {
    assert(gui);
    gui->sample();
}

int DbgGui_isClosed(void) {
    if (gui) {
        return gui->isClosed();
    }
    return 1;

#ifdef __cplusplus
}
void DbgGui_close(void) {
    gui = nullptr;
}
#endif

DbgGuiWrapper::DbgGuiWrapper(double sampling_time) {
    gui = std::make_unique<DbgGui>(sampling_time);
}

DbgGuiWrapper::~DbgGuiWrapper() {
    gui = nullptr;
}

void DbgGuiWrapper::startUpdateLoop() {
    gui->startUpdateLoop();
}

void DbgGuiWrapper::sample() {
    gui->sample();
}

bool DbgGuiWrapper::isClosed() {
    return gui->isClosed();
}

void DbgGuiWrapper::close() {
    gui = nullptr;
}

void DbgGuiWrapper::addScalar(ValueSource const& src, std::string const& group, std::string const& name) {
    gui->addScalar(src, group, name);
}

void DbgGuiWrapper::addVector(ValueSource const& x, ValueSource const& y, std::string const& group, std::string const& name) {
    gui->addVector(x, y, group, name);
}
