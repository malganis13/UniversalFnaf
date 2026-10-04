# Архитектура UniversalFnaf

Документ описывает слои, контракты, потоки данных и обоснование каждого
неинвазивного решения. Цель — чтобы архитектуру можно было проверить, а не
поверить ей на слово.

---

## 1. Слои и зависимости

```
                        ┌─────────────────────────────┐
                        │  main.cpp (wWinMain)        │  COM STA, single instance,
                        │  DPI, обработка исключений  │  без бизнес-логики
                        └──────────────┬──────────────┘
                                       │
                        ┌──────────────▼──────────────┐
                        │  App/Application            │  композиционный корень:
                        │  (композиция + главный цикл)│  владеет всеми объектами,
                        └───┬───────┬───────┬─────────┘  связывает интерфейсы
                            │       │       │
        ┌───────────────────┘       │       └────────────────────┐
        │                           │                            │
┌───────▼────────┐        ┌─────────▼─────────┐        ┌─────────▼─────────┐
│ Net            │        │ Process           │        │ Input             │
│ INetworkFilter │        │ IProcessMonitor   │        │ IHotkeyManager    │
│  └ WfpFilter   │        │  └ Win32Process   │        │  └ Win32Hotkey    │
│    Manager     │        │    Manager        │        │  └ HotkeyCapture  │
│ ITcpConnTerm.  │        │                   │        │                   │
│  └ Win32Tcp…   │        │                   │        │                   │
└───────┬────────┘        └─────────┬─────────┘        └─────────┬─────────┘
        │                           │                            │
        │              ┌────────────▼─────────────┐              │
        │              │ UI                       │              │
        └─────────────►│ Renderer (D3D11/DComp)   │◄─────────────┘
                       │ OverlayWindow (Win32)    │
                       │ UIManager (Dear ImGui)   │
                       │ GifAnimator (GDI+)       │
                       └────────────┬─────────────┘
                                    │
                       ┌────────────▼─────────────┐
                       │ Core/ConfigManager       │
                       │ Common/*  (WinError,     │
                       │  Logger, Utils, GDI+,    │
                       │  Localization RU/ENG)    │
                       └──────────────────────────┘
```

Правила зависимостей:

* `Common` не зависит ни от чего, кроме Win32.
* `Core` зависит только от `Common` (`TransparencyMode` и `Localization` вынесены
  в `Common` именно для того, чтобы `Core` не тянул `UI/Renderer.h` с DirectX).
* `Net`, `Process`, `Input` зависят от `Common` и своих интерфейсов.
* `UI` зависит от `Common` и интерфейсов `Input`/`Process`.
* `App` — единственное место, где известны конкретные реализации.
* `UI` не вызывает Win32-операции над окном напрямую: перетаскивание и ресайз
  публикуются как дельты в `UiRequests`, а применяет их `Application`.

Инъекция зависимостей — через конструкторы (`ILogger&`, `const GdiPlusSession&`,
`Renderer&`, `const GifAnimator&`). Глобальных синглтонов нет; единственный
статический ресурс — состояние GDI+, которым владеет `GdiPlusSession` в `Application`.

---

## 2. Соответствие требованиям (SOLID)

