# Lua в GGEngine

Справочник по текущим привязкам движка. В проекте используется Lua 5.4.8 и sol2. Описаны только методы и свойства, действительно доступные из Lua.

## 1. Подключение скрипта

1. В Content Browser создайте `Lua script`, например `res/Scripts/Player.lua`.
2. Выберите объект и нужный узел его дерева трансформации. Добавьте компонент `Lua Script`.
3. В поле `Script path` укажите путь к файлу, например `res/Scripts/Player.lua`.
4. Запустите игру. В режиме редактирования обработчики скрипта не выполняются.

Пути — строки UTF-8 с разделителем `/`, без `res://`. Загрузка проходит через `ResourceManager.ResolvePath`: если менеджеру задан каталог проекта, относительный путь разрешается от него; иначе — от рабочей директории процесса. Абсолютные пути также поддерживаются. `res` — папка ресурсов, поэтому префикс `res/` в примере нужен.

Файл должен вернуть таблицу. Обработчики `on_init` и `on_update` необязательны:

```lua
local M = {}

function M.on_init(self)
    -- Состояние хранится отдельно для каждого компонента скрипта.
    self.speed = 2.0
    self.elapsed = 0.0
end

function M.on_update(self, dt)
    -- dt — время кадра в секундах; скорость задаём в единицах в секунду.
    self.elapsed = self.elapsed + dt
    self.transform:move(self.speed * dt, 0, 0)
end

return M
```

`on_init(self)` вызывается при инициализации компонента; `on_update(self, dt)` — при его покадровом обновлении. При повторной инициализации создаётся новая таблица `self`. Она не сохраняется в сцену. Автоматических обработчиков `on_destroy`, `on_fixed_update` и столкновений пока нет.

Ошибка загрузки, `on_init` или `on_update` выводится с префиксом `[Lua]` и отключает дальнейшее обновление данного скрипта до повторной инициализации.

### Что находится в self

| Поле | Значение |
| --- | --- |
| `self.object` | Объект `GameObject`, которому принадлежит компонент. |
| `self.level` | Уровень `Level`, которому принадлежит объект. |
| `self.transform` | Узел, к которому привязан компонент скрипта. Если узел не выбран, используется корень объекта. |
| `self.input` | Игровой ввод `GameInput`; предоставляется только в сборке с `GG_GAME`. |
| Собственные поля | Любое состояние скрипта: таймеры, параметры, найденные компоненты. |

Методы модуля доступны через таблицу `self`. Для собственного метода можно написать `function M:jump()` и вызвать `self:jump()`.

Все скрипты одного `ScriptServer` используют общее Lua-состояние и глобальные переменные. Объявляйте вспомогательные переменные через `local`, а состояние экземпляра храните в `self`.

### Прямые вызовы скрипта другого объекта

Свойство `LuaScript.instance` возвращает runtime-таблицу `self` конкретного компонента. Само свойство доступно только для чтения; поля возвращённой таблицы можно читать и изменять.

```lua
-- target — уже полученный живой GameObject.
local component = target:get_component(ComponentType.LuaScript)
local other = component and component.instance
if other and other.is_valid and type(other.take_damage) == "function" then
    other:take_damage(10)
end
```

В скрипте целевого объекта метод определяется как `function M.take_damage(self, damage)` или `function M:take_damage(damage)`. Двоеточие в вызове передаёт таблицу целевого объекта как `self`. `require` для этого не нужен.

До успешного завершения `on_init`, в режиме редактирования и после ошибки компонента `instance` возвращает `nil`. Порядок инициализации имеет значение: во время своего `on_init` другой объект ещё может быть не готов. Ошибка прямого вызова распространяется в вызывающий Lua-код; для обработки используйте `pcall(other.take_damage, other, 10)`. Она сама по себе не отключает целевой компонент.

При удалении или повторной инициализации компонента старая таблица получает `is_valid = false`, поля `object`, `transform`, `level`, `input` очищаются, а метатаблица с методами модуля удаляется. Пользовательские поля сохраняются. `is_valid` — служебный флаг времени жизни таблицы, не изменяйте его. Уже сохранённая таблица не заменяется новым экземпляром автоматически.

