// FlashShot's automated tests: a minimal runner without dependencies.
//
//   FlashShotTests.exe <build dir>
//
// The build dir holds FlashShot.exe, used by the lifecycle tests. Tests that
// need something unavailable report SKIP instead of passing.

#include "app/screen_capture.h"
#include "core/clipboard.h"
#include "core/encode.h"
#include "core/geometry.h"
#include "core/hotkey.h"
#include "core/paths.h"
#include "core/render.h"
#include "core/session.h"
#include "core/settings.h"

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace flashshot;
namespace fs = std::filesystem;

namespace {

struct TestCase {
    const char* name;
    std::function<void()> run;
};

std::vector<TestCase>& Tests() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> run) { Tests().push_back({name, std::move(run)}); }
};

struct Failure {
    std::string message;
};
struct Skip {
    std::string reason;
};

#define TEST(name)                                  \
    static void name();                             \
    static Registrar name##_registrar(#name, name); \
    static void name()

#define CHECK(expression)                                                                                                \
    do {                                                                                                                 \
        if (!(expression)) throw Failure{std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #expression}; \
    } while (false)

#define SKIP(reason) throw Skip{reason}

fs::path buildDir;

constexpr wchar_t kTestSettingsKey[] = L"Software\\FlashShot Screen Capture Tests";
constexpr float kTolerance = 6;

fs::path TemporaryFolder() {
    wchar_t folder[MAX_PATH];
    GetTempPathW(MAX_PATH, folder);
    const fs::path path = fs::path(folder) / L"FlashShotTests";
    fs::create_directories(path);
    return path;
}

Session NewSession(int width = 1000, int height = 800) {
    return Session(width, height, DefaultToolStyles(), MeasureTextExtent);
}

// Drags from `a` to `b` in a few steps, like a mouse would.
Session::Release Drag(Session& session, Vec a, Vec b, Modifiers modifiers = {}) {
    session.PointerDown(a, modifiers, kTolerance);
    session.PointerMove({(a.x + b.x) / 2, (a.y + b.y) / 2}, modifiers);
    return session.PointerUp(b, modifiers);
}

Session SessionWithRegion(RECT region = {100, 100, 600, 500}) {
    Session session = NewSession();
    Drag(session, {static_cast<float>(region.left), static_cast<float>(region.top)},
         {static_cast<float>(region.right - 1), static_cast<float>(region.bottom - 1)});
    return session;
}

bool SameRect(const RECT& a, const RECT& b) { return EqualRect(&a, &b) != FALSE; }

// A gray test pattern standing in for the frozen screen.
BgraImage Pattern(int width, int height) {
    BgraImage image{width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t* p = &image.pixels[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = static_cast<uint8_t>(x * 7);
            p[1] = static_cast<uint8_t>(y * 3);
            p[2] = static_cast<uint8_t>((x + y) * 5);
            p[3] = 255;
        }
    }
    return image;
}

COLORREF PixelAt(const BgraImage& image, int x, int y) {
    const uint8_t* p = &image.pixels[(static_cast<size_t>(y) * image.width + x) * 4];
    return RGB(p[2], p[1], p[0]);
}

std::optional<BgraImage> DecodeFile(const fs::path& path) {
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    std::optional<BgraImage> result;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
        SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0,
                                        WICBitmapPaletteTypeCustom))) {
        UINT width = 0, height = 0;
        converter->GetSize(&width, &height);
        BgraImage image{static_cast<int>(width), static_cast<int>(height), std::vector<uint8_t>(static_cast<size_t>(width) * height * 4)};
        if (SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(image.pixels.size()), image.pixels.data()))) {
            result = std::move(image);
        }
    }
    for (IUnknown* object : {static_cast<IUnknown*>(converter), static_cast<IUnknown*>(frame), static_cast<IUnknown*>(decoder),
                             static_cast<IUnknown*>(factory)}) {
        if (object) object->Release();
    }
    return result;
}

}  // namespace

