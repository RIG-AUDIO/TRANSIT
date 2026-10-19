// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

// CI GUI smoke/screenshot harness (review C2). Runs under Xvfb with a real
// windowed peer and a serviced message loop, drives deterministic UI states
// (empty / loaded / zoom-4s / whole / playing / FX page) at four viewport
// sizes, writes one PNG per state+viewport, and fails the job on any missing,
// undecodable, wrong-sized, or all-dark/transparent frame.
//
// Usage: GuiSmoke <output-dir>
//
// Phase 1 checks (implemented here): file exists, PNG re-decodes, exact
// dimensions, non-black/non-transparent content. Phase 2 intersection and
// tab-reachability checks are documented in docs/development/CI_VISUAL_TESTING.md
// as planned, not claimed.
#include <juce_gui_extra/juce_gui_extra.h>

#include "../Source/MainComponent.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if JUCE_LINUX
 #include <X11/Xlib.h>
 #include <execinfo.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// X11 error handling + atom seeding (bare-server fixes).
//
// (1) Handler: JUCE installs its own error-swallowing X handler only when
//     JUCEApplicationBase::isStandaloneApp() — i.e. only for apps created
//     via START_JUCE_APPLICATION (createInstance != nullptr). This console
//     harness has no JUCEApplication, so JUCE never installs it and the
//     Xlib default handler stays active: any X error aborts the process
//     with exit 1. The real TRANSIT app instead swallows stray WM-protocol
//     errors silently. We mirror the app: install our own handler first
//     (JUCE will not touch it in this process) so a stray error degrades to
//     a diagnostic line instead of killing the job.
//
// (2) Atom seeding: JUCE's juce_XWindowSystem_linux.cpp interns a set of
//     WM/ICCCM/EWMH atoms with XInternAtom(..., only_if_exists = true)
//     (XWindowSystemUtilities::Atoms and getIfExists call sites), then
//     passes atoms.protocols ("WM_PROTOCOLS") to XChangeProperty unchecked
//     at window creation. On a desktop some other client has already
//     created those atoms, but on a bare Xvfb server (no window manager,
//     no prior clients) they do not exist yet: the lookup returns 0 and the
//     write raises BadAtom. Seeding every such name with
//     XInternAtom(..., False) on a short-lived connection makes them exist
//     for JUCE's own connection (atoms are server-global and persist).
// ---------------------------------------------------------------------------
#if JUCE_LINUX
int gXErrorsSeen = 0;

int smokeXErrorHandler(Display* dpy, XErrorEvent* ev) {
    static bool inHandler = false; // guards XGetAtomName against recursion
    char text[128] = { 0 };
    XGetErrorText(dpy, ev->error_code, text, static_cast<int>(sizeof(text)));
    char atomName[128] = "n/a";
    if (!inHandler && ev->error_code == BadAtom && ev->resourceid != 0) {
        inHandler = true;
        if (char* an =
                XGetAtomName(dpy, static_cast<Atom>(ev->resourceid))) {
            std::snprintf(atomName, sizeof(atomName), "%s", an);
            XFree(an);
        }
        inHandler = false;
    }
    if (gXErrorsSeen < 20) {
        std::fprintf(stderr,
                     "GUI_SMOKE X error (swallowed): %s code=%d req=%d "
                     "minor=%d serial=%lu resource=0x%lx atom-name=%s\n",
                     text, static_cast<int>(ev->error_code),
                     static_cast<int>(ev->request_code),
                     static_cast<int>(ev->minor_code),
                     static_cast<unsigned long>(ev->serial),
                     static_cast<unsigned long>(ev->resourceid), atomName);
        void* frames[32];
        const int n = backtrace(frames, 32);
        backtrace_symbols_fd(frames, n, 2); // stderr, avoids fileno()
    } else if (gXErrorsSeen == 20) {
        std::fprintf(stderr,
                     "GUI_SMOKE X errors: further messages suppressed\n");
    }
    ++gXErrorsSeen;
    return 0; // swallow, like the real app does
}

int smokeXIOErrorHandler(Display*) {
    std::fprintf(stderr, "GUI_SMOKE X IO error: connection lost\n");
    std::fflush(stderr);
    std::_Exit(1);
}

void installX11ErrorHandlers() {
    XSetErrorHandler(smokeXErrorHandler);
    XSetIOErrorHandler(smokeXIOErrorHandler);
}