| Требование задания | Тип | Реализация |
|---|---|---|
| Обёртка WFP, добавляет/удаляет блокировку исходящих по .exe или PID | `INetworkFilter` → `WfpFilterManager` | ALE_AUTH_CONNECT v4/v6 + `ALE_APP_ID`, транзакции, динамическая сессия |
| Список пользовательских процессов, фильтрация системных | `IProcessMonitor` → `Win32ProcessManager` | `CreateToolhelp32Snapshot`, denylist, `PROCESS_QUERY_LIMITED_INFORMATION` |
| Глобальные хоткеи | `IHotkeyManager` → `Win32HotkeyManager` | `RegisterHotKey`/`UnregisterHotKey` + скрытое окно-приёмник `WM_HOTKEY` |
| Настройки в `config.json` | `ConfigManager` | nlohmann/json, атомарная запись, устойчивость к правкам руками |
| ImGui DX11: RGB-границы, ввод | `UIManager` | ImGui-окно на весь клиент, hue-кольцо по периметру окна, `UiState`/`UiRequests` |
| GIF в левом верхнем углу окна | `GifAnimator` | GDI+ декодирование в RGBA, кэш текстур в `UIManager` |
| Сквозной клик | `OverlayWindow::SetClickThrough` | `WS_EX_TRANSPARENT` + `HTTRANSPARENT` |
| Обычное перемещаемое окно приложения | `OverlayWindow` + `UIManager` | drag за заголовок, ресайз за угол, `WS_EX_APPWINDOW`, геометрия в `config.json` |
| RGB-обводка самого окна | `UIManager::DrawWindowBorder` | кольцо из `AddQuadFilled` по скруглённому периметру, hue по длине дуги, свечение |
| Язык RU/ENG | `Common/Localization.h` + кнопки `RU`/`EN` | таблица `UiStrings`, выбор в `config.json` (`ui.language`) |
| Блокировка «от меня» без остатка | `ITcpConnectionTerminator` → `Win32TcpConnectionTerminator` | `GetExtendedTcpTable` + `SetTcpEntry(DELETE_TCB)`, входящие сессии сохраняются |
| Манифест requireAdministrator | `resources/app.manifest` | встроен через `.rc`, `/MANIFEST:NO` |

**SRP.** `OverlayWindow` знает только о Win32-окне и его стилях; пиксели — дело
`Renderer`; содержимое — дело `UIManager`; декодирование GIF — `GifAnimator`.
`WfpFilterManager` не ищет процессы (`Win32ProcessManager`), а `Application`
разрешает PID → путь и передаёт `BlockTarget`.

**OCP/DIP.** Все четыре подсистемы используются через интерфейсы, поэтому
`INetworkFilter` можно заменить на реализацию через Windows Firewall COM API,
`IHotkeyManager` — на другую платформу, не меняя `Application`.

**ISP/LSP.** Узкие интерфейсы без «божественных» методов; реализации не
ослабляют контракты (все — `final`, методы возвращают либо значение, либо
`bool + сообщение об ошибке`).

**RAII.** `ScopeGuard`, `UniqueHandle`, `WRL::ComPtr`, `std::unique_ptr`; порядок
разрушения в `Application::Shutdown` гарантирует, что фильтры снимаются раньше,
чем закрывается оконная процедура и логгер.

---

## 3. Поток данных: блокировка трафика

```
пользователь ──► UIManager (кнопка/тумблер)
                        │ UiRequests::toggleBlocking
                        ▼
                 Application::ApplyRequests
                        │
                        ▼
              Application::ResolveBlockTarget
        ┌───────────────┴────────────────┐
        │ путь из config/UI              │ если пути нет, но есть PID:
        │                                ▼
        │                    IProcessMonitor::ResolveImagePath
        │                    (OpenProcess(QUERY_LIMITED_INFORMATION)
        │                     + QueryFullProcessImageNameW)
        ▼
   INetworkFilter::ApplyOutboundBlock(BlockTarget)
        │
        ▼
   WfpFilterManager:
     FwpmGetAppIdFromFileName0(path) -> FWP_BYTE_BLOB
     FwpmTransactionBegin0
       FwpmFilterAdd0(ALE_AUTH_CONNECT_V4, ALE_APP_ID, BLOCK)
       FwpmFilterAdd0(ALE_AUTH_CONNECT_V6, ALE_APP_ID, BLOCK)
     FwpmTransactionCommit0
```

Обратный путь (снятие) — `FwpmFilterDeleteById0` для обоих id внутри транзакции.
Аварийный путь (kill/crash) — закрытие дескриптора динамической сессии ядром,
после чего WFP удаляет фильтры и подслой сам.

Хоткей приходит не в UI-поток, а в окно-приёмник `Win32HotkeyManager`; колбэк
кладёт `HotkeyAction` в `pendingHotkeys_` под мьютексом, а главный цикл забирает
их на своём шаге (`ProcessPendingHotkeys`). Так тяжёлый RPC-вызов WFP никогда не
выполняется внутри оконной процедуры.

---

## 4. Ключевые решения и отвергнутые альтернативы

### 4.1. WFP, а не драйвер и не хуки сокетов

