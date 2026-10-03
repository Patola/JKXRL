#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#ifdef JK2_MODE
#include "../codeJK2/game/g_local.h"
#else
#include "../code/game/g_local.h"
#endif
#include "../code/qcommon/ojk_i_saved_game.h"
#include "../code/qcommon/ojk_saved_game_helper.h"

// Exercise the real GNPC serializers without a running server or a save file.
class MemorySave final : public ojk::ISavedGame
{
public:
    std::vector<unsigned char> bytes;
    size_t offset = 0;
    bool failed = false;
    bool reading = false;

    bool read(void* destination, int size) override
    {
        if (size < 0 || static_cast<size_t>(size) > bytes.size() - offset)
        {
            failed = true;
            return false;
        }
        std::memcpy(destination, bytes.data() + offset, size);
        offset += size;
        return true;
    }
    bool write(const void* source, int size) override
    {
        if (size < 0) { failed = true; return false; }
        bytes.resize(offset + size);
        std::memcpy(bytes.data() + offset, source, size);
        offset += size;
        return true;
    }
    bool is_all_data_read() const override { return offset == bytes.size(); }
    void ensure_all_data_read() override
    {
        if (!is_all_data_read()) throw_error();
    }
    const void* get_buffer_data() const override { return bytes.data(); }
    int get_buffer_size() const override { return static_cast<int>(bytes.size()); }
    void reset_buffer() override { bytes.clear(); offset = 0; reading = false; }
    void reset_buffer_offset() override { offset = 0; }
    bool is_failed() const override { return failed; }
    void clear_error() override { failed = false; }
    void throw_error() override { throw std::runtime_error("NPC save buffer failure"); }
    bool read_chunk(uint32_t) override { throw std::logic_error("Unexpected chunk read"); }
    bool write_chunk(uint32_t) override { throw std::logic_error("Unexpected chunk write"); }
    bool skip(int count) override
    {
        if (count < 0 || (reading && static_cast<size_t>(count) > bytes.size() - offset))
        {
            failed = true;
            return false;
        }
        offset += count;
        if (!reading) bytes.resize(offset);
        return true;
    }
    void save_buffer() override { throw std::logic_error("Unexpected buffer save"); }
    void load_buffer() override { throw std::logic_error("Unexpected buffer load"); }
};

int main()
{
    try
    {
        for (const intptr_t index : { -1, 0, 1, 137, MAX_GENTITIES - 1 })
        {
            // EnumerateFields encodes entity pointers before sg_export. Preserve
            // these indices until EvaluateFields applies F_GENTITY after import.
            gNPC_t source{};
            source.watchTarget = reinterpret_cast<gentity_t*>(index);
            source.behaviorState = BS_CINEMATIC;
            source.desiredYaw = 123.5f;
            source.lockedDesiredYaw = 234.5f;
            source.scriptFlags = 0x1234;
            source.enemyLaggedPos[0][0] = 456.0f;
            source.ffireCount = 7;
            source.ffireDebounce = 890;
            source.ffireFadeDebounce = 1234;

            MemorySave buffer;
            ojk::SavedGameHelper helper(&buffer);
            source.sg_export(helper);
            buffer.reset_buffer_offset();
            buffer.reading = true;
            gNPC_t restored{};
            restored.sg_import(helper);
            buffer.ensure_all_data_read();
            if (reinterpret_cast<intptr_t>(restored.watchTarget) != index)
            {
                std::cerr << "watchTarget index " << index << " changed to "
                          << reinterpret_cast<intptr_t>(restored.watchTarget) << '\n';
                return 1;
            }
            if (restored.behaviorState != source.behaviorState ||
                restored.desiredYaw != source.desiredYaw ||
                restored.lockedDesiredYaw != source.lockedDesiredYaw ||
                restored.scriptFlags != source.scriptFlags ||
                restored.enemyLaggedPos[0][0] != source.enemyLaggedPos[0][0] ||
                restored.ffireCount != source.ffireCount ||
                restored.ffireDebounce != source.ffireDebounce ||
                restored.ffireFadeDebounce != source.ffireFadeDebounce)
                throw std::runtime_error("NPC state changed during load");

            MemorySave roundTrip;
            ojk::SavedGameHelper writer(&roundTrip);
            restored.sg_export(writer);
            if (buffer.bytes != roundTrip.bytes)
                throw std::runtime_error("GNPC serialized bytes changed on re-save");
        }
        std::cout << "NPC save round trips preserve null, player and other watch targets\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