// --- Region ---------------------------------------------------------------------------

TEST(SelectionWorksInEveryDragDirection) {
    Session upward = NewSession();
    CHECK(Drag(upward, {300, 400}, {100, 150}) == Session::Release::SelectionMade);
    CHECK(SameRect(upward.selection(), {100, 150, 301, 401}));

    Session forward = NewSession();
    Drag(forward, {100, 150}, {300, 400});
    CHECK(SameRect(forward.selection(), upward.selection()));
}

TEST(ClickWithoutDragMakesNoRegion) {
    Session session = NewSession();
    CHECK(Drag(session, {50, 50}, {51, 51}) == Session::Release::None);
    CHECK(!session.HasSelection());
    CHECK(Drag(session, {50, 50}, {200, 120}) == Session::Release::SelectionMade);
    CHECK(session.HasSelection());
}

TEST(CtrlDragAsksToCopyOnRelease) {
    Session session = NewSession();
    CHECK(Drag(session, {10, 10}, {200, 200}, {false, true}) == Session::Release::CopyRequested);
    CHECK(session.HasSelection());
}

TEST(RegionResizesFromEveryCornerAndFlips) {
    Session session = SessionWithRegion({100, 100, 600, 500});
    Drag(session, {100, 100}, {50, 60});
    CHECK(SameRect(session.selection(), {50, 60, 600, 500}));
    Drag(session, {600, 60}, {700, 30});
    CHECK(SameRect(session.selection(), {50, 30, 700, 500}));
    Drag(session, {700, 500}, {720, 550});
    CHECK(SameRect(session.selection(), {50, 30, 720, 550}));
    Drag(session, {50, 550}, {10, 600});
    CHECK(SameRect(session.selection(), {10, 30, 720, 600}));
    // Past the opposite edge the region flips instead of vanishing.
    Drag(session, {720, 300}, {0, 300});
    CHECK(SameRect(session.selection(), {0, 30, 10, 600}));
}

TEST(RegionAtTheScreenEdgeStaysInside) {
    Session session = SessionWithRegion({0, 0, 200, 100});
    Drag(session, {100, 50}, {-500, -500});  // Moving with the Select tool
    CHECK(SameRect(session.selection(), {0, 0, 200, 100}));
    Drag(session, {100, 50}, {2000, 2000});
    CHECK(SameRect(session.selection(), {800, 700, 1000, 800}));
}

TEST(ArrowKeysMoveTheRegionOnePixel) {
    Session session = SessionWithRegion({100, 100, 200, 200});
    CHECK(session.NudgeSelection(-1, 0));
    CHECK(SameRect(session.selection(), {99, 100, 199, 200}));
    Session corner = SessionWithRegion({0, 0, 50, 50});
    CHECK(!corner.NudgeSelection(0, -1));
}

// --- Drawing and editing ---------------------------------------------------------------

TEST(ShiftSnapsLinesTo45Degrees) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Arrow);
    Drag(session, {200, 200}, {300, 212}, {true, false});
    const Annotation& arrow = session.annotations().back();
    CHECK(arrow.kind == Kind::Arrow);
    CHECK(arrow.points[1].y == 200);
    Drag(session, {200, 300}, {290, 395}, {true, false});
    const Annotation& diagonal = session.annotations().back();
    CHECK(std::abs((diagonal.points[1].x - 200) - (diagonal.points[1].y - 300)) < 1);
}

TEST(ShiftMakesSquaresAndCircles) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Ellipse);
    Drag(session, {200, 200}, {300, 250}, {true, false});
    const Box box = Box::Spanning(session.annotations().back().points[0], session.annotations().back().points[1]);
    CHECK(box.width() == box.height());
}

TEST(TinyGesturesDoNotLeaveObjects) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Rectangle);
    Drag(session, {200, 200}, {200, 200});
    CHECK(session.annotations().empty());
    CHECK(!session.CanUndo());
}

