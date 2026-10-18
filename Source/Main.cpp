// TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only

#include <juce_gui_extra/juce_gui_extra.h>

#include "MainComponent.h"

// JUCE application shell. All audio/DSP work lives in AudioEngine and
// lean::Deck; this file only owns the window.
class TransitApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "TRANSIT"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override {}

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colours::darkgrey,
                             DocumentWindow::allButtons) {
            setUsingNativeTitleBar(false);
            setTitleBarHeight(0); // chromeless: the TopBar owns the chrome
            // Fixed layout, no resize handles: the app runs fullscreen like
            // hardware. F11 toggles, Escape leaves fullscreen.
            setResizable(false, false);
            setContentOwned(new MainComponent(), true);
            setVisible(true);
            setFullScreen(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::F11Key) {
                setFullScreen(!isFullScreen());
                return true;
            }
            if (key == juce::KeyPress::escapeKey && isFullScreen()) {
                setFullScreen(false);
                return true;
            }
            return DocumentWindow::keyPressed(key);
        }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(TransitApplication)