Это не общая защита C++-ссылок: отдельно сохранённые объекты, компоненты, трансформации, функции и ссылки в пользовательских полях не инвалидируются. Не используйте их после удаления владельца. Сам `component` также является заимствованной ссылкой. Обработчики столкновений этим API не добавляются.

### Точка и двоеточие

```lua
-- Фабрики типов вызываются через точку.
local offset = Vector3.new(1, 0, 0)
local rotation = Quaternion.identity()

-- Методы экземпляров — через двоеточие, свойства — через точку.
self.transform:move(offset)
self.transform.rotation = rotation
```

`value:method(arg)` эквивалентно `value.method(value, arg)`.

## 2. GameObject и поиск компонентов

| API | Описание |
| --- | --- |
| `object.transform` | Корневой `Transform`; само свойство только для чтения, свойства узла изменяемы. |
| `object:get_component(type)` | Первый подходящий компонент среди компонентов всего объекта; `nil`, если он не найден или имя неизвестно. |

Для поиска используйте константы `ComponentType`:

| Константа | Lua-тип |
| --- | --- |
| `ComponentType.Mesh` | `Mesh` |
| `ComponentType.GameCamera` | `GameCamera` |
| `ComponentType.DirectionalLight` | `DirectionalLight` |
| `ComponentType.PointLight` | `PointLight` |
| `ComponentType.LuaScript` | `LuaScript` |
| `ComponentType.BoxCollider` | `BoxCollider` |
| `ComponentType.DynamicRigidBody` | `DynamicRigidBody` |
| `ComponentType.StaticRigidBody` | `StaticRigidBody` |
| `ComponentType.EditorCamera` | `EditorCamera`, только при `GG_EDITOR` |
| `ComponentType.CameraMovement` | `CameraMovement`, только при `GG_EDITOR` |

Обычное изменение констант и замена их метатаблицы запрещены. Старые строковые имена, например `"PointLight"`, также принимаются, но константы удобнее для написания скриптов.

```lua
local light = self.object:get_component(ComponentType.PointLight)
if light then
    -- Поиск может вернуть nil: проверяем его перед обращением к свойствам.
    light.intensity = 3.0
    light.color = Color.new(1.0, 0.7, 0.3)
end
```

Поиск не ограничивается узлом `self.transform`. Если одинаковых компонентов несколько, выбрать конкретный по имени или получить список через Lua пока нельзя. `Transform:get_component` отсутствует.

Объекты, узлы и компоненты принадлежат C++. Lua получает заимствованные ссылки. После запроса удаления больше не используйте ссылки на объект, его узлы и компоненты: при следующей обработке очереди уровня они станут недействительными.

### Создание, дублирование и удаление объектов

| API | Описание |
| --- | --- |
| `self.level:create_object(path)` | Загружает объект из файла `.object.json`; возвращает `object, error`. При ошибке объект равен `nil`, при успехе ошибка — пустая строка. Пути разрешаются так же, как пути Lua-скриптов. |
| `self.level:duplicate_object(object)` | Создаёт независимую копию объекта этого уровня; возвращает `copy, error`. Объект, ожидающий удаления, копировать нельзя. |
| `self.level:destroy_object(object)` | Запрашивает отложенное удаление объекта этого уровня. Возвращает `true`, если запрос принят; для `nil`, чужого объекта или повторного запроса — `false`. |

Новые объекты сразу принадлежат уровню. Инициализация компонентов выполняется при обработке очереди уровня; до неё можно настроить трансформацию. Копируются дерево трансформации и сериализуемые настройки компонентов. Таблицы `self`, состояние выполнения Lua и физического тела не копируются; скрипты копии получают собственный `on_init`.

```lua
local object, err = self.level:create_object("res/Objects/Enemy.object.json")
if not object then
    print(err)
    return
end
object.transform.position = Vector3.new(0, 1, 0)

local copy, copyError = self.level:duplicate_object(object)
if copy then
    copy.transform.position = Vector3.new(3, 1, 0)
else
    print(copyError)
end

self.level:destroy_object(object)
object = nil
```

Для удаления самого себя используйте `self.level:destroy_object(self.object)` и завершите обработчик через `return`. Память освобождается отложенно, поэтому выполняющийся обработчик не уничтожается посреди вызова. Автоматической проверки сохранённых ссылок после фактического удаления нет.