| Вариант | Почему отклонён |
|---|---|
| NDIS-фильтр / WFP callout-драйвер | Ядерный драйвер, тестовая подпись, BSOD-риски, максимальное внимание антивирусов |
| LSP / перехват `connect`/`send` | DLL в чужой процесс — прямое нарушение принципа неинвазивности |
| `SetWindowsHookEx` + фильтрация в чужом процессе | Инжект + хук, античиты реагируют мгновенно |
| Windows Firewall COM (`INetFwRules`) | Допустимо и официально, но правила **постоянные**: они переживают выход утилиты, требуют ручной уборки и меняют пользовательский профиль брандмауэра |
| **WFP + динамическая сессия** | Только RPC к `BFE`, объекты живут в рамках сессии, нулевой след после выхода, точное соответствие «блокировать программу» |

### 4.2. Почему блокировка по образу, а не «по PID»

Условия ALE-слоёв для фильтров описывают **идентичность приложения**
(`FWPM_CONDITION_ALE_APP_ID`), а не номер процесса. Это тот же механизм, что
использует брандмауэр Windows в правилах «Программа». PID используется только
для поиска пути и отображения цели; если процесс перезапустился, путь остаётся
валидным, а блокировка сохраняется. Эмуляция «только этот PID» потребовала бы
callout-драйвера, что несовместимо с главным приоритетом проекта.

### 4.3. Динамическая сессия и транзакции

* `FWPM_SESSION_FLAG_DYNAMIC` — гарантия уборки: фильтры и подслой привязаны к
  `HANDLE` движка; при завершении процесса (в том числе аварийном) ядро снимает их.
* `FwpmTransactionBegin0/Commit/Abort` — атомарность: нельзя получить состояние,
  когда IPv4 уже заблокирован, а IPv6 ещё нет.
* Подслой с weight `0x7FFF` — приоритет над подслоем брандмауэра; при этом чужие
  подслои и правила не читаются и не изменяются.
* `FwpmSubLayerAdd0`, вернувший `FWP_E_ALREADY_EXISTS`, не считается ошибкой:
  подслой с нашим GUID переиспользуется (например, оставшийся от старой сборки).

### 4.4. Прозрачность: DirectComposition вместо `WS_EX_LAYERED`

`WS_EX_LAYERED` + `UpdateLayeredWindow` — устаревший путь: попиксельная альфа
требует ручного композитинга и GDI-манипуляций. DirectComposition даёт DWM
нативный слой с premultiplied-альфой и работает с обычным DXGI swap chain.
`WS_EX_LAYERED` сохранён как автоматический fallback (colour-key), чтобы утилита
работала и там, где `DCompositionCreateDevice` недоступен.

Порядок выбора в `Application::Initialize`: пробуем DirectComposition → при
неудаче пересоздаём окно без `WS_EX_NOREDIRECTIONBITMAP`, включаем
`WS_EX_LAYERED` + `LWA_COLORKEY` и повторяем инициализацию рендерера.

### 4.5. Захват хоткея без фокуса и без хуков

Окно живёт с `WS_EX_NOACTIVATE`, поэтому `WM_KEYDOWN` в него не приходит. Хук
`WH_KEYBOARD_LL` — вне принципов проекта. Решение: `HotkeyCapture` опрашивает
`GetAsyncKeyState` на кадрах UI и сравнивает с предыдущим состоянием; уже
зажатые клавиши исключаются снимком состояния в момент `Begin()`. Реальная
регистрация по-прежнему выполняется только через `RegisterHotKey`.

### 4.6. Ошибки: исключения на старте, `bool + сообщение` в рантайме

* Конструкторы/инициализация: исключения (`Win32Error`, `HresultError`) —
  `main.cpp` ловит и показывает `MessageBoxW`, приложение не стартует частично.
* Рантайм-операции (поставить/снять блокировку, сохранить конфиг, зарегистрировать
  хоткей): `bool` + `std::wstring error`, потому что это ожидаемые состояния
  (занятая комбинация, отсутствие прав, исчезнувший файл). Текст попадает в
  статусную строку UI и в журнал, приложение продолжает работать.

