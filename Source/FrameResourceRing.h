#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

struct DjehutiFramePacket { float angle = 0.0f; std::uint64_t serial = 0; };

// Exactly three bounded application-owned frame packets. The producer owns a
// writing slot, the GL callback owns a consuming slot, and the mutex protects
// publication/release. GPU swap buffers remain JUCE's responsibility.
class DjehutiFrameResourceRing
{
public:
    bool publish(const DjehutiFramePacket& packet)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& slot : slots)
            if (slot.state == State::free)
            {
                slot.packet = packet; slot.state = State::ready; return true;
            }
        return false;
    }

    bool consume(DjehutiFramePacket& packet)
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& slot : slots)
            if (slot.state == State::ready)
            {
                slot.state = State::consuming; packet = slot.packet; slot.state = State::free; return true;
            }
        return false;
    }

private:
    enum class State { free, ready, consuming };
    struct Slot { DjehutiFramePacket packet; State state = State::free; };
    std::array<Slot, 3> slots {};
    std::mutex mutex;
};
