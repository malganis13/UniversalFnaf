#pragma once
// ---------------------------------------------------------------------------
//  UniversalFnaf :: Common/Localization.h
//
//  Two-language UI string table (Russian / English). Header-only data, no
//  dependency on Dear ImGui, so Core/Config can persist the choice without
//  pulling in the UI layer.
//
//  The literals are UTF-8 (the project compiles with /utf-8), which is exactly
//  what Dear ImGui expects for glyph lookup.
// ---------------------------------------------------------------------------

#include <string_view>

namespace universalfnaf {

enum class UiLanguage {
    Russian = 0,
    English = 1,
};

struct UiStrings {
    // --- header / window ----------------------------------------------------
    const char* appTitle;
    const char* dragHint;
    const char* closeTip;
    const char* resizeHint;
    const char* languageTip;

    // --- engine status ------------------------------------------------------
    const char* enginePrefix;          // "%s"
    const char* engineUnavailable;

    // --- traffic ------------------------------------------------------------
    const char* sectionTraffic;
    const char* stateBlocked;
    const char* stateAllowed;
    const char* targetNone;
    const char* targetFormat;          // "%s (PID %lu)"
    const char* targetRulesTip;        // "%s\nWFP rules: %zu"
    const char* buttonBlock;
    const char* buttonAllow;
    const char* hotkeyFormat;          // "%s"

    // --- connection termination --------------------------------------------
    const char* terminateNow;
    const char* terminateTip;
    const char* terminateResult;       // "%zu / inbound kept: %zu / failed: %zu"
    const char* terminateFailed;       // "%s"

    // --- process picker -----------------------------------------------------
    const char* sectionProcess;
    const char* refreshList;
    const char* showSystem;
    const char* filterHint;
    const char* selectPlaceholder;
    const char* noMatch;
    const char* badgeAdmin;
    const char* badgeWindow;
    const char* listAgeFormat;         // "%.0f"

    // --- hotkeys ------------------------------------------------------------
    const char* sectionHotkeys;
    const char* hotkeyBlock;
    const char* hotkeyBlockTip;
    const char* hotkeyClickThrough;
    const char* hotkeyClickThroughTip;
    const char* hotkeyOverlay;
    const char* hotkeyOverlayTip;
    const char* hotkeyBorder;
    const char* hotkeyBorderTip;
    const char* hotkeyWaiting;
    const char* hotkeyNeedModifier;    // "%s"
    const char* hotkeyCancelled;
    const char* hotkeyClickHint;

    // --- window behaviour ---------------------------------------------------
    const char* sectionWindow;
    const char* clickThrough;
    const char* clickThroughTip;
    const char* clickThroughWarning;   // "%s"
    const char* alwaysOnTop;
    const char* gameMode;
    const char* gameModeTip;
    const char* showInTaskbar;
    const char* excludeFromCapture;
    const char* excludeFromCaptureTip;
    const char* resetPosition;

    // --- RGB border ---------------------------------------------------------
    const char* sectionBorder;
    const char* borderEnabled;
    const char* borderThickness;
    const char* borderHueSpeed;
    const char* borderSaturation;
    const char* borderBrightness;
    const char* borderGlow;

    // --- GIF ----------------------------------------------------------------
    const char* sectionGif;
    const char* gifEnabled;
    const char* gifEnabledTip;
    const char* browse;
    const char* reload;
    const char* gifScale;
    const char* gifLoadedFormat;       // "%d x %d, %zu"
    const char* gifNotLoaded;
    const char* gifPathHint;

    // --- network options ----------------------------------------------------
    const char* sectionNetwork;
    const char* blockOnStartup;
    const char* terminateExisting;
    const char* terminateExistingTip;