TEST(EachGestureIsOneUndoStep) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Pen);
    session.PointerDown({200, 200}, {}, kTolerance);
    for (int i = 1; i < 50; ++i) session.PointerMove({200.0f + i, 200.0f + i % 7}, {});
    session.PointerUp({260, 210}, {});
    CHECK(session.annotations().size() == 1);
    CHECK(session.annotations()[0].points.size() > 10);
    CHECK(session.Undo());
    CHECK(session.annotations().empty());
    CHECK(!session.CanUndo());
    CHECK(session.Redo());
    CHECK(session.annotations().size() == 1);
}

TEST(NumbersKeepTheirSequenceThroughDeleteAndUndo) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Number);
    Drag(session, {200, 200}, {200, 200});
    Drag(session, {300, 200}, {300, 200});
    CHECK(session.annotations().size() == 2);
    CHECK(session.annotations()[0].number == 1 && session.annotations()[1].number == 2);
    CHECK(session.nextNumber() == 3);

    session.SetTool(Tool::Select);
    Drag(session, {200, 200}, {200, 200});
    CHECK(session.selected() == 0);
    CHECK(session.DeleteSelected());
    CHECK(session.annotations().size() == 1 && session.annotations()[0].number == 2);

    session.SetTool(Tool::Number);
    Drag(session, {400, 200}, {400, 200});
    CHECK(session.annotations().back().number == 3);

    CHECK(session.Undo());  // The third number
    CHECK(session.nextNumber() == 3);
    CHECK(session.Undo());  // The delete
    CHECK(session.annotations().size() == 2 && session.annotations()[0].number == 1);
    CHECK(session.Undo());  // The second number
    CHECK(session.nextNumber() == 2);
    CHECK(session.Redo());
    CHECK(session.nextNumber() == 3);
}

TEST(EditingAnOlderObjectKeepsTheNewerOnes) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Rectangle);
    Drag(session, {150, 150}, {250, 250});
    session.SetTool(Tool::Arrow);
    Drag(session, {300, 300}, {400, 350});
    session.SetTool(Tool::Number);
    Drag(session, {450, 400}, {450, 400});

    session.SetTool(Tool::Select);
    Drag(session, {150, 200}, {170, 220});  // Grabs the rectangle by its edge and moves it
    CHECK(session.selected() == 0);
    session.SetColor(RGB(0, 144, 255));
    CHECK(session.annotations().size() == 3);
    CHECK(session.annotations()[0].points[0].x == 170);
    CHECK(session.annotations()[0].color == RGB(0, 144, 255));
    CHECK(session.annotations()[1].kind == Kind::Arrow && session.annotations()[2].kind == Kind::Number);

    CHECK(session.Undo());  // Color
    CHECK(session.annotations()[0].color == DefaultToolStyles()[static_cast<size_t>(Tool::Rectangle)].color);
    CHECK(session.annotations()[0].points[0].x == 170);
    CHECK(session.Undo());  // Move
    CHECK(session.annotations()[0].points[0].x == 150);
    CHECK(session.Redo());
    CHECK(session.Redo());
    CHECK(session.annotations()[0].color == RGB(0, 144, 255));
}

TEST(NewActionAfterUndoDropsRedo) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Line);
    Drag(session, {150, 150}, {250, 250});
    CHECK(session.Undo());
    CHECK(session.CanRedo());
    Drag(session, {150, 300}, {250, 300});
    CHECK(!session.CanRedo());
}

TEST(TopmostObjectIsPickedFirst) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Censor);
    Drag(session, {200, 200}, {300, 300});
    Drag(session, {250, 250}, {350, 350});
    session.SetTool(Tool::Select);
    const auto hit = session.HitTestAt({275, 275}, kTolerance);
    CHECK(hit.target == Session::Target::Annotation && hit.index == 1);
}