void seedX11Atoms() {
    static const char* const kSeedNames[] = {
        // Atoms ctor in juce_XWindowSystem_linux.cpp (getIfExists)
        "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING",
        "WM_CHANGE_STATE", "WM_STATE", "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
        "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ",
        "_NET_WM_STATE_MAXIMIZED_VERT",
        // getIfExists call sites
        "_NET_WM_STATE_FULLSCREEN", "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS",
        "_NET_WORKAREA", "_MOTIF_WM_HINTS", "_WIN_HINTS", "KWM_WIN_DECORATION",
        "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", "_NET_WM_ALLOWED_ACTIONS",
        "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_NORMAL",
        // addAtomIfExists call sites
        "_NET_WM_ACTION_RESIZE", "_NET_WM_ACTION_FULLSCREEN",
        "_NET_WM_ACTION_MINIMIZE", "_NET_WM_ACTION_CLOSE",
        "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_ABOVE",
        // Xlib WMProps lookups also use only-if-exists semantics
        "WM_NORMAL_HINTS", "WM_SIZE_HINTS",
    };
    Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        std::fprintf(stderr, "GUI_SMOKE seed: XOpenDisplay FAILED\n");
        return;
    }
    int nonZero = 0;
    for (const char* name : kSeedNames) {
        if (XInternAtom(dpy, name, False) != 0)
            ++nonZero;
    }
    const Atom check = XInternAtom(dpy, "WM_PROTOCOLS", False);
    std::fprintf(stderr, "GUI_SMOKE seed: display ok, %d/%zu atoms, "
                         "WM_PROTOCOLS=%lu\n",
                 nonZero,
                 sizeof(kSeedNames) / sizeof(kSeedNames[0]),
                 static_cast<unsigned long>(check));
    XCloseDisplay(dpy);
}
#else
void installX11ErrorHandlers() {}
void seedX11Atoms() {}
#endif

struct Viewport {
    int w;
    int h;
};

constexpr Viewport kViewports[] = { { 1100, 824 }, { 1280, 800 },
                                    { 1440, 900 }, { 1920, 1080 } };

constexpr int kReadyState = static_cast<int>(lean::LoadState::Ready);
constexpr int kErrorState = static_cast<int>(lean::LoadState::Error);
constexpr int kLoadTimeoutMs = 30000;

int gFailures = 0;

void fail(const juce::String& what) {
    std::printf("GUI_SMOKE FAIL: %s\n", what.toRawUTF8());
    ++gFailures;
}

void pump(int ms) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

// ---------------------------------------------------------------------------
// Synthetic test tone (8 s, 48 kHz stereo PCM16): sine + beat-decay envelope,
// generated at test time — no audio fixture is ever committed to the repo.
// Hand-rolled RIFF so we don't depend on WavAudioFormat stream ownership.
// ---------------------------------------------------------------------------
void putU16(std::vector<unsigned char>& b, std::uint16_t v) {
    const std::uint32_t u = v; // widen unsigned->unsigned (no sign mixing)
    b.push_back(static_cast<unsigned char>(u & 0xffu));
    b.push_back(static_cast<unsigned char>((u >> 8) & 0xffu));
}

void putU32(std::vector<unsigned char>& b, std::uint32_t v) {
    b.push_back(static_cast<unsigned char>(v & 0xffu));
    b.push_back(static_cast<unsigned char>((v >> 8) & 0xffu));
    b.push_back(static_cast<unsigned char>((v >> 16) & 0xffu));
    b.push_back(static_cast<unsigned char>((v >> 24) & 0xffu));
}

void tag(std::vector<unsigned char>& b, const char* s) {
    for (const char* p = s; *p != '\0'; ++p)
        b.push_back(static_cast<unsigned char>(*p));
}

