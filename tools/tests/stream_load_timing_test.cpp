#include "rendering/stream_load_timing.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace wowee::rendering;

int main() {
    StreamLoadTiming measured(1000);
    measured.switchTo(StreamLoadStage::Geometry, 1200);
    measured.switchTo(StreamLoadStage::NormalGenerate, 1700);
    measured.switchTo(StreamLoadStage::Geometry, 1800);
    measured.switchTo(StreamLoadStage::Metadata, 1900);
    measured.switchTo(StreamLoadStage::Metadata, 1950);
    assert(measured.us[static_cast<unsigned>(StreamLoadStage::Geometry)] == 600);
    assert(measured.us[static_cast<unsigned>(StreamLoadStage::NormalGenerate)] == 100);
    assert(measured.us[static_cast<unsigned>(StreamLoadStage::Metadata)] == 250);
    assert(measured.totalUs() == 950); // no nested double-counting

    StreamLoadTiming nested(streamLoadNowUs());
    {
        StreamLoadStageScope outer(&nested, StreamLoadStage::TextureLookup);
        try {
            StreamLoadStageScope inner(&nested, StreamLoadStage::NormalUpload);
            assert(nested.stage == StreamLoadStage::NormalUpload);
            throw std::runtime_error("allocation failure surrogate");
        } catch (const std::runtime_error&) {}
        assert(nested.stage == StreamLoadStage::TextureLookup);
    }
    assert(nested.stage == StreamLoadStage::Metadata);
    { StreamLoadStageScope disabled(nullptr, StreamLoadStage::Geometry); }

    StreamLoadLogGate gate;
    for (int i = 0; i < 4; ++i) assert(gate.allow(10000000 + i));
    for (int i = 0; i < 100; ++i) assert(!gate.allow(10000004 + i));
    assert(gate.suppressed == 100 && gate.emitted == 4);
    assert(!gate.allow(14999999));
    assert(gate.allow(15000000));
    assert(gate.suppressed == 101); // caller logs and resets it, gate cannot hide events
    assert(gate.emitted == 1);
    std::cout << "PASS exclusive stages, exception restoration, disabled scopes, bounded reporting\n";
}
