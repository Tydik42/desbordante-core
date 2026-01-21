# Архитектура Python Bindings: Проектный Документ

## Резюме

Текущая архитектура Python bindings страдает от плохой модульности из-за централизованного реестра конвертации типов. Документ предлагает миграцию с pybind11 на nanobind с децентрализованной регистрацией типов, что обеспечит независимую разработку модулей и устранит конфликты при слиянии.

## Анализ Текущей Архитектуры

### Система Конвертации Типов

Существующая реализация использует глобальную статическую map в `py_to_any.cpp`:

```cpp
std::unordered_map<std::type_index, ConvFunc> const kConverters{
    kNormalConvPair<bool>,
    kNormalConvPair<double>,
    kEnumConvPair<algos::InputFormat>,        // AR модуль
    kEnumConvPair<algos::metric::Metric>,     // Metric модуль
    kEnumConvPair<algos::cfd::Substrategy>,   // CFD модуль
    // 30+ записей из всех модулей
};
```

Поток: `Python kwargs → PyToAny(type_index, py::handle) → kConverters[type_index] → boost::any → Algorithm::SetOption`

### Критические Проблемы

**Связанность**: Все модули зависят от одной единицы трансляции. Добавление Primitive-специфичного типа требует модификации файла, который включает заголовки CFD, MD, HyMD.

**Масштабируемость**: Линейный рост зависимостей компиляции.

**Типобезопасность**: Нет compile-time защиты от использования, например, AR модулем CFD-специфичных типов. Возможны коллизии имен между константами `config::names`.

**Поддержка**: Ручная реализация pickle требует 20+ строк на класс результата. Легко забыть обновить при добавлении полей.

### Сериализация

Текущий подход использует ручные `__getstate__/__setstate__`:

```cpp
.def(py::pickle(
    [](ARStrings const& ars) {
        std::vector<std::string> left_vec(ars.left.begin(), ars.left.end());
        std::vector<std::string> right_vec(ars.right.begin(), ars.right.end());
        return py::make_tuple(std::move(left_vec), std::move(right_vec),
                              ars.confidence, ars.support);
    },
    [](py::tuple t) {
        if (t.size() != 4) throw std::runtime_error("Invalid state!");
        auto left_vec = t[0].cast<std::vector<std::string>>();
        // ... ручная реконструкция
    }
));
```

Проблема: Хрупкость. Добавление поля в `ARStrings` требует обновления кода pickle. Ошибки только в runtime.

## Предлагаемая Архитектура

### Принципы Проектирования

1. **Независимость модулей**: Каждый модуль владеет своими конвертерами типов
2. **Локальность**: Логика конвертации типов живет с определением типа
3. **Нулевые изменения core**: Классы Algorithm/Option остаются неизменными
4. **Compile-time безопасность**: Несоответствия типов обнаруживаются при компиляции
5. **Автоматизация**: Минимизация boilerplate через метапрограммирование

### Редизайн Конвертации Типов

#### Абстракция Интерфейса

```cpp
// infrastructure/option_converter.h
struct IConverter {
    using ConvertFunc = std::function<boost::any(std::string_view, void*)>;
    virtual boost::any Convert(std::string_view option_name, 
                              std::type_index type, 
                              void* py_handle) const = 0;
};
```

Ключевая идея: Абстрактный интерфейс конвертации позволяет dependency injection. Каждый модуль предоставляет свою реализацию.

#### Локальная Реализация Модуля

```cpp
class ModuleConverter : public IConverter {
    std::unordered_map<std::type_index, ConvertFunc> converters_;
public:
    void Register(std::type_index type, ConvertFunc func);
    boost::any Convert(std::string_view, std::type_index, void*) const override;
};
```

Критическое отличие: Map является членом экземпляра, а не глобальной статической. Каждый модуль создает свой экземпляр конвертера.

#### Стратегия Регистрации Типов

Использование инфраструктуры `CommonOption<T>`. Расширение метаданными конвертера:

```cpp
// core/config/common_option.h
template <typename T, typename ConverterTag = void>
class CommonOptionEx : public CommonOption<T> {
public:
    using ConverterType = ConverterTag;
    static constexpr bool has_custom_converter = !std::is_void_v<ConverterTag>;
};
```

Конвертер живет с определением опции:

```cpp
// config/tabular_data/input_table/option.h
struct InputTableConverter {
    static boost::any FromPython(nb::handle h) {
        if (IsDataFrame(h)) return CreateDataFrameReader(h);
        if (nb::isinstance<nb::str>(h)) return CreateCSVParser(h);
        if (nb::isinstance<nb::tuple>(h)) {
            auto [path, sep, header] = nb::cast<std::tuple<std::string, char, bool>>(h);
            return std::make_shared<CSVParser>(path, sep, header);
        }
        throw std::runtime_error("Invalid table type");
    }
};

extern CommonOptionEx<InputTable, InputTableConverter> const kTableOpt;
```

#### Автоматическая Регистрация

```cpp
// infrastructure/option_binder.h
template<typename T, typename Converter>
void RegisterOption(ModuleConverter& conv, CommonOptionEx<T, Converter> const& opt) {
    conv.Register<T>([](std::string_view name, void* h) -> boost::any {
        return Converter::FromPython(nb::handle(static_cast<PyObject*>(h)));
    });
}

template<typename T>
void RegisterOption(ModuleConverter& conv, CommonOption<T> const& opt) {
    // Примитивные типы: делегируем nanobind
    conv.Register<T>([](std::string_view name, void* h) -> boost::any {
        return nb::cast<T>(nb::handle(static_cast<PyObject*>(h)));
    });
}
```

Использование в модуле:

