#pragma once
#include "Processor.h"

namespace a1 {
class Editor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    Editor(Processor&,const juce::File& resources);
    ~Editor() override;
    void paint(juce::Graphics&) override;
    int getControlParameterIndex(juce::Component&) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    void timerCallback() override;
};
}
