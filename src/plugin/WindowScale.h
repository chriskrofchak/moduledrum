// Window scaling. From shnolk/monomodule src/plugin/WindowScale.h (AGPL-3.0). Every Moduledrum window keeps the layout
// it was designed at, its base size, and shows it scaled as a whole: the window resizes with its aspect ratio locked,
// so no layout has to adapt to another shape. One scale is shared by all of them: the last scale a window was resized
// to is kept in the shared settings (uiScale), and every window opens at it, or smaller where it would not fit the
// screen (openScale). The plugins' editor is in ScaledEditor.h.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <functional>
#include <memory>
#include "SharedSettings.h"

namespace md::plugin::scale {

constexpr double kMin = 0.5, kMax = 2.0;

inline double load()   // 1 until a window has been resized
{
    const double s = loadSharedSetting("uiScale", "1").getDoubleValue();
    return s > 0.0 ? juce::jlimit(kMin, kMax, s) : 1.0;
}
inline void save(double s) { saveSharedSetting("uiScale", juce::String(juce::jlimit(kMin, kMax, s), 3)); }

inline juce::Point<int> scaled(juce::Point<int> base, double s) { return {int(std::lround(base.x * s)), int(std::lround(base.y * s))}; }

// The work area of the screen a new window appears on: the one under the mouse (a host opens a plugin's window where
// the user is working), else the main one.
inline juce::Rectangle<int> screenArea(bool underMouse)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* d = underMouse ? displays.getDisplayForPoint(juce::Desktop::getMousePosition()) : nullptr;
    if (d == nullptr) d = displays.getPrimaryDisplay();
    return d != nullptr ? d->userArea : juce::Rectangle<int>();
}

// The scale a window of `base` size opens at on a screen with work area `area`: s if it fits there with room left for
// what surrounds it (a title bar, a host's plugin header), otherwise the largest scale that keeps it inside kOpenWithin
// of the area in both directions. Never more than s, never below kMin. The result is not stored: on a larger screen
// the window opens at s again.
constexpr double kFitsWidth = 0.95, kFitsHeight = 0.85, kOpenWithin = 0.8;
inline double openScale(double s, juce::Point<int> base, juce::Rectangle<int> area)
{
    if (area.isEmpty() || (base.x * s <= kFitsWidth * area.getWidth() && base.y * s <= kFitsHeight * area.getHeight())) return s;
    return juce::jlimit(kMin, s, kOpenWithin * juce::jmin(area.getWidth() / double(base.x), area.getHeight() / double(base.y)));
}

// A window's size: its base size's aspect ratio, from kMin to kMax times the base size.
class AspectConstrainer : public juce::ComponentBoundsConstrainer {
public:
    void setBase(juce::Point<int> base)
    {
        const auto lo = scaled(base, kMin), hi = scaled(base, kMax);
        setSizeLimits(lo.x, lo.y, hi.x, hi.y);
        setFixedAspectRatio(double(base.x) / double(base.y));
    }
};

// Shows one component, the view, laid out at its own size (the base size) and scaled to fill this one. The view may
// change its own size (a later layout with more pages): onBaseChanged then lets the owner resize, at the same scale.
// A new scale that lasts becomes the shared scale (a resize by the user); the scale a window opens at does not.
class ScaledHolder : public juce::Component, private juce::Timer {
public:
    explicit ScaledHolder(std::unique_ptr<juce::Component> view) : m_view(std::move(view)), m_base(m_view->getWidth(), m_view->getHeight())
    {
        addAndMakeVisible(*m_view);
    }
    ~ScaledHolder() override { if (isTimerRunning()) timerCallback(); }

    juce::Component& view() const { return *m_view; }
    juce::Point<int> base() const { return m_base; }
    double scale() const { return getWidth() / double(m_base.x); }
    std::function<void()> onBaseChanged;

    void resized() override
    {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        // each axis fills the window exactly: the two differ by a rounded pixel at most
        m_view->setTransform(juce::AffineTransform::scale(float(getWidth()) / float(m_base.x), float(getHeight()) / float(m_base.y)));
        if (m_saved < 0.0) m_saved = scale();
        else if (std::abs(scale() - m_saved) > 0.001) startTimer(400);   // saved once the resizing has settled
    }
    void childBoundsChanged(juce::Component* child) override
    {
        if (child != m_view.get() || juce::Point<int>(m_view->getWidth(), m_view->getHeight()) == m_base) return;   // (setTransform lands here too)
        m_base = {m_view->getWidth(), m_view->getHeight()};
        if (onBaseChanged) onBaseChanged();
        resized();
    }

private:
    void timerCallback() override { stopTimer(); m_saved = scale(); save(m_saved); }

    std::unique_ptr<juce::Component> m_view;
    juce::Point<int> m_base;
    double m_saved = -1.0;   // the scale last saved, or the one the window opened at
};

} // namespace md::plugin::scale