## 3. Transform — узел дерева трансформации

`self.transform` может быть веткой, а `self.object.transform` всегда обозначает корень объекта.

### Свойства

| Свойство | Тип | Доступ | Значение |
| --- | --- | --- | --- |
| `position` | `Vector3` | Чтение/запись | Локальная позиция. |
| `rotation` | `Quaternion` | Чтение/запись | Локальное вращение. |
| `scale` | `Vector3` | Чтение/запись | Локальный масштаб. |
| `inherit_position` | `boolean` | Чтение/запись | Наследование позиции родителя. |
| `inherit_rotation` | `boolean` | Чтение/запись | Наследование вращения родителя. |
| `inherit_scale` | `boolean` | Чтение/запись | Наследование масштаба родителя. |
| `world_position` | `Vector3` | Чтение | Мировая позиция. |
| `world_rotation` | `Quaternion` | Чтение | Мировое вращение. |
| `world_scale` | `Vector3` | Чтение | Мировой масштаб. |
| `parent` | `Transform` или `nil` | Чтение | Родительский узел. |
| `child_count` | Целое число | Чтение | Количество непосредственных дочерних узлов. |

Векторы и кватернионы свойств возвращаются копиями. Записывайте изменённое значение обратно:

```lua
local position = self.transform.position
position.y = 2.0
self.transform.position = position

-- Короткая запись для полной замены позиции.
self.transform.position = Vector3.new(0, 2, 0)
```

Запись `self.transform.position.y = 2` меняет только временную копию и не перемещает узел. Это же правило относится к `rotation`, `scale` и цвету света.

Мировые свойства вычисляются разложением мировой матрицы. Если разложение невозможно, возвращаются нейтральные значения: нулевая позиция, единичное вращение и масштаб `(1, 1, 1)`.

### Методы

| Вызов | Действие |
| --- | --- |
| `t:move(offset)` | Прибавить `Vector3` к локальной позиции. |
| `t:move(x, y, z)` | То же, отдельными числами. |
| `t:rotate(axis, radians)` | Добавить вращение вокруг оси `Vector3`; ось нормализуется. Нулевая ось игнорируется. |
| `t:resize(x, y, z)` | Умножить текущий масштаб покомпонентно. |
| `t:look_at(target)` | Вычислить вращение через текущую реализацию `CreateLookAt`, с осью вверх `(0, 1, 0)`. |
| `t:look_at(target, up)` | То же с явно заданным вектором вверх. |
| `t:get_child(index)` | Получить непосредственного ребёнка по индексу; вне диапазона возвращает `nil`. |

`move` не поворачивает смещение по ориентации узла: это прибавление к `position`. Для движения вдоль повёрнутого направления используйте `rotation:rotate(vector)`.

В `look_at` цель должна быть в той же системе координат, что и локальная `position`. Текущая реализация напрямую преобразует матрицу вида в кватернион; не следует считать её универсальным мировым наведением объекта. Совпадение цели с позицией и нулевой `up` игнорируются; избегайте также направления, параллельного `up`.

```lua
-- В отличие от обычных Lua-массивов, индексы узлов начинаются с 0.
for i = 0, self.transform.child_count - 1 do
    local child = self.transform:get_child(i)
    print(i, child.position)
end
```

Создание, удаление, переименование, переподчинение и поиск узлов по имени из Lua пока недоступны.

## 4. Vector3

### Создание и поля

| Вызов | Результат |
| --- | --- |
| `Vector3.new()` | Нулевой вектор. |
| `Vector3.new(x, y, z)` | Вектор с указанными компонентами. |
| `Vector3.zero()` | `(0, 0, 0)` |
| `Vector3.one()` | `(1, 1, 1)` |
| `Vector3.unit_x()` | `(1, 0, 0)` |
| `Vector3.unit_y()` | `(0, 1, 0)` |
| `Vector3.unit_z()` | `(0, 0, 1)` |

Поля `v.x`, `v.y`, `v.z` доступны для чтения и записи. Фабрики возвращают новые значения.

### Методы и операции

