#pragma once

namespace lib_collision_a {
struct Node {
    int value;
    virtual ~Node();
};
struct Holder {
    Node node;
};
} // namespace lib_collision_a

namespace lib_collision_b {
struct Node {
    double value;
    int marker;
    virtual ~Node();
};
struct Holder {
    Node node;
};
} // namespace lib_collision_b