```cpp
// ar/bind_ar_nanobind.h
ModuleConverter CreateArConverter() {
    ModuleConverter conv;
    RegisterBasicTypes(conv);  // bool, int, double, string
    RegisterOption(conv, config::kTableOpt);  // Кастомный конвертер
    RegisterOption(conv, config::kMinSupOpt); // Примитив, авто-обработка
    return conv;
}
```

### Редизайн Сериализации

Замена ручного pickle на библиотеку cista. Один метод `serialize()`:

```cpp
// core/model/ar_strings.h
struct ARStrings {
    std::list<std::string> left;
    std::list<std::string> right;
    double confidence;
    double support;
    
    template<typename Archive>
    void serialize(Archive& ar) {
        ar(left, right, confidence, support);
    }
};

// bindings
nb::class_<ARStrings>(m, "ARStrings")
    .def_ro("left", &ARStrings::left)
    .def(CistaPickle<ARStrings>());  // Универсальный pickle через cista
```

Преимущества: Добавление поля в структуру автоматически обновляет сериализацию. Compile-time ошибка если поле не сериализуемо.

### Интеграция с Core

Поток конфигурации алгоритма не изменен:

```
Python: algo.load_data(table=df, minsup=0.5)
    ↓
Bindings: ConfigureAlgo(algo, kwargs, ar_converter)
    ↓
    для каждой опции в kwargs:
        type_index = algo.GetTypeIndex(option_name)
        py_value = kwargs[option_name]
        cpp_value = ar_converter.Convert(option_name, type_index, py_value)
        algo.SetOption(option_name, cpp_value)
    ↓
Core: Algorithm::SetOption(boost::any)
    ↓
Core: Option<T>::Set(boost::any)
    ↓
Core: *value_ptr_ = boost::any_cast<T>(value)
```

Нулевые изменения в `Algorithm`, `Option<T>`, или реализациях алгоритмов.

## Сравнительный Анализ

### Зависимости Компиляции

Текущее: `py_to_any.cpp` включает заголовки всех модулей. Изменение AR enum требует перекомпиляции всех bindings.

Предлагаемое: `ar/bind_ar_nanobind.h` включает только AR заголовки. Независимые единицы компиляции.

### Рабочий Процесс Разработки

Текущее: Разработчик добавляющий новый AR тип должен:
1. Модифицировать `py_to_any.cpp` (общий файл)
2. Добавить конвертер в `kConverters` (риск конфликта слияния)
3. Вручную реализовать pickle (подвержено ошибкам)

Предлагаемое: Разработчик добавляющий новый AR тип:
1. Определить конвертер в `config/ar_option/option.h` (локальный файл)
2. Вызвать `RegisterOption(conv, kNewOpt)` (нет общего состояния)
3. Добавить метод `serialize()` (проверка compile-time)

### Типобезопасность

Текущее: Ничто не мешает AR модулю использовать `config::names::kCfdMinimumSupport`. Runtime ошибка при несоответствии типов.

Предлагаемое: `CommonOptionEx<T, Converter>` кодирует тип и конвертер вместе. Compile ошибка если конвертер отсутствует.

### Производительность

Runtime: Идентична. Обе используют lookup в `std::unordered_map<std::type_index, ConvertFunc>`.

Compile-time: Предлагаемая архитектура позволяет параллельную компиляцию независимых модулей. nanobind сообщает о 3-5x более быстрой компиляции чем pybind11.

## Стратегия Миграции

### Фаза 1: Инфраструктура (Текущая)

Реализация базовых абстракций без нарушения существующего кода:
- Интерфейс `IConverter`
- Реализация `ModuleConverter`
- Расширение `CommonOptionEx`
- Хелперы nanobind

### Фаза 2: Proof of Concept

Миграция одного модуля (AR) для валидации архитектуры:
- Создание `ar/bind_ar_nanobind.h`
- Реализация `CreateArConverter()`
- Тестирование с реальными данными
- Измерение времени компиляции

### Фаза 3: Параллельная Поддержка

Поддержка pybind11 и nanobind одновременно:
- Условная компиляция через `DESBORDANTE_USE_NANOBIND`
- Отдельные точки входа: `bindings.cpp` (pybind11), `bindings_nb.cpp` (nanobind)
- Постепенная миграция модулей

### Фаза 4: Полная Миграция

После валидации всех модулей с nanobind:
- Deprecation пути pybind11
- Удаление `py_to_any.cpp`
- Обновление документации

## Оценка Рисков

**Ограничения API nanobind**: Митигация: Валидация всех требуемых фич до миграции.

**Breaking changes в Python API**: Митигация: Сохранение идентичного интерфейса. Пользователи не должны заметить разницу.

**Регрессия производительности**: Митигация: Бенчмарк до/после. Runtime производительность должна быть идентична.

**Увеличение бремени поддержки**: Митигация: Лучшая модульность снижает долгосрочную поддержку. Краткосрочные затраты для долгосрочной выгоды.

## Открытые Вопросы

1. Должен ли `CommonOptionEx` жить в core или bindings? Текущее предложение: core, для единственного источника правды.

2. Как обрабатывать общие типы (например, `config::InputTable` используемый несколькими модулями)? Текущее предложение: хелпер `RegisterBasicTypes()`.

3. Обратная совместимость для pickled объектов? Текущее предложение: Версионирование формата pickle, поддержка миграции.

## Заключение

Предлагаемая архитектура решает фундаментальные проблемы масштабируемости через децентрализацию. Независимость модулей обеспечивает параллельную разработку. Улучшения типобезопасности обнаруживают ошибки на compile-time. Автоматизация снижает boilerplate и бремя поддержки.

Изменения core минимальны и обратно совместимы. Миграция может происходить инкрементально без нарушения разработки.

Рекомендация: Приступить к реализации Фазы 1 и proof of concept AR модуля.
