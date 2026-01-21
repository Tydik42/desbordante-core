#pragma once

#include <functional>
#include <string_view>
#include <typeindex>
#include <unordered_map>

#include <boost/any.hpp>

namespace python_bindings {

// Abstract converter interface
struct IConverter {
    using ConvertFunc = std::function<boost::any(std::string_view, void*)>;
    
    virtual boost::any Convert(std::string_view option_name, std::type_index type, 
                              void* py_handle) const = 0;
    virtual ~IConverter() = default;
};

// Module-local converter
class ModuleConverter : public IConverter {
    std::unordered_map<std::type_index, ConvertFunc> converters_;
    
public:
    void Register(std::type_index type, ConvertFunc func) {
        converters_[type] = std::move(func);
    }
    
    template<typename T>
    void Register(ConvertFunc func) {
        Register(std::type_index(typeid(T)), std::move(func));
    }
    
    boost::any Convert(std::string_view option_name, std::type_index type, 
                      void* py_handle) const override {
        auto it = converters_.find(type);
        if (it == converters_.end()) {
            throw std::runtime_error(std::string("Type not registered for option \"") + 
                                   std::string(option_name) + "\"");
        }
        return it->second(option_name, py_handle);
    }
};

} // namespace python_bindings