TEST(EndpointsOfArrowsCanBeEdited) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Arrow);
    Drag(session, {200, 200}, {300, 300});
    session.SetTool(Tool::Select);
    Drag(session, {250, 250}, {250, 250});  // Select it
    Drag(session, {300, 300}, {350, 260});  // Drag its tip
    CHECK(session.annotations()[0].points[0] == (Vec{200, 200}));
    CHECK(session.annotations()[0].points[1] == (Vec{350, 260}));
}

TEST(EscapeRevertsTheGestureInProgress) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Rectangle);
    session.PointerDown({200, 200}, {}, kTolerance);
    session.PointerMove({260, 260}, {});
    CHECK(session.CancelGesture());
    CHECK(session.annotations().empty());
    CHECK(!session.GestureActive());
    CHECK(!session.CancelGesture());
}

TEST(TextEditingCommitsCancelsAndDiscardsEmpty) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Text);
    session.PointerDown({200, 200}, {}, kTolerance);
    CHECK(session.editingText() == 0);
    session.SetEditingText(L"Hello\r\nworld");
    session.FinishTextEdit(true);
    CHECK(session.annotations().size() == 1);
    CHECK(session.annotations()[0].textExtent.y > session.annotations()[0].size * 2);

    session.BeginTextEdit(0, {});
    session.SetEditingText(L"Changed");
    session.FinishTextEdit(false);
    CHECK(session.annotations()[0].text == L"Hello\r\nworld");

    session.PointerDown({400, 400}, {}, kTolerance);
    session.SetEditingText(L"   ");
    session.FinishTextEdit(true);
    CHECK(session.annotations().size() == 1);

    session.BeginTextEdit(0, {});
    session.SetEditingText(L"");
    session.FinishTextEdit(true);
    CHECK(session.annotations().empty());
    CHECK(session.Undo());
    CHECK(session.annotations().size() == 1);
}

TEST(StylesAreRememberedPerTool) {
    Session session = SessionWithRegion();
    session.SetTool(Tool::Pen);
    session.SetColor(RGB(48, 164, 108));
    session.SetSize(12);
    session.SetTool(Tool::Arrow);
    CHECK(session.CurrentStyle().color != RGB(48, 164, 108));
    session.SetTool(Tool::Pen);
    CHECK(session.CurrentStyle().color == RGB(48, 164, 108));
    CHECK(session.CurrentStyle().size == 12);
    CHECK(session.styles()[static_cast<size_t>(Tool::Pen)].size == 12);
}

// --- Composition and export ---------------------------------------------------------------------

TEST(CensorCoversWholePixelsOpaquely) {
    const BgraImage screen = Pattern(300, 200);
    Annotation censor{Kind::Censor, {{40.4f, 30.6f}, {120.2f, 90.5f}}, RGB(0, 0, 0)};
    const BgraImage out = Compose(screen.pixels.data(), screen.width, screen.height, {0, 0, 300, 200}, {censor});
    for (int y = 30; y < 91; ++y) {
        for (int x = 40; x < 121; ++x) CHECK(PixelAt(out, x, y) == RGB(0, 0, 0));
    }
    CHECK(PixelAt(out, 39, 50) == PixelAt(screen, 39, 50));
    for (size_t i = 3; i < out.pixels.size(); i += 4) CHECK(out.pixels[i] == 255);
}

TEST(MarkerDoesNotDarkenWhereItCrossesItself) {
    BgraImage white{200, 200, std::vector<uint8_t>(200 * 200 * 4, 255)};
    Annotation marker{Kind::Marker, {{20, 100}, {180, 100}, {100, 20}, {100, 180}}, RGB(255, 213, 0), 18};
    const BgraImage out = Compose(white.pixels.data(), 200, 200, {0, 0, 200, 200}, {marker});
    CHECK(PixelAt(out, 100, 100) == PixelAt(out, 60, 100));
    CHECK(PixelAt(out, 60, 100) != RGB(255, 255, 255));
}

