// The plugins' editor: shows the plugin's view scaled, as WindowScale.h describes. From shnolk/monomodule
// src/plugin/ScaledEditor.h (AGPL-3.0).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "WindowScale.h"

namespace md::plugin::scale {

// A plugin editor showing a view scaled. It opens at the shared scale; the host, or the view (through
// getConstrainer(), as the editor's corner grip does), resizes it with its aspect ratio locked.
class ScaledEditor : public juce::AudioProcessorEditor {
public:
    ScaledEditor(juce::AudioProcessor& p, std::unique_ptr<juce::Component> view) : AudioProcessorEditor(p), m_holder(std::move(view))
    {
        addAndMakeVisible(m_holder);
        m_constrainer.setBase(m_holder.base());
        setConstrainer(&m_constrainer);
        setResizable(true, false);
        m_holder.onBaseChanged = [this] {
            const double s = m_holder.scale();
            m_constrainer.setBase(m_holder.base());
            const auto size = scaled(m_holder.base(), s);
            setSize(size.x, size.y);
        };
        const auto size = scaled(m_holder.base(), openScale(load(), m_holder.base(), screenArea(true)));
        setSize(size.x, size.y);
    }
    juce::Component& view() const { return m_holder.view(); }
    juce::Point<int> baseSize() const { return m_holder.base(); }
    void resized() override { m_holder.setBounds(getLocalBounds()); }

private:
    AspectConstrainer m_constrainer;
    ScaledHolder m_holder;
};

} // namespace md::plugin::scale