bool writeToneWav(const juce::File& file, double freqHz) {
    const double rate = 48000.0;
    const double seconds = 8.0;
    const auto frames = static_cast<std::uint32_t>(rate * seconds);
    const double pi = 3.14159265358979323846;
    const double beatPeriod = 0.5;

    std::vector<unsigned char> b;
    b.reserve(static_cast<std::size_t>(44u + frames * 4u));
    tag(b, "RIFF");
    putU32(b, 36u + frames * 4u);
    tag(b, "WAVE");
    tag(b, "fmt ");
    putU32(b, 16u);
    putU16(b, static_cast<std::uint16_t>(1u));  // PCM
    putU16(b, static_cast<std::uint16_t>(2u));  // stereo
    putU32(b, static_cast<std::uint32_t>(rate));
    putU32(b, static_cast<std::uint32_t>(rate) * 4u); // byte rate
    putU16(b, static_cast<std::uint16_t>(4u));  // block align
    putU16(b, static_cast<std::uint16_t>(16u)); // bits per sample
    tag(b, "data");
    putU32(b, frames * 4u);

    for (std::uint32_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / rate;
        const double phase = std::fmod(t, beatPeriod);
        const double env = 0.35 + 0.65 * std::exp(-4.0 * phase);
        double s = std::sin(2.0 * pi * freqHz * t) * 0.55 * env;
        s = s > 1.0 ? 1.0 : (s < -1.0 ? -1.0 : s);
        const auto v =
            static_cast<std::int16_t>(std::lround(s * 32000.0));
        const auto bits = static_cast<std::uint16_t>(v); // PCM is bit-identical
        putU16(b, bits);
        putU16(b, bits);
    }
    return file.replaceWithData(b.data(), b.size());
}

// ---------------------------------------------------------------------------
// Real windowed shell: chromeless DocumentWindow like the app (Main.cpp),
// content sized exactly to the viewport so snapshots have known dimensions.
// ---------------------------------------------------------------------------
class SmokeWindow : public juce::DocumentWindow {
public:
    SmokeWindow()
        : DocumentWindow("TRANSIT-smoke", juce::Colour(0xff0b0d10),
                         DocumentWindow::allButtons) {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setResizable(false, false);
        auto* mc = new MainComponent();
        content_ = mc;
        setContentOwned(mc, true);
    }

    void closeButtonPressed() override {}

    MainComponent& content() { return *content_; }

    void useViewport(int w, int h) {
        setSize(w, h);
        content_->setSize(w, h);
        setVisible(true);
        pump(400); // let layout + timer repaints settle before snapshotting
    }

private:
    MainComponent* content_ = nullptr;
};

// ---------------------------------------------------------------------------
// Snapshot + phase-1 validation.
// ---------------------------------------------------------------------------
void snapshot(MainComponent& content, const juce::File& dir,
              const juce::String& state, int w, int h) {
    const juce::String name =
        juce::String(state) + "-" + juce::String(w) + "x" + juce::String(h);
    const juce::File out = dir.getChildFile(name + ".png");

    const juce::Image img =
        content.createComponentSnapshot(content.getLocalBounds(), true, 1.0f);
    if (!img.isValid()) {
        fail(name + ": snapshot image invalid");
        return;
    }
    {
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        if (!stream.openedOk() || !png.writeImageToStream(img, stream)) {
            fail(name + ": PNG write failed");
            return;
        }
        stream.flush();
    }

    if (!out.existsAsFile() || out.getSize() <= 0) {
        fail(name + ": PNG missing or empty on disk");
        return;
    }
    const juce::Image back = juce::ImageFileFormat::loadFrom(out);
    if (!back.isValid()) {
        fail(name + ": PNG does not re-decode");
        return;
    }
    if (back.getWidth() != w || back.getHeight() != h) {
        fail(name + ": size " + juce::String(back.getWidth()) + "x" +
             juce::String(back.getHeight()) + " != " + juce::String(w) + "x" +
             juce::String(h));
        return;
    }
    // Bright-pixel fraction: catches all-black and all-transparent frames
    // while tolerating a dark-glass palette. Subsampled 4x4 for speed.
    int bright = 0;
    int total = 0;
    for (int y = 0; y < h; y += 4) {
        for (int x = 0; x < w; x += 4) {
            const juce::Colour c = back.getPixelAt(x, y);
            const int lum = static_cast<int>(c.getRed()) +
                            static_cast<int>(c.getGreen()) +
                            static_cast<int>(c.getBlue());
            if (lum > 96)
                ++bright;
            ++total;
        }
    }
    if (total == 0 || static_cast<float>(bright) < 0.001f *
                                                        static_cast<float>(total)) {
        fail(name + ": frame is all-dark/transparent (bright " +
             juce::String(bright) + "/" + juce::String(total) + ")");
        return;
    }
    std::printf("GUI_SMOKE shot %s\n", name.toRawUTF8());
}

