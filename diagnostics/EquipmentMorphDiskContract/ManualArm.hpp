#pragma once
namespace facecontinuous {
class ManualArmEdge {
    bool held=false;
public:
    bool Pressed(bool down,bool foreground) noexcept {
        const bool edge=down&&!held;held=down;return edge&&foreground;
    }
};
}