---

## 5. Модель UI: `UiState` → `UiRequests`

Однонаправленный поток, похожий на MVVM:

```
Application ──(обновляет read-only поля)──► UiState ──► UIManager::RenderFrame
     ▲                                                        │
     └────── UiRequests (намерения) ◄──────────────────────────┘
```

* `UiState` содержит и данные для отображения, и **редактируемые** поля
  (настройки рамки, путь к GIF, флаги окна). Правки UI живут ровно до следующего
  кадра; `Application::UpdateUiState` не перетирает их, а обновляет только
  read-only часть (статус движка, цель, счётчики, FPS).
* `UiRequests` — перечисление намерений (`toggleBlocking`, `saveConfig`,
  `rebindHotkey`, `quit`, ...) плюс полезная нагрузка (`rebindAction`,
  `reboundBinding`). Применяет их исключительно `Application`, поэтому UI не
  имеет доступа к WFP, реестру и файловой системе напрямую.
* Отдельно: `UiRequests::processOptionsChanged` пересоздаёт опции
  `Win32ProcessManager` и принудительно обновляет список.
* Геометрия окна тоже проходит через `UiRequests`: заголовок публикует
  `windowDrag` + дельту, угол — `windowResize` + дельту. `Application` применяет их
  через `OverlayWindow::MoveBy/ResizeBy`, поэтому UI не вызывает Win32 напрямую и
  остаётся тестируемым.
* Локализация — чистая функция от `UiState::language`: `StringsFor(language)` даёт
  `const UiStrings&` (RU/ENG), таблица строк не тянет ImGui и доступна `Core/Config`
  для сохранения выбора.

Отрисовка кадра:

```
Renderer::BeginFrame()        // clear(0,0,0,0), bind RTV, viewport
UIManager::NewFrame()         // ImGui_ImplDX11_NewFrame, ImGui_ImplWin32_NewFrame, ImGui::NewFrame
UIManager::RenderFrame(...)   // ImGui-окно на весь клиент: заголовок (drag/GIF/RU-EN/X),
                              //   прокручиваемый контент, футер, grip ресайза;
                              //   цветное кольцо по периметру окна -> ImGui::Render -> RenderDrawData
Renderer::EndFrame()          // Present(1,0)
```

Кольцо рамки строится как замкнутый путь по скруглённому прямоугольнику
(`BuildRoundedRectPath`: центр, нормаль и нормированная длина дуги для каждой точки),
затем `EmitHueRing` выпускает по одному `AddQuadFilled` на сегмент, окрашивая его
`hue = hueBase + arcPosition`. При `spread = 1.0` замыкающий сегмент совпадает по
цвету с первым, поэтому радуга замкнута без шва; свечение — два дополнительных
кольца с меньшей альфой внутрь и наружу.

Смена размера (`WM_SIZE`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`) обрабатывается
окном-хуком `Application::InstallWindowMessageHook`: `InvalidateDeviceObjects` →
`ResizeBuffers` → `RecreateDeviceObjects` — штатная последовательность backend'а ImGui.
При смене DPI дополнительно пересобирается стиль и выставляется `FontGlobalScale`
(атлас шрифта растрируется один раз при инициализации).

---

## 6. Потоки и владение ресурсами

| Ресурс | Владелец | Освобождение |
|---|---|---|
| Движок WFP + фильтры | `WfpFilterManager` | `RemoveOutboundBlock` + `FwpmEngineClose0`; при аварии — ядро (dynamic session) |
| Окно оверлея, класс окна | `OverlayWindow` | `Destroy()` + `UnregisterClassW` |
| D3D11 device, swap chain, DComp-визуал | `Renderer` (ComPtr) | `Shutdown()` |
| ImGui context, backend-объекты | `UIManager` | `Shutdown()` |
| Текстуры иконок и кадров GIF | `UIManager` (кэш) | `ReleaseTextureCache()`, вызывается также при `InvalidateDeviceObjects` |
| GDI+ | `GdiPlusSession` | `GdiplusShutdown` |
| Хоткеи | `Win32HotkeyManager` | `UnregisterAll` + уничтожение скрытого окна |
| Обрыв TCP-сессий | `Win32TcpConnectionTerminator` | одноразовые вызовы, состояния не хранит |
| Лог | `FileLogger` | ротация 2 МБ, `fclose` в деструкторе; файл открыт с `_SH_DENYWR`, поэтому читается на ходу |

Поток один (UI) + потоки RPC внутри `fwpuclnt`. Все вызовы WFP — на UI-потоке
между кадрами, поэтому мьютексы не нужны; мьютекс есть только у очереди хоткеев.

---

## 7. Обрыв установленных сессий (почему блокировка полная)

Фильтр на `ALE_AUTH_CONNECT` останавливает только *новые* подключения: уже
открытая TCP-сессия не проходит повторную авторизацию, поэтому данные по ней
продолжают уходить. Чтобы «от меня не уходило» выполнялось буквально, при
включении блока вызывается `Win32TcpConnectionTerminator`:

```
GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0)
   → проход 1: собрать порты, на которых процесс СЛУШАЕТ (состояние LISTEN)
   → проход 2: для строк того же PID и не-LISTEN, чей локальный порт НЕ слушающий:
                 MIB_TCPROW{ dwState = MIB_TCP_STATE_DELETE_TCB, ... } → SetTcpEntry()
   → отдельно посчитать IPv6-сессии (SetTcpEntry работает только с IPv4)