    // --- footer -------------------------------------------------------------
    const char* saveSettings;
    const char* reloadSettings;
    const char* statsFormat;           // "%.0f %.0f"
    const char* statsTip;              // "%s\n%s"
    const char* openLogFolder;
};

inline const UiStrings kUiStringsRussian{
    .appTitle = "UniversalFnaf",
    .dragHint = "Потяните, чтобы переместить окно",
    .closeTip = "Выход: снять все фильтры WFP",
    .resizeHint = "Потяните, чтобы изменить размер окна",
    .languageTip = "Язык интерфейса",

    .enginePrefix = "WFP: %s",
    .engineUnavailable = "Движок недоступен. Запустите утилиту от имени администратора.",

    .sectionTraffic = "Исходящий трафик",
    .stateBlocked = "ТРАФИК ЗАБЛОКИРОВАН",
    .stateAllowed = "ТРАФИК РАЗРЕШЁН",
    .targetNone = "Цель не выбрана",
    .targetFormat = "%s (PID %lu)",
    .targetRulesTip = "%s\nПравил WFP: %zu",
    .buttonBlock = "ЗАБЛОКИРОВАТЬ ТРАФИК",
    .buttonAllow = "РАЗРЕШИТЬ ТРАФИК",
    .hotkeyFormat = "Хоткей: %s",

    .terminateNow = "Разорвать соединения сейчас",
    .terminateTip = "Удаляет уже установленные ИСХОДЯЩИЕ TCP-сессии выбранного\n"
                    "процесса. Входящие (принятые на его слушающий порт) сохраняются.\n"
                    "IPv6-сессии Windows удалить не позволяет - только новые не создаются.",
    .terminateResult = "разорвано: %zu, входящих сохранено: %zu, не удалось: %zu",
    .terminateFailed = "Не удалось разорвать соединения: %s",

    .sectionProcess = "Целевой процесс",
    .refreshList = "Обновить список",
    .showSystem = "Показывать системные",
    .filterHint = "фильтр по имени или PID",
    .selectPlaceholder = "<выберите процесс>",
    .noMatch = "нет подходящих процессов",
    .badgeAdmin = "[админ]",
    .badgeWindow = "[окно]",
    .listAgeFormat = "список обновлён %.0f с назад",

    .sectionHotkeys = "Горячие клавиши (RegisterHotKey)",
    .hotkeyBlock = "Блокировка",
    .hotkeyBlockTip = "Включить или снять блокировку исходящего трафика",
    .hotkeyClickThrough = "Сквозной клик",
    .hotkeyClickThroughTip = "Мышь проходит сквозь окно утилиты",
    .hotkeyOverlay = "Показать окно",
    .hotkeyOverlayTip = "Скрыть или показать окно утилиты",
    .hotkeyBorder = "RGB-рамка",
    .hotkeyBorderTip = "Включить или выключить подсветку рамки",
    .hotkeyWaiting = "Нажмите комбинацию... (Esc - отмена)",
    .hotkeyNeedModifier = "Нужен Ctrl/Alt/Shift или F-клавиша: %s",
    .hotkeyCancelled = "Захват комбинации отменён",
    .hotkeyClickHint = "Нажмите кнопку, затем нажмите нужные клавиши.",

    .sectionWindow = "Окно",
    .clickThrough = "Сквозной клик (WS_EX_TRANSPARENT)",
    .clickThroughTip = "Клики проходят в окно под этим.\nВернуть управление мышью можно хоткеем.",
    .clickThroughWarning = "Сквозной клик включён: управление мышью возвращает %s",
    .alwaysOnTop = "Поверх всех окон",
    .gameMode = "Не забирать фокус (игровой режим)",
    .gameModeTip = "Окно не активируется при клике, игра не теряет фокус.\n"
                   "Ввод текста в полях при этом недоступен.",
    .showInTaskbar = "Показывать в панели задач",
    .excludeFromCapture = "Исключить из скриншотов",
    .excludeFromCaptureTip = "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)",
    .resetPosition = "Сбросить положение окна",

    .sectionBorder = "RGB-рамка окна",
    .borderEnabled = "Включена",
    .borderThickness = "Толщина",
    .borderHueSpeed = "Скорость цвета",
    .borderSaturation = "Насыщенность",
    .borderBrightness = "Яркость",
    .borderGlow = "Свечение",

    .sectionGif = "GIF-анимация",
    .gifEnabled = "Включена",
    .gifEnabledTip = "Рисуется в левом верхнем углу окна",
    .browse = "Обзор...",
    .reload = "Перезагрузить",
    .gifScale = "Масштаб",
    .gifLoadedFormat = "загружено %d x %d, кадров: %zu",
    .gifNotLoaded = "анимация не загружена",
    .gifPathHint = "путь к .gif или кнопка Обзор...",

    .sectionNetwork = "Сеть",
    .blockOnStartup = "Блокировать при запуске",
    .terminateExisting = "Обрывать установленные сессии",
    .terminateExistingTip = "При включении блокировки сразу удалять уже открытые\n"
                            "исходящие TCP-сессии выбранного процесса.",

    .saveSettings = "Сохранить настройки",
    .reloadSettings = "Перечитать",
    .statsFormat = "%.0f FPS | аптайм %.0f с",
    .statsTip = "конфиг: %s\nлог: %s",
    .openLogFolder = "Открыть папку с логом",
};

inline const UiStrings kUiStringsEnglish{
    .appTitle = "UniversalFnaf",
    .dragHint = "Drag to move the window",
    .closeTip = "Exit and release all WFP filters",
    .resizeHint = "Drag to resize the window",
    .languageTip = "UI language",

    .enginePrefix = "WFP: %s",
    .engineUnavailable = "Engine unavailable. Run the utility as administrator.",

    .sectionTraffic = "Outbound traffic",
    .stateBlocked = "TRAFFIC BLOCKED",
    .stateAllowed = "TRAFFIC ALLOWED",
    .targetNone = "No target selected",
    .targetFormat = "%s (PID %lu)",
    .targetRulesTip = "%s\nWFP rules: %zu",
    .buttonBlock = "BLOCK TRAFFIC",
    .buttonAllow = "ALLOW TRAFFIC",
    .hotkeyFormat = "Hotkey: %s",

    .terminateNow = "Terminate connections now",
    .terminateTip = "Deletes the already established OUTBOUND TCP sessions of the\n"
                    "selected process. Inbound sessions accepted on its listening\n"
                    "port are preserved. Windows offers no API to drop IPv6 sessions;\n"
                    "for those only new connections are prevented.",
    .terminateResult = "terminated: %zu, inbound kept: %zu, failed: %zu",
    .terminateFailed = "Could not terminate connections: %s",

    .sectionProcess = "Target process",
    .refreshList = "Refresh list",
    .showSystem = "Show system processes",
    .filterHint = "filter by name or PID",
    .selectPlaceholder = "<select a process>",
    .noMatch = "no matching process",
    .badgeAdmin = "[admin]",
    .badgeWindow = "[window]",
    .listAgeFormat = "list refreshed %.0f s ago",

    .sectionHotkeys = "Hotkeys (RegisterHotKey)",
    .hotkeyBlock = "Blocking",
    .hotkeyBlockTip = "Engage or release the outbound block",
    .hotkeyClickThrough = "Click-through",
    .hotkeyClickThroughTip = "The mouse passes through this window",
    .hotkeyOverlay = "Show window",
    .hotkeyOverlayTip = "Hide or show this window",
    .hotkeyBorder = "RGB border",
    .hotkeyBorderTip = "Toggle the animated window border",
    .hotkeyWaiting = "Press a combination... (Esc cancels)",
    .hotkeyNeedModifier = "Add Ctrl/Alt/Shift or use an F-key: %s",
    .hotkeyCancelled = "Hotkey capture cancelled",
    .hotkeyClickHint = "Click the button, then press the combination.",

    .sectionWindow = "Window",
    .clickThrough = "Click-through (WS_EX_TRANSPARENT)",
    .clickThroughTip = "Clicks go to the window underneath.\nA hotkey brings mouse control back.",
    .clickThroughWarning = "Click-through is ON: press %s to get mouse control back",
    .alwaysOnTop = "Always on top",
    .gameMode = "Do not take focus (game mode)",
    .gameModeTip = "The window does not activate on click, so a game keeps focus.\n"
                   "Text fields become unusable in this mode.",
    .showInTaskbar = "Show in the taskbar",
    .excludeFromCapture = "Exclude from screen capture",
    .excludeFromCaptureTip = "SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)",
    .resetPosition = "Reset window position",

    .sectionBorder = "Window RGB border",
    .borderEnabled = "Enabled",
    .borderThickness = "Thickness",
    .borderHueSpeed = "Hue speed",
    .borderSaturation = "Saturation",
    .borderBrightness = "Brightness",
    .borderGlow = "Glow",

    .sectionGif = "GIF animation",
    .gifEnabled = "Enabled",
    .gifEnabledTip = "Drawn in the top-left corner of this window",
    .browse = "Browse...",
    .reload = "Reload",
    .gifScale = "Scale",
    .gifLoadedFormat = "loaded %d x %d, frames: %zu",
    .gifNotLoaded = "no animation loaded",
    .gifPathHint = "path to a .gif or use Browse...",

    .sectionNetwork = "Network",
    .blockOnStartup = "Block on startup",
    .terminateExisting = "Terminate established sessions",
    .terminateExistingTip = "When blocking starts, immediately delete the already open\n"
                            "outbound TCP sessions of the selected process.",

    .saveSettings = "Save settings",
    .reloadSettings = "Reload settings",
    .statsFormat = "%.0f FPS | uptime %.0f s",
    .statsTip = "config: %s\nlog: %s",
    .openLogFolder = "Open log folder",
};

inline const UiStrings& StringsFor(UiLanguage language)
{
    return language == UiLanguage::English ? kUiStringsEnglish : kUiStringsRussian;
}

inline const wchar_t* LanguageTag(UiLanguage language)
{
    return language == UiLanguage::English ? L"en" : L"ru";
}

inline UiLanguage LanguageFromTag(std::wstring_view tag)
{
    if (tag == L"en" || tag == L"EN" || tag == L"english") {
        return UiLanguage::English;
    }
    return UiLanguage::Russian;
}

} // namespace universalfnaf