bool waitReady(MainComponent& content, int deck) {
    int waited = 0;
    while (waited < kLoadTimeoutMs) {
        const int st = content.harnessDeckState(deck);
        if (st == kReadyState)
            return true;
        if (st == kErrorState) {
            fail("load error on deck " + juce::String(deck));
            return false;
        }
        pump(50);
        waited += 50;
    }
    fail("load timeout on deck " + juce::String(deck));
    return false;
}

// G5: mirror of waitReady for the deterministic error-state snapshots.
bool waitError(MainComponent& content, int deck) {
    int waited = 0;
    while (waited < kLoadTimeoutMs) {
        if (content.harnessDeckState(deck) == kErrorState)
            return true;
        pump(50);
        waited += 50;
    }
    fail("error timeout on deck " + juce::String(deck));
    return false;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::printf("usage: GuiSmoke <output-dir>\n");
        return 2;
    }

    // Must run before JUCE's XWindowSystem exists (see the block comments):
    // swallow-handler first, then atom seeding.
    installX11ErrorHandlers();
    seedX11Atoms();

    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir =
        juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    if (!outDir.createDirectory().wasOk()) {
        fail("cannot create output directory " + outDir.getFullPathName());
        return 1;
    }

    const juce::File toneA = outDir.getChildFile("tone-a.wav");
    const juce::File toneB = outDir.getChildFile("tone-b.wav");
    if (!writeToneWav(toneA, 440.0) || !writeToneWav(toneB, 220.0)) {
        fail("cannot write synthetic WAV fixtures");
        return 1;
    }

    // S11: window create/destroy cycles (lifecycle regressions, no shots).
    for (int i = 0; i < 3; ++i) {
        SmokeWindow cycle;
        cycle.useViewport(1100, 824);
        pump(150);
    }
    pump(200);

    SmokeWindow window;
    MainComponent& content = window.content();

    // Pass 1: empty state (before any file is loaded).
    for (const Viewport& vp : kViewports) {
        window.useViewport(vp.w, vp.h);
        snapshot(content, outDir, "empty", vp.w, vp.h);
    }

    // G5: deterministic error state — deck A fails on a nonexistent path,
    // deck B stays empty, so one frame shows both chips.
    content.harnessLoadFile(
        0, outDir.getChildFile("missing.wav").getFullPathName());
    if (!waitError(content, 0))
        return 1;
    for (const Viewport& vp : kViewports) {
        window.useViewport(vp.w, vp.h);
        snapshot(content, outDir, "error", vp.w, vp.h);
    }

    content.harnessLoadFile(0, toneA.getFullPathName());
    content.harnessLoadFile(1, toneB.getFullPathName());
    if (!waitReady(content, 0) || !waitReady(content, 1))
        return 1;

    // Pass 2: deterministic state set per viewport.
    for (const Viewport& vp : kViewports) {
        window.useViewport(vp.w, vp.h);
        content.harnessSetMeters(0.72f, 0.55f, 0.88f); // G4: dB fill + hold
        snapshot(content, outDir, "loaded", vp.w, vp.h);

        content.harnessSetZoom(0, 0);
        content.harnessSetZoom(1, 0); // 4 s window
        pump(150);
        snapshot(content, outDir, "zoom4s", vp.w, vp.h);

        content.harnessSetZoom(0, 7);
        content.harnessSetZoom(1, 7); // whole track
        pump(150);
        snapshot(content, outDir, "whole", vp.w, vp.h);

        content.harnessSetZoom(0, 3);
        content.harnessSetZoom(1, 3); // restore 32 s default
        content.harnessPlay(0, true);
        content.harnessPlay(1, true);
        pump(150);
        snapshot(content, outDir, "playing", vp.w, vp.h);
        content.harnessPlay(0, false);
        content.harnessPlay(1, false);

        content.harnessShowFxPage(true);
        pump(150);
        snapshot(content, outDir, "fx", vp.w, vp.h);
        content.harnessShowFxPage(false);
        pump(150);
    }

    if (gFailures != 0) {
        std::printf("GUI_SMOKE FAIL (%d failure(s))\n", gFailures);
        return 1;
    }
    std::printf("GUI_SMOKE ok\n");
    return 0;
}
