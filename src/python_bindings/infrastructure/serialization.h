#pragma once

#include <cstdint>
#include <vector>

namespace python_bindings {

// Generic pickle support using cista serialization
// To be implemented with cista once we add it
template<typename T>
class PickleSupport {
public:
    static std::vector<uint8_t> Serialize(T const& obj) {
        // TODO: implement with cista
        // For now, placeholder
        return {};
    }
    
    static T Deserialize(uint8_t const* data, size_t size) {
        // TODO: implement with cista
        // For now, placeholder
        return T{};
    }
};

} // namespace python_bindings