TEST(AnnotationsStayOnTheContentWhenTheRegionMoves) {
    const BgraImage screen = Pattern(400, 300);
    Annotation box{Kind::Censor, {{150, 100}, {170, 120}}, RGB(255, 0, 0)};
    const BgraImage first = Compose(screen.pixels.data(), 400, 300, {100, 50, 300, 250}, {box});
    const BgraImage moved = Compose(screen.pixels.data(), 400, 300, {140, 90, 340, 290}, {box});
    CHECK(PixelAt(first, 55, 55) == RGB(255, 0, 0));
    CHECK(PixelAt(moved, 15, 15) == RGB(255, 0, 0));
    // Only what lies inside the region is exported.
    const BgraImage cut = Compose(screen.pixels.data(), 400, 300, {160, 110, 260, 210}, {box});
    CHECK(cut.width == 100 && PixelAt(cut, 5, 5) == RGB(255, 0, 0) && PixelAt(cut, 15, 15) == PixelAt(screen, 175, 125));
}

TEST(SavedFileMatchesTheComposedImage) {
    const BgraImage screen = Pattern(320, 240);
    std::vector<Annotation> annotations = {
        {Kind::Arrow, {{20, 20}, {200, 150}}, RGB(229, 72, 77), 4},
        {Kind::Ellipse, {{50, 60}, {150, 120}}, RGB(0, 144, 255), 7},
        {Kind::Number, {{250, 50}}, RGB(229, 72, 77), 16, false, L"", 1},
    };
    Annotation text{Kind::Text, {{30, 160}}, RGB(255, 255, 255), 24, true, L"Note"};
    text.textExtent = MeasureTextExtent(text);
    annotations.push_back(text);
    const BgraImage composed = Compose(screen.pixels.data(), 320, 240, {10, 10, 310, 230}, annotations);

    const fs::path file = TemporaryFolder() / L"same.png";
    CHECK(SaveImage(composed, file, ImageFormat::Png) == ERROR_SUCCESS);
    const auto decoded = DecodeFile(file);
    CHECK(decoded && decoded->width == composed.width && decoded->height == composed.height);
    CHECK(decoded->pixels == composed.pixels);

    const fs::path jpeg = TemporaryFolder() / L"same.jpg";
    CHECK(FormatForPath(jpeg) == ImageFormat::Jpeg);
    CHECK(SaveImage(composed, jpeg, FormatForPath(jpeg)) == ERROR_SUCCESS);
    const auto decodedJpeg = DecodeFile(jpeg);
    CHECK(decodedJpeg && decodedJpeg->width == composed.width);
    CHECK(!fs::exists(fs::path(jpeg).concat(L".part")));
}

TEST(ClipboardGetsTheComposedImage) {
    const BgraImage screen = Pattern(64, 48);
    const Annotation censor{Kind::Censor, {{10, 10}, {20, 20}}, RGB(0, 0, 255)};
    const BgraImage composed = Compose(screen.pixels.data(), 64, 48, {0, 0, 64, 48}, {censor});
    if (!SetClipboardImage(nullptr, composed)) SKIP("clipboard busy");
    CHECK(OpenClipboard(nullptr));
    bool same = false;
    if (HANDLE data = GetClipboardData(CF_DIB)) {
        const auto* header = static_cast<const BITMAPINFOHEADER*>(GlobalLock(data));
        const auto* bits = reinterpret_cast<const uint8_t*>(header + 1);
        same = header->biWidth == 64 && header->biHeight == 48;
        for (int y = 0; y < 48 && same; ++y) {
            same = std::memcmp(bits + (47 - y) * 64 * 4, &composed.pixels[static_cast<size_t>(y) * 64 * 4], 64 * 4) == 0;
        }
        GlobalUnlock(data);
    }
    const bool hasPng = IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) != FALSE;
    EmptyClipboard();
    CloseClipboard();
    CHECK(same);
    CHECK(hasPng);
}