```

Ключевое различие — направление инициатора. У исходящей сессии локальный порт
эфемерный, у принятой извне он равен слушающему порту процесса, поэтому входящие
сессии (`inboundPreserved`) остаются нетронутыми: «ко мне трафик идёт» сохраняется.
Операции выполняются только над записями выбранного PID; чужие соединения не
затрагиваются. Требуются права администратора — они уже есть у процесса.

---

## 8. Точки расширения

1. **Выбор монитора.** Окно свободно перетаскивается и запоминает позицию;
   следующий шаг — привязка к монитору по `MonitorFromPoint` и «магнит» к краям
   рабочей области (`WM_WINDOWPOSCHANGING`).
2. **IPv6-обрыв.** Документированного `SetTcpEntry` для IPv6 нет. Вариант —
   `FwpmNetEventSubscribe4` + завершение потока на уровне WFP-события; это уже
   отдельный модуль за своим интерфейсом.
3. **Альтернативный фильтр через Windows Firewall COM API.** Реализация
   `INetworkFilter` на `INetFwPolicy2` + `INetFwRules`:
   `CoCreateInstance(CLSID_NetFwPolicy2)` → `get_Rules` → `Create(CLSID_NetFwRule)` →
   `put_Action(NET_FW_ACTION_BLOCK)`, `put_Direction(NET_FW_RULE_DIR_OUT)`,
   `put_ApplicationName(path)`, `put_Enabled(VARIANT_TRUE)`, `Rules->Add(rule)`.
   Плюс: правила видны в `wf.msc`. Минус: постоянные записи, требующие явного
   удаления; поэтому WFP остаётся основным механизмом.
4. **Мониторинг/статистика.** `FwpmNetEventSubscribe4` (документированный API
   подписки на события WFP) позволяет строить счётчик заблокированных соединений
   без драйвера.
5. **Профили правил.** `ConfigManager` уже версионирован; список целей
   (`std::vector<BlockTarget>`) и правила «разрешить/запретить» укладываются в
   текущую схему JSON без изменения `INetworkFilter` (достаточно цикла по целям).
6. **Дополнительные языки.** Добавить `UiStrings` в `Common/Localization.h` и
   ветку в `StringsFor` — UI-код менять не нужно.

---

## 9. Что архитектура сознательно НЕ делает

* Не читает и не пишет память других процессов.
* Не внедряет код и не перехватывает API — ни в чужих, ни в своих модулях
  (внутренние вызовы идут напрямую через импорт Win32).
* Не ставит драйверов и не требует тестового режима подписи.
* Не изменяет правила брандмауэра Windows и не трогает чужие WFP-объекты.
* Не скрывает своё присутствие от пользователя: окно видно, действия пишутся в
  журнал, фильтры инспектируются штатным `netsh wfp show state`.
* Не запускается автоматически и не устанавливает службы.