| Вызов | Результат |
| --- | --- |
| `v:copy()` | Копия вектора. |
| `v:length()` | Длина. |
| `v:length_squared()` | Квадрат длины. |
| `v:normalized()` | Нормализованная копия; перед вызовом проверяйте, что длина ненулевая. |
| `a:dot(b)` | Скалярное произведение, число. |
| `a:cross(b)` | Векторное произведение. |
| `a:distance(b)` | Расстояние между векторами как точками. |
| `a:lerp(b, t)` | Линейная интерполяция; `t = 0` даёт `a`, `t = 1` — `b`. |

Поддерживаются `a + b`, `a - b`, `-a`, `a * b`, `a / b`, `a * number`, `number * a`, `a / number`, `a == b`, `tostring(a)`. Умножение и деление двух векторов покомпонентные. Результаты арифметики — новые значения. Равенство точное, без допуска. Деление числа на вектор не привязано.

```lua
local direction = Vector3.new(1, 0, 1)
if direction:length_squared() > 0 then
    -- Нормализация сохраняет одинаковую скорость движения по диагонали.
    direction = direction:normalized()
    self.transform:move(direction * self.speed * dt)
end
```

## 5. Quaternion

Углы задаются в радианах. Для перевода градусов используйте `math.rad(degrees)`.

| Вызов | Результат |
| --- | --- |
| `Quaternion.new()` | Единичный кватернион `(0, 0, 0, 1)`. |
| `Quaternion.new(x, y, z, w)` | Кватернион из четырёх компонент. |
| `Quaternion.identity()` | Единичное вращение. |
| `Quaternion.from_axis_angle(axis, radians)` | Вращение вокруг оси; ось нормализуется, для нулевой оси возвращается единичное вращение. |
| `Quaternion.from_euler(yaw, pitch, roll)` | Вращение из углов вокруг Y, X, Z соответственно, в порядке параметров yaw/pitch/roll. |
| `q:copy()` | Копия. |
| `q:normalized()` | Нормализованная копия. |
| `q:conjugate()` | Сопряжённый кватернион. |
| `q:rotate(v)` | Повернуть `Vector3` и вернуть новый вектор. |
| `a:slerp(b, t)` | Сферическая интерполяция между вращениями. |

Поля `x`, `y`, `z`, `w` доступны для чтения и записи. Поддерживаются `a * b`, `a == b`, `tostring(q)`. В используемом SimpleMath произведение `a * b` применяет сначала вращение `a`, затем `b`. Для корректных вращений используйте нормализованные кватернионы.

```lua
-- Поворот на 90 градусов вокруг Y и преобразование направления.
local q = Quaternion.from_axis_angle(Vector3.unit_y(), math.rad(90))
self.transform.rotation = q
local direction = q:rotate(Vector3.unit_z())
```

## 6. Color

| Вызов | Результат |
| --- | --- |
| `Color.new()` | Чёрный цвет с альфой 1. |
| `Color.new(r, g, b)` | Цвет с альфой 1. |
| `Color.new(r, g, b, a)` | Цвет с явно заданной альфой. |
| `color:copy()` | Копия. |
| `tostring(color)` | Строковое представление. |

Поля `r`, `g`, `b`, `a` доступны для чтения и записи. Обычный диапазон компонент — от 0 до 1; привязка не ограничивает значения автоматически. Арифметические операции для `Color` не привязаны.

## 7. Освещение

`DirectionalLight` и `PointLight` наследуют свойства Lua-типа `Light`:

| Свойство | Тип | Доступ | Назначение |
| --- | --- | --- | --- |
| `color` | `Color` | Чтение/запись | RGB света; альфа игнорируется, при чтении равна 1. |
| `intensity` | Число | Чтение/запись | Интенсивность. |
| `enabled` | `boolean` | Чтение/запись | Включение света. |
| `range` | Число | Чтение/запись | Дальность, только для `PointLight`. |

Полный пример пульсирующего источника света:

```lua
local M = {}

function M.on_init(self)
    self.light = self.object:get_component(ComponentType.PointLight)
    self.elapsed = 0
end

function M.on_update(self, dt)
    if not self.light then return end

    -- Меняем интенсивность между 1 и 3 с периодом 2 секунды.
    self.elapsed = self.elapsed + dt
    self.light.intensity = 2 + math.sin(self.elapsed * math.pi)
end

return M
```

## 8. Физика

### DynamicRigidBody