TEST(FailedSaveLeavesNothingBehind) {
    const BgraImage screen = Pattern(16, 16);
    const fs::path missing = TemporaryFolder() / L"no such folder" / L"x.png";
    CHECK(SaveImage(screen, missing, ImageFormat::Png) != ERROR_SUCCESS);
    CHECK(!fs::exists(missing));
}

TEST(QuickSaveNamesNeverOverwrite) {
    std::tm time{};
    time.tm_year = 126;
    time.tm_mon = 10;
    time.tm_mday = 11;
    time.tm_hour = 9;
    time.tm_min = 5;
    time.tm_sec = 7;
    CHECK(CaptureFileName(time, L".png") == L"FlashShot 2026-11-11 09-05-07.png");
    const fs::path folder = TemporaryFolder() / L"unique";
    fs::remove_all(folder);
    fs::create_directories(folder);
    const fs::path first = UniquePath(folder, L"a.png");
    CHECK(first.filename() == L"a.png");
    std::ofstream(first).put('x');
    CHECK(UniquePath(folder, L"a.png").filename() == L"a (2).png");
}

// --- Settings and shortcuts ------------------------------------------------------------------------------

TEST(PrintScreenIsAValidShortcutOnItsOwn) {
    CHECK((Hotkey{0, VK_SNAPSHOT}.IsValid()));
    CHECK(!(Hotkey{0, 'A'}.IsValid()));
    CHECK((Hotkey{MOD_CONTROL | MOD_SHIFT, 'A'}.IsValid()));
    CHECK(Settings{}.captureHotkey == (Hotkey{0, VK_SNAPSHOT}));
    CHECK(ToString(Hotkey{0, VK_SNAPSHOT}) == L"Print Screen");
    CHECK(ToString(Hotkey{MOD_CONTROL | MOD_SHIFT, 'S'}) == L"Ctrl + Shift + S");
}

TEST(SettingsRoundTrip) {
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
    Settings settings;
    settings.captureHotkey = {MOD_CONTROL | MOD_SHIFT, 'S'};
    settings.saveFolder = L"C:\\Captures";
    settings.styles[static_cast<size_t>(Tool::Pen)] = {RGB(1, 2, 3), 12, false};
    settings.styles[static_cast<size_t>(Tool::Text)] = {RGB(4, 5, 6), 36, true};
    settings.shapeTool = Tool::Ellipse;
    settings.drawTool = Tool::Marker;
    CHECK(settings.Save(kTestSettingsKey));
    const Settings loaded = Settings::Load(kTestSettingsKey);
    CHECK(loaded.captureHotkey == settings.captureHotkey);
    CHECK(loaded.saveFolder == settings.saveFolder);
    CHECK(loaded.styles == settings.styles);
    CHECK(loaded.shapeTool == Tool::Ellipse && loaded.drawTool == Tool::Marker);
    CHECK(ConsumeFirstRun(kTestSettingsKey));
    CHECK(!ConsumeFirstRun(kTestSettingsKey));
    RegDeleteTreeW(HKEY_CURRENT_USER, kTestSettingsKey);
}

// --- Screen capture -----------------------------------------------------------------------------------

TEST(SnapshotCoversEveryMonitor) {
    const auto snapshot = ScreenSnapshot::Capture();
    if (!snapshot) SKIP("no desktop to capture");
    CHECK(snapshot->width() == GetSystemMetrics(SM_CXVIRTUALSCREEN));
    CHECK(snapshot->height() == GetSystemMetrics(SM_CYVIRTUALSCREEN));
    const auto monitors = ListMonitors(snapshot->bounds());
    CHECK(!monitors.empty());
    for (const MonitorArea& monitor : monitors) {
        CHECK(monitor.bounds.left >= 0 && monitor.bounds.top >= 0);
        CHECK(monitor.bounds.right <= snapshot->width() && monitor.bounds.bottom <= snapshot->height());
        CHECK(monitor.dpi >= 96);
    }
}

