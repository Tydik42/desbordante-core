# Python Bindings Architecture PoC

## Цель

Proof of Concept новой архитектуры Python bindings с:
- **nanobind** вместо pybind11 (быстрее компиляция, лучше метапрограммирование)
- **Локальная регистрация типов** вместо глобальной мапы `kConverters`
- **cista** для автоматической сериализации (pickle)

## Структура

```
python_bindings/
├── infrastructure/
│   ├── option_converter.h    # Конвертер Python → boost::any
│   └── serialization.h       # Generic pickle (TODO: cista)
│
└── ar/
    └── bind_ar_nanobind.h    # PoC: nanobind версия AR модуля
```

## Ключевые изменения

### 1. OptionConverter вместо kConverters

**Было (py_to_any.cpp):**
```cpp
// Глобальная мапа → конфликты при мерже
static std::unordered_map<...> const kConverters{
    kNormalConvPair<int>,
    kNormalConvPair<double>,
    // ... 30+ типов
};
```

**Стало (option_converter.h):**
```cpp
// Каждый модуль регистрирует свои типы локально
void RegisterArOptionTypes() {
    auto& converter = OptionConverter::Instance();
    converter.Register<double>([](void* handle) {
        return boost::any(nb::cast<double>(nb::handle(handle)));
    });
}
```

### 2. nanobind API

**Было (pybind11):**
```cpp
py::class_<ARStrings>(ar_module, "ARStrings")
    .def_readonly("left", &ARStrings::left)
    .def(py::pickle(...)); // 20+ строк ручного pickle
```

**Стало (nanobind):**
```cpp
nb::class_<ARStrings>(ar_module, "ARStrings")
    .def_ro("left", &ARStrings::left);
    // TODO: автоматический pickle через cista
```

### 3. Упрощённый set_option

```cpp
.def("set_option", [](Apriori& algo, std::string const& name, nb::handle value) {
    auto type = algo.GetTypeIndex(name);  // Узнаём тип из core
    boost::any any_value = OptionConverter::Instance().Convert(type, value.ptr());
    algo.SetOption(name, any_value);
})
```

## Как собрать PoC

```bash
# 1. Включить nanobind
cmake -B build -DDESBORDANTE_BINDINGS=BUILD -DDESBORDANTE_USE_NANOBIND=ON

# 2. Собрать
cmake --build build

# 3. Тестировать
cd build/target
python3 -c "import desbordante; print(desbordante.ar.algorithms.Apriori())"
```

## TODO

- [ ] Добавить cista в CMake
- [ ] Реализовать `serialization.h` с cista
- [ ] Добавить автоматический pickle для ARStrings
- [ ] Протестировать с реальными данными
- [ ] Измерить время компиляции (pybind11 vs nanobind)
- [ ] Создать ModuleBuilder DSL

## Метрики успеха

| Метрика | pybind11 | nanobind (цель) |
|---------|----------|-----------------|
| Строк кода для AR модуля | ~100 | ~50 |
| Время компиляции | Базовая линия | -30% |
| Конфликты при добавлении типа | Да (kConverters) | Нет |

## Следующие шаги

1. Завершить PoC на AR модуле
2. Добавить ModuleBuilder для декларативного биндинга
3. Мигрировать FD модуль (более сложный)
4. Постепенно мигрировать остальные модули
