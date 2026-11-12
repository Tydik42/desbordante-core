# Графовые ключи из Python

Валидатор проверяет один ключ и возвращает нарушающие его пары сущностей. Дедупликатор применяет набор ключей до неподвижной точки и возвращает классы эквивалентности. Он не изменяет вершины и рёбра исходного графа. Обе реализации используют один тип `GraphKey`.

## Запуск примеров

Из корня репозитория после сборки Python-модуля:

```bash
export PYTHONPATH="$PWD/build/src/python_bindings${PYTHONPATH:+:$PYTHONPATH}"
python examples/basic/verifying_gk.py
python examples/basic/deduplicating_gk.py
```

Замените `build` своим каталогом сборки. Версия Python должна соответствовать собранному модулю. Скрипты требуют только стандартную библиотеку и `desbordante`.

## Граф и ключ

Общие типы доступны в `desbordante.gk_verification` и `desbordante.gk_deduplication`. Граф можно задать списком `Triple(Node(...), predicate, Node(...))` или CSV:

```csv
subject_id,subject_type,predicate,object_kind,object_id_or_value,object_type
a,Person,worksAt,entity,c,Company
b,Person,worksAt,entity,c,Company
a,Person,name,value,Alice,-
```

ID сущностей глобально уникальны; тип одной сущности во всех строках одинаков. Поля непустые. `-` обозначает отсутствие типа значения и не используется как имя типа. Для кавычек, запятых и переводов строк действуют обычные правила CSV.

```python
import desbordante

gk = desbordante.gk_verification
x = gk.Variable(gk.VarKind.ENTITY, "x")
y = gk.Variable(gk.VarKind.ENTITY, "y")
key = gk.GraphKey([gk.PatternTriple(x, "worksAt", y)],
                  {x: "Person", y: "Company"}, x)
graph = gk.CompactGraph.from_csv("graph.csv")
```

Этот ключ нарушается, если разные люди работают в общей компании. `ENTITY` учитывает совпадение образов переменной, `WILDCARD` требует подходящего соседа без сравнения его образов, `VALUE` сравнивает значения, `CONSTANT` фиксирует значение из имени переменной. Словарь типов содержит только `ENTITY` и `WILDCARD`; корень — `ENTITY`, шаблон связен в неориентированном смысле.

Ключ предоставляет `root`, `pattern`, `types`, `to_string()`, сравнение, хеширование и `pickle`. Порядок троек не влияет на равенство ключей. Парсера ключей из файла нет; `pickle` используйте только с доверенными данными.

## Валидация и дедупликация

```python
validator = gk.algorithms.CompactGkValidator()
validator.load_data(gk_graph=graph, gk_key=key)
validator.execute()
print(validator.holds, validator.get_violations())

dedup = desbordante.gk_deduplication.algorithms.CompactGkDeduplicator()
dedup.load_data(gk_graph=graph, gk_keys=[key])
dedup.execute()
print(dedup.get_clusters(), dedup.cluster_count, dedup.merge_count)
```

Вместо `from_csv` есть `CompactGraph.from_triples(iterable)`. Граф можно передавать нескольким алгоритмам. Для загрузки внутри алгоритма передайте `gk_graph_path="graph.csv"` вместо `gk_graph`. Строковые `GkValidator` и `GkDeduplicator` принимают список `Triple` или тот же CSV. `algorithms.Default` выбирает строковую реализацию.

Результаты доступны после успешного `execute()`. Валидатор возвращает все найденные нарушающие пары. Дедупликатор возвращает и одноэлементные классы; `merge_count` — число объединений классов. Пустой набор ключей оставляет все сущности отдельными. Получение списков результатов создаёт копии в Python.

Повторный `execute()` начинает вычисление заново, без прежних объединений. `load_data()` без аргументов перезагружает тот же источник и сбрасывает результат. Для другого графа или ключа создайте новый экземпляр алгоритма.

## Оптимизации

Все стадии по умолчанию выключены. Спаривание требует локализации, редукция — спаривания. Передавайте нужное сочетание при каждом вызове; допустимы также запуск без опций, только локализация и локализация со спариванием:

```python
validator.execute(use_localization=True,
                  use_pairing_filter=True,
                  use_subgraph_reduction=True)
```

У дедупликатора те же опции. Их имена, описания и текущие значения доступны через `get_possible_options()`, `get_description(name)`, `get_opts()`.

Самостоятельные операции доступны для компактных графов в обоих модулях:

```python
ops = gk.optimizations
first = ops.localize(graph, "a", 1)
second = ops.localize(graph, "b", 1)
pairing = ops.compute_pairing(key, first, second)
print(pairing.can_pair("a", "b"), pairing.pair_count)
for variable in pairing.variables:
    print(variable, pairing.candidate_count(variable))
reduced_first, reduced_second = ops.reduce_subgraphs(pairing)
```

`localize` строит индуцированную окрестность неориентированного радиуса. Радиус выбирает вызывающий код: слишком маленький может потерять вложения. Оба входа спаривания должны происходить от одного исходного `CompactGraph`, а не быть загружены независимо. `can_pair=True` — только необходимое условие, не результат точного поиска. Редукция относится к данному ключу и входным графам, не заменяет весь исходный граф.

Результаты сохраняют необходимые данные после удаления исходных Python-переменных. Размеры графа: `vertex_count`, `entity_count`, `edge_count`, `input_triple_count`; повторяющиеся тройки учитываются только в последнем счётчике. `get_vertices()` и `pairing.get_candidates(variable)` копируют данные в Python. Самостоятельные операции материализуют подграфы: для полного расчёта на большом графе используйте опции алгоритма, а не Python-цикл по всем парам.

## Логи фаз

При сборке с `-LTRACE` примеры могут записывать фазы в файл:

```bash
python examples/basic/verifying_gk.py --trace-log validation.log
python examples/basic/deduplicating_gk.py --trace-log deduplication.log
```

В своём скрипте после импорта `desbordante` настройте `logging.basicConfig(filename="gk.log", level=logging.TRACE)` и `logging.getLogger("desbordante").setLevel(logging.TRACE)`. Уровень `TRACE` равен 5, ниже `DEBUG`. Фазы вложены друг в друга, поэтому их времена нельзя просто складывать. Сравнивайте производительность отдельными замерами без подробных логов, не по времени демонстрационных скриптов.