// --- Application lifecycle --------------------------------------------------------------------------------

namespace {

HWND FindTrayWindow() { return FindWindowW(L"FlashShotScreenCaptureTray", nullptr); }

HANDLE StartApp(const wchar_t* arguments) {
    std::wstring command = L"\"" + (buildDir / L"FlashShot.exe").wstring() + L"\" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process);
    if (process.hThread) CloseHandle(process.hThread);
    return process.hProcess;
}

template <class Condition>
bool WaitFor(Condition condition, int milliseconds = 5000) {
    for (int waited = 0; waited < milliseconds; waited += 50) {
        if (condition()) return true;
        Sleep(50);
    }
    return condition();
}

int CountOverlays() {
    int count = 0;
    EnumWindows([](HWND window, LPARAM data) {
        wchar_t name[64]{};
        GetClassNameW(window, name, 64);
        if (wcscmp(name, L"FlashShotOverlay") == 0 && IsWindowVisible(window)) ++*reinterpret_cast<int*>(data);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&count));
    return count;
}

constexpr UINT WM_APP_NEW_CAPTURE = WM_APP + 20;

}  // namespace

TEST(RepeatedCapturesLeaveNoWindowsBehind) {
    if (!fs::exists(buildDir / L"FlashShot.exe")) SKIP("FlashShot.exe not built");
    if (FindTrayWindow()) SKIP("FlashShot is already running");
    HANDLE process = StartApp(L"--background");
    CHECK(process);
    const HWND tray = [] {
        HWND found = nullptr;
        WaitFor([&] { return (found = FindTrayWindow()) != nullptr; });
        return found;
    }();
    CHECK(tray);
    for (int round = 0; round < 3; ++round) {
        PostMessageW(tray, WM_APP_NEW_CAPTURE, 0, 0);
        CHECK(WaitFor([] { return CountOverlays() == 1; }));
        // A second request while a capture is open is ignored.
        PostMessageW(tray, WM_APP_NEW_CAPTURE, 0, 0);
        Sleep(200);
        CHECK(CountOverlays() == 1);
        HWND overlay = FindWindowW(L"FlashShotOverlay", nullptr);
        PostMessageW(overlay, WM_KEYDOWN, VK_ESCAPE, 0);
        CHECK(WaitFor([] { return CountOverlays() == 0; }));
    }
    // A second launch does not start a second copy.
    HANDLE second = StartApp(L"--background");
    CHECK(WaitForSingleObject(second, 5000) == WAIT_OBJECT_0);
    CloseHandle(second);

    PostMessageW(tray, WM_CLOSE, 0, 0);
    CHECK(WaitForSingleObject(process, 5000) == WAIT_OBJECT_0);
    CloseHandle(process);
}

int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    // Capture coordinates are physical pixels, as in FlashShot.exe.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    buildDir = argc > 1 ? fs::path(argv[1]) : fs::path(argv[0]).parent_path();
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 2;
    GdiplusSession gdiplus;

    int passed = 0, failed = 0, skipped = 0;
    for (const auto& test : Tests()) {
        try {
            test.run();
            ++passed;
            std::cout << "PASS " << test.name << "\n";
        } catch (const Skip& skip) {
            ++skipped;
            std::cout << "SKIP " << test.name << ": " << skip.reason << "\n";
        } catch (const Failure& failure) {
            ++failed;
            std::cout << "FAIL " << test.name << ": " << failure.message << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "FAIL " << test.name << ": exception: " << e.what() << "\n";
        }
    }
    std::error_code error;
    fs::remove_all(TemporaryFolder(), error);
    std::cout << "\n" << passed << " passed, " << failed << " failed, " << skipped << " skipped\n";
    CoUninitialize();
    return failed == 0 ? 0 : 1;
}
