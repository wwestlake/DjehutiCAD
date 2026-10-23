#pragma once

#include <array>
#include <cstdint>
#include <mutex>

struct DjehutiMat4 { float v[16] {}; };
struct DjehutiFramePacket
{
    DjehutiMat4 model, view, projection;
    std::uint64_t serial = 0;
    float angle = 0.0f;
    float nearPlane = 0.01f, farPlane = 1000.0f;
    bool floorVisible = true;
};

// Three bounded application-owned frame resources. A slot remains in-flight
// until its GL fence signals; JUCE/GL presentation buffers are separate.
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

    bool consumeNewest(DjehutiFramePacket& packet, int& slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        int newest = -1;
        for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            if (slots[static_cast<size_t>(i)].state == State::ready
                && (newest < 0 || slots[static_cast<size_t>(i)].packet.serial
                                  > slots[static_cast<size_t>(newest)].packet.serial))
                newest = i;
        if (newest < 0) return false;
        for (int i = 0; i < static_cast<int>(slots.size()); ++i)
            if (i != newest && slots[static_cast<size_t>(i)].state == State::ready)
                slots[static_cast<size_t>(i)].state = State::free;
        slots[static_cast<size_t>(newest)].state = State::inFlight;
        packet = slots[static_cast<size_t>(newest)].packet;
        slotIndex = newest;
        return true;
    }

    void complete(int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (slotIndex >= 0 && slotIndex < static_cast<int>(slots.size())
            && slots[static_cast<size_t>(slotIndex)].state == State::inFlight)
            slots[static_cast<size_t>(slotIndex)].state = State::free;
    }

    void reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& slot : slots)
            slot.state = State::free;
    }

private:
    enum class State { free, ready, inFlight };
    struct Slot { DjehutiFramePacket packet; State state = State::free; };
    std::array<Slot, 3> slots {};
    std::mutex mutex;
};