| Метод | Действие |
| --- | --- |
| `body:add_force(Vector3)` | Добавить силу на ближайший физический шаг. |
| `body:add_impulse(Vector3)` | Добавить линейный импульс. |
| `body:add_torque(Vector3)` | Добавить момент силы на ближайший физический шаг. |
| `body:add_angular_impulse(Vector3)` | Добавить угловой импульс. |

Все векторы задаются в мировых координатах. Методы возвращают `boolean`: команда принята в очередь или отклонена. `false` возвращается при неправильных аргументах, выключенном теле, неконечных значениях или переполнении накопленной команды. `true` ещё не означает, что тело физически переместилось: например, без созданного физического тела команды будут сброшены на шаге.

Команды суммируются до ближайшего fixed-step, затем очищаются. Сила и момент действуют один шаг; импульсы применяются однократно. Если между физическими шагами вызвать `add_force` несколько раз, силы сложатся. Если в кадре несколько физических шагов, команда не повторяется автоматически на каждом из них. Поэтому вызов силы из `on_update` сам по себе не обеспечивает постоянную силу независимо от частоты кадров. Lua-обработчика fixed-step пока нет.

| Свойство | Тип | Доступ | Значение |
| --- | --- | --- | --- |
| `linear_velocity` | `Vector3` | Чтение | Линейная скорость. |
| `angular_velocity` | `Vector3` | Чтение | Угловая скорость. |
| `mass` | Число | Чтение | Масса тела. |
| `has_actor` | `boolean` | Чтение | Создано ли физическое тело. |
| `is_sleeping` | `boolean` | Чтение | Спит ли тело. |
| `enabled` | `boolean` | Чтение/запись | Включено ли тело. |

Скорости возвращаются копиями и отражают состояние после завершённого физического шага. Прямой setter скорости и массы из Lua отсутствует. Создание физического тела происходит при обработке физикой, поэтому в `on_init` `has_actor` может быть ещё `false`.

### StaticRigidBody и BoxCollider

| Тип | Свойство | Доступ | Значение |
| --- | --- | --- | --- |
| `StaticRigidBody` | `has_actor` | Чтение | Наличие физического тела. |
| `StaticRigidBody` | `enabled` | Чтение/запись | Включение тела. |
| `BoxCollider` | `size` | Чтение | Размер как копия `Vector3`. |
| `BoxCollider` | `enabled` | Чтение/запись | Включение коллайдера. |

У статического тела нет методов сил и импульсов. Изменение `enabled` учитывается физической системой на шаге. Для динамического тела настройте корневой узел и коллайдер в редакторе; Lua не создаёт их автоматически.

Пример одиночного толчка по пробелу (не проверяет нахождение на земле):

```lua
local M = {}

function M.on_init(self)
    self.body = self.object:get_component(ComponentType.DynamicRigidBody)
end

function M.on_update(self, dt)
    if not self.input or not self.input.active then return end
    if not self.body or not self.body.has_actor then return end

    -- Импульс по нажатию применяется один раз, умножать его на dt не нужно.
    if self.input:was_key_pressed(Keys.Space) then
        local accepted = self.body:add_impulse(Vector3.new(0, 5, 0))
        if not accepted then print("Импульс отклонён") end
    end
end

return M
```

## 9. Игровой ввод

`self.input`, тип `GameInput` и таблица `Keys` доступны при `GG_GAME`.

| Метод | Результат |
| --- | --- |
| `input:is_key_down(key)` | Клавиша или кнопка мыши удерживается. |
| `input:was_key_pressed(key)` | Нажатие произошло в текущем кадре. |
| `input:was_key_released(key)` | Отпускание произошло в текущем кадре. |

Все свойства ввода доступны только для чтения:

| Свойство | Значение |
| --- | --- |
| `active` | Ввод включён и игровое окно имеет фокус. |
| `mouse_x`, `mouse_y` | Позиция мыши из системы ввода. |
| `mouse_delta_x`, `mouse_delta_y` | Суммарное смещение мыши за кадр. |
| `wheel_delta` | Суммарное значение прокрутки за кадр. |

Чтение не потребляет событие: разные скрипты могут увидеть одно нажатие. Повтор клавиши при удержании не создаёт нового `was_key_pressed`. При потере фокуса или изменении активности состояние очищается. Дельты мыши и колеса сбрасываются в начале следующего кадра; это не скорости, умножать их на `dt` для получения смещения не нужно.

Доступные константы:

- Буквы: `Keys.A` … `Keys.Z`.
- Цифры: `Keys.D0` … `Keys.D9`.
- Мышь: `Keys.LeftButton`, `Keys.RightButton`, `Keys.MiddleButton`.
- Остальные: `Keys.Escape`, `Keys.Space`, `Keys.LeftShift`, `Keys.RightShift`, `Keys.Up`, `Keys.Down`, `Keys.Left`, `Keys.Right`.

Имена вроде `Keys.Enter`, `Keys.Control` и `Keys.F1` сейчас не зарегистрированы.

Полный пример движения по WASD для объекта без динамического физического управления:

```lua
local M = {}

function M.on_init(self)
    self.speed = 4.0
end

function M.on_update(self, dt)
    local input = self.input
    if not input or not input.active then return end

    -- Собираем направление в плоскости XZ относительно родителя узла.
    local direction = Vector3.zero()
    if input:is_key_down(Keys.W) then direction.z = direction.z + 1 end
    if input:is_key_down(Keys.S) then direction.z = direction.z - 1 end
    if input:is_key_down(Keys.D) then direction.x = direction.x + 1 end
    if input:is_key_down(Keys.A) then direction.x = direction.x - 1 end

    -- Убираем ускорение по диагонали и учитываем длительность кадра.
    if direction:length_squared() > 0 then
        self.transform:move(direction:normalized() * self.speed * dt)
    end
end

return M
```

## 10. Библиотеки Lua и отладка

Открыты базовая библиотека, `math`, `string`, `table`, `utf8`, `package`. Например, доступны `pairs`, `ipairs`, `type`, `tostring`, `math.sin`, `math.rad`, `string.format`, `table.insert`, `require`.

`io`, `os`, `debug`, `coroutine` не открываются. В `package` отключены загрузчики нативных библиотек и `loadlib`. Это перечень открытых библиотек, а не гарантия изоляции: базовая библиотека Lua также содержит функции загрузки кода.

### Подключение других Lua-скриптов

`require("res.Scripts.Utils")` ищет `res/Scripts/Utils.lua`, затем `res/Scripts/Utils/init.lua`. Поиск идёт от каталога проекта, заданного менеджеру ресурсов, а без него — от рабочей директории. Это не путь относительно вызывающего скрипта. Имя указывают без `.lua`; точки разделяют каталоги.

Например, содержимое `res/Scripts/Utils.lua`:

```lua
local Utils = {}

function Utils.double(value)
    return value * 2
end

return Utils
```

В другом скрипте:

```lua
local Utils = require("res.Scripts.Utils")
local M = {}

function M.on_init(self)
    self.speed = Utils.double(3)
end

return M
```

Модуль выполняется при первом подключении; результат кешируется в `package.loaded` по имени модуля и разделяется всеми скриптами данного `ScriptServer`. Используйте одно имя для каждого модуля. Состояние отдельных объектов по-прежнему храните в `self`, а не в общей таблице модуля. Без возвращаемого значения `require` обычно возвращает `true`.

Подключение само по себе не вызывает `on_init` и `on_update` и не создаёт компонент. Ошибка поиска или выполнения модуля становится ошибкой вызывающего скрипта; её можно обработать через `pcall(require, "res.Scripts.Utils")`. Избегайте циклических подключений. Автоматической перезагрузки модулей при изменении файла нет.

`print(...)` выводит аргументы через табуляцию с префиксом `[Lua]`; для математических типов используется их строковое представление:

```lua
print("Позиция:", self.transform.position)
print("Компонент найден:", self.object:get_component(ComponentType.Mesh) ~= nil)
```

При диагностике проверяйте:

1. Файл возвращает таблицу через `return M`.
2. Обработчики названы именно `on_init` и `on_update`.
3. Игра запущена: в режиме редактирования скрипты не обновляются.
4. Методы экземпляров вызываются через `:`, свойства читаются через `.`.
5. Результат `get_component` проверяется на `nil`.
6. Изменённые векторные свойства присваиваются целиком.
7. Углы заданы в радианах, а индексы дочерних узлов начинаются с нуля.
8. Для физики есть действующее тело и коллайдер; сообщение об ошибке ищется в выводе `[Lua]`.

## 11. Что пока недоступно

- Создание и удаление отдельных компонентов и узлов из Lua.
- Поиск других объектов, загрузка ресурсов и переключение сцен через Lua API.
- Поиск веток по именам и поиск компонента только на выбранном узле.
- Изменение мешей, материалов и параметров камеры.
- Настройка размера коллайдера, массы и скорости тела из Lua.
- Обработчики столкновений, fixed-step, завершения скрипта и автоматическая горячая перезагрузка.

`Mesh`, `GameCamera`, а также редакторские `EditorCamera` и `CameraMovement` зарегистрированы как типы без пользовательских методов и свойств. Их можно найти и проверить на наличие. `LuaScript` предоставляет свойство `instance` для доступа к runtime-таблице скрипта. Наличие метода в C++ не означает, что он доступен в Lua.

## 12. Источники API в проекте

- [ScriptBindingsMath.cpp](Engine/Scripting/ScriptBindingsMath.cpp) — `Vector3`, `Quaternion`, `Color`.
- [ScriptBindingsTransform.cpp](Engine/Scripting/ScriptBindingsTransform.cpp) — `Transform`.
- [ScriptBindingsComponents.cpp](Engine/Scripting/ScriptBindingsComponents.cpp) — объекты, компоненты, физика, ввод и константы.
- [LuaScriptComponent.cpp](Engine/Scripting/LuaScriptComponent.cpp) — загрузка файла, `self`, жизненный цикл и обработка ошибок.
- [ScriptServer.cpp](Engine/Scripting/ScriptServer.cpp) — библиотеки Lua и `print`.
- [TransformTree.cpp](Engine/Transform/TransformTree.cpp) — реализация операций трансформации.
- [PhysicsServer.cpp](Engine/Physics/PhysicsServer.cpp) — накопление команд и физический шаг.
- [GameInput.cpp](Engine/Platform/GameInput.cpp) — поведение игрового ввода.
- [ResourceManager.cpp](Engine/Resources/ResourceManager.cpp) — разрешение путей.

При изменении привязок обновляйте соответствующий раздел этого справочника.

## 13. Utility AI без UI

`UtilityAIComponent` выбирает одно действие по таблице полезности. Таблица и кривые —
ресурсы `ResourceManager`; значения параметров, Lua-состояния и таймеры принадлежат
конкретному компоненту NPC. В режиме редактора действия не запускаются.

Компонент в JSON объекта:

```json
{
  "type": "UtilityAIComponent",
  "table_path": "res/AI/guard.utilityai.json",
  "enabled": true
}
```

Пример `guard.utilityai.json` (все показанные поля обязательны):

```json
{
  "parameters": [
    { "name": "Health", "type": "float", "default": 100 },
    { "name": "Ammo", "type": "int", "default": 30 }
  ],
  "interval": 0.2,
  "switch_threshold": 0.05,
  "minimum_duration": 0.5,
  "actions": [
    {
      "name": "Attack",
      "script": "res/AI/attack.action.lua",
      "curves": ["res/AI/healthy.curve.json", "res/AI/ammo.curve.json"]
    },
    {
      "name": "Heal",
      "script": "res/AI/heal.action.lua",
      "curves": ["res/AI/injured.curve.json", null]
    }
  ]
}
```

Порядок ячеек `curves` совпадает с порядком `parameters`. `null` исключает параметр
из оценки строки. Нужна хотя бы одна кривая на действие. Имена параметров и имена
действий уникальны внутри своих списков. Действия обязательно имеют суффикс `.action.lua`.
Все пути разрешаются относительно корня проекта менеджером ресурсов, а не относительно
файла таблицы. Без корня проекта пути считаются от рабочей директории процесса.

Пример `injured.curve.json`:

```json
{
  "points": [
    { "x": 0, "y": 1 },
    { "x": 50, "y": 0.7 },
    { "x": 100, "y": 0 }
  ]
}
```

Нужны минимум две точки с конечными числами, строго возрастающими `x` и `y` в диапазоне
0…1. Интерполяция линейная, вне диапазона возвращается значение крайней точки.
`x` измеряется непосредственно в единицах параметра. Общая кривая переиспользуется
по пути; горячая перезагрузка не реализована.

### Параметры из обычного Lua-скрипта

```lua
local ai = self.object:get_component(ComponentType.UtilityAI)
local health, error = ai:get_float("Health")
if health ~= nil then
    local ok, write_error = ai:set_float("Health", health - 10)
    if not ok then print(write_error) end
else
    print(error)
end

for _, parameter in ipairs(ai:get_parameters()) do
    print(parameter.name, parameter.type, parameter.value)
end
```

| API | Результат |
|---|---|
| `get_float(name)`, `get_int(name)` | Значение и пустая строка; при ошибке `nil, error` |
| `set_float(name, value)`, `set_int(name, value)` | `true, ""`; при ошибке `false, error` (неверные аргументы могут вернуть `nil, error`) |
| `get_parameters()` | Новый массив `{name, type, value}`; его изменение не меняет компонент |
| `current_action` | Имя действия или пустая строка, только чтение |
| `last_error` | Последняя ошибка загрузки/исполнения, только чтение |
| `enabled` | Флаг включения, доступен для записи |

`float` хранится как конечное число двойной точности; `int` — целое в диапазоне
−2147483648…2147483647. Тип доступа должен совпадать с описанием. Неизвестные имена,
дробный `int`, NaN и бесконечности отклоняются. Setter не запускает пересчёт.
Изменение `enabled` обрабатывается после текущего callback либо в следующем Update.

Компоненты инициализируются в порядке объекта: для доступа из `on_init` обычного
скрипта разместите UtilityAIComponent раньше него. До инициализации методы параметров
возвращают ошибку. Параметры сбрасываются к значениям таблицы при повторной инициализации.
Runtime-значения и ход действия не сохраняются в JSON объекта.

### Контракт `.action.lua`

Пример `heal.action.lua`:

```lua
return {
    action_available = function(self, context)
        local health = context.ai:get_float("Health")
        return health ~= nil and health < 100
    end,

    action_entered = function(self, context)
        self.elapsed = 0
    end,

    action_executing = function(self, context, dt)
        self.elapsed = self.elapsed + dt
        if self.elapsed < 1 then return "running" end
        local health = context.ai:get_float("Health")
        if health == nil then return "failure" end
        local ok = context.ai:set_float("Health", math.min(100, health + 25))
        return ok and "success" or "failure"
    end,

    action_exited = function(self, context, reason)
        self.elapsed = 0
    end
}
```

Три функции жизненного цикла обязательны; `action_available` необязательна и должна
возвращать boolean. `context.object` — владелец, `context.ai` — его компонент,
`context.is_valid` — признак живого контекста. После Shutdown ссылки контекста очищаются.
Не сохраняйте заимствованные указатели на удаляемые объекты/компоненты.

У каждого действия каждого NPC собственные модуль и `self`. Lua-глобальные переменные
остаются общими: индивидуальное состояние храните в `self`, сбрасывая его при входе.
Проверки доступности не должны менять мир; запись AI-параметров в этой фазе запрещена,
а чтение использует единый снимок текущего пересчёта.

Полезность — среднее непустых ячеек. Побеждает максимум; при равенстве сохраняется
текущее действие, иначе выбирается первая подходящая строка. Переключение требует
превышения текущей оценки более чем на `switch_threshold` и истечения `minimum_duration`
в секундах. Потеря доступности обнаруживается при пересчёте и отменяет эти ограничения.
Если нет доступных действий, компонент ждёт следующего пересчёта.

`action_executing` вызывается каждый игровой кадр, включая кадр успешного входа.
После `success`/`failure` новый выбор происходит на следующем обновлении компонента.
`action_exited` вызывается один раз после успешного входа; причины: `success`, `failure`,
`interrupted`, `unavailable`, `disabled`, `shutdown`, `error`.
При ошибке выполнения или доступности действие отключается до переинициализации;
для уже активного действия делается одна защищённая попытка выхода. При ошибке входа
выход не вызывается. Ошибки выводятся в консоль и доступны через `last_error`.

Синхронизация здоровья/боезапаса с другими игровыми компонентами выполняется игровыми
скриптами. Система не ищет цели, не строит маршруты и не содержит UI.
