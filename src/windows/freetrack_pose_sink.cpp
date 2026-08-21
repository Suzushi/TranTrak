#include "trackheader/freetrack_pose_sink.h"

#ifndef _WIN32
#error "freetrack_pose_sink.cpp is Windows-only"
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace trackheader {

namespace {

constexpr char kSharedMemoryName[] = "FT_SharedMem";
constexpr char kMutexName[] = "FT_Mutext";
constexpr double kDegreesToRadians = 3.14159265358979323846 / 180.0;

struct FTData
{
    std::uint32_t data_id;
    std::int32_t camera_width;
    std::int32_t camera_height;
    float yaw;
    float pitch;
    float roll;
    float x;
    float y;
    float z;
    float raw_yaw;
    float raw_pitch;
    float raw_roll;
    float raw_x;
    float raw_y;
    float raw_z;
    float x1, y1, x2, y2, x3, y3, x4, y4;
};

struct FTHeap
{
    FTData data;
    std::int32_t game_id;
    union {
        unsigned char table[8];
        std::int32_t table_ints[2];
    };
    std::int32_t game_id2;
};

static_assert(sizeof(FTData) == 92, "FreeTrack FTData layout changed");
static_assert(sizeof(FTHeap) == 108, "FreeTrack FTHeap layout changed");

void store_float(volatile float& place, float value)
{
    union {
        float as_float;
        LONG as_long;
    } bits{};
    bits.as_float = value;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&place), bits.as_long);
}

void store_int(volatile std::int32_t& place, std::int32_t value)
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&place),
                         static_cast<LONG>(value));
}

void store_uint(volatile std::uint32_t& place, std::uint32_t value)
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&place),
                         static_cast<LONG>(value));
}

std::int32_t load_int(const volatile std::int32_t& place)
{
    return static_cast<std::int32_t>(InterlockedCompareExchange(
        const_cast<volatile LONG*>(reinterpret_cast<const volatile LONG*>(&place)),
        0, 0));
}

std::vector<std::string> split_fields(const std::string& line)
{
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (const char character : line) {
        if (character == '"') {
            quoted = !quoted;
        } else if (character == ';' && !quoted) {
            fields.push_back(current);
            current.clear();
        } else {
            current += character;
        }
    }
    fields.push_back(current);
    return fields;
}

bool load_game_data(std::int32_t game_id, const std::string& path,
                    unsigned char table[8], std::string& name)
{
    std::memset(table, 0, 8);
    if (path.empty())
        return false;

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::fprintf(stderr, "freetrack: can't open game list '%s'\n", path.c_str());
        return false;
    }

    const std::string id = std::to_string(game_id);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto fields = split_fields(line);
        if (fields.size() != 8 || fields[6] != id)
            continue;

        name = fields[1];
        if (fields[3] == "V160" || fields[7].size() != 22)
            return true;

        unsigned values[8]{};
        unsigned ignored[3]{};
        const int parsed = std::sscanf(
            fields[7].c_str(),
            "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
            &ignored[2], &ignored[0], &values[3], &values[2], &values[1],
            &values[0], &values[7], &values[6], &values[5], &values[4],
            &ignored[1]);
        if (parsed == 11) {
            for (int index = 0; index < 8; ++index)
                table[index] = static_cast<unsigned char>(values[index]);
        }
        return true;
    }
    return false;
}

std::int32_t pack_table(const unsigned char* table)
{
    const std::uint32_t value =
        static_cast<std::uint32_t>(table[0]) |
        (static_cast<std::uint32_t>(table[1]) << 8) |
        (static_cast<std::uint32_t>(table[2]) << 16) |
        (static_cast<std::uint32_t>(table[3]) << 24);
    return static_cast<std::int32_t>(value);
}

}  // namespace

struct FreetrackPoseSink::Impl
{
    explicit Impl(FreetrackPoseSinkConfig config_in)
        : config(std::move(config_in))
    {
    }

    FreetrackPoseSinkConfig config;
    HANDLE mapping = nullptr;
    HANDLE mutex = nullptr;
    volatile FTHeap* heap = nullptr;
    std::int32_t last_game_id = -1;
    mutable std::mutex name_mutex;
    std::string game_name;
};

FreetrackPoseSink::FreetrackPoseSink(FreetrackPoseSinkConfig config)
    : impl_(std::make_unique<Impl>(std::move(config)))
{
}

FreetrackPoseSink::~FreetrackPoseSink()
{
    close();
}

bool FreetrackPoseSink::open()
{
    close();

    impl_->mutex = CreateMutexA(nullptr, FALSE, kMutexName);
    if (!impl_->mutex) {
        std::fprintf(stderr, "freetrack: CreateMutexA failed (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        return false;
    }

    impl_->mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                        0, static_cast<DWORD>(sizeof(FTHeap)),
                                        kSharedMemoryName);
    if (!impl_->mapping) {
        std::fprintf(stderr, "freetrack: CreateFileMappingA failed (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        close();
        return false;
    }

    impl_->heap = reinterpret_cast<volatile FTHeap*>(MapViewOfFile(
        impl_->mapping, FILE_MAP_WRITE, 0, 0, sizeof(FTHeap)));
    if (!impl_->heap) {
        std::fprintf(stderr, "freetrack: MapViewOfFile failed (%lu)\n",
                     static_cast<unsigned long>(GetLastError()));
        close();
        return false;
    }

    store_uint(impl_->heap->data.data_id, 1);
    store_int(impl_->heap->data.camera_width, 100);
    store_int(impl_->heap->data.camera_height, 250);
    store_int(impl_->heap->game_id2, 0);
    store_int(impl_->heap->table_ints[0], 0);
    store_int(impl_->heap->table_ints[1], 0);
    impl_->last_game_id = -1;
    {
        std::lock_guard<std::mutex> lock(impl_->name_mutex);
        impl_->game_name.clear();
    }
    return true;
}

void FreetrackPoseSink::close()
{
    if (impl_->heap) {
        UnmapViewOfFile(const_cast<FTHeap*>(impl_->heap));
        impl_->heap = nullptr;
    }
    if (impl_->mapping) {
        CloseHandle(impl_->mapping);
        impl_->mapping = nullptr;
    }
    if (impl_->mutex) {
        CloseHandle(impl_->mutex);
        impl_->mutex = nullptr;
    }
}

bool FreetrackPoseSink::is_open() const
{
    return impl_->heap != nullptr;
}

std::string FreetrackPoseSink::connected_game() const
{
    std::lock_guard<std::mutex> lock(impl_->name_mutex);
    return impl_->game_name;
}

void FreetrackPoseSink::send(const Pose& pose, std::int64_t)
{
    if (!impl_->heap)
        return;

    // Match the legacy sender: UDP and FreeTrack expose different axis
    // conventions, and the shared-memory protocol stores radians and mm.
    const double tx_cm = -pose.translation_m[1] * 100.0;
    const double ty_cm = -pose.translation_m[2] * 100.0;
    const double tz_cm = -pose.translation_m[0] * 100.0;
    const double yaw_deg = pose.rotation_deg[0];
    const double pitch_deg = pose.rotation_deg[1];
    const double roll_deg = -pose.rotation_deg[2];

    const float yaw = static_cast<float>(-yaw_deg * kDegreesToRadians);
    const float roll = static_cast<float>(roll_deg * kDegreesToRadians);
    const bool crossing_90 = std::fabs(pitch_deg - 90.0) < 0.15;
    const float pitch = static_cast<float>(-
        (crossing_90 ? 89.86 : pitch_deg) * kDegreesToRadians);
    const float x = static_cast<float>(tx_cm * 10.0);
    const float y = static_cast<float>(ty_cm * 10.0);
    const float z = static_cast<float>(tz_cm * 10.0);

    store_float(impl_->heap->data.x, x);
    store_float(impl_->heap->data.y, y);
    store_float(impl_->heap->data.z, z);
    store_float(impl_->heap->data.yaw, yaw);
    store_float(impl_->heap->data.pitch, pitch);
    store_float(impl_->heap->data.roll, roll);
    store_float(impl_->heap->data.raw_x, x);
    store_float(impl_->heap->data.raw_y, y);
    store_float(impl_->heap->data.raw_z, z);
    store_float(impl_->heap->data.raw_yaw, yaw);
    store_float(impl_->heap->data.raw_pitch, static_cast<float>(pitch_deg * kDegreesToRadians));
    store_float(impl_->heap->data.raw_roll,
                static_cast<float>(-pose.rotation_deg[2] * kDegreesToRadians));

    const std::int32_t game_id = load_int(impl_->heap->game_id);
    if (game_id != impl_->last_game_id) {
        unsigned char table[8]{};
        std::string name;
        load_game_data(game_id, impl_->config.games_csv_path, table, name);
        store_int(impl_->heap->table_ints[0], pack_table(table));
        store_int(impl_->heap->table_ints[1], pack_table(table + 4));
        store_int(impl_->heap->game_id2, game_id);
        store_uint(impl_->heap->data.data_id, 0);
        impl_->last_game_id = game_id;
        if (name.empty())
            name = "Unknown game";
        std::lock_guard<std::mutex> lock(impl_->name_mutex);
        impl_->game_name = std::move(name);
    } else {
        InterlockedIncrement(reinterpret_cast<volatile LONG*>(&impl_->heap->data.data_id));
    }
}

}  // namespace trackheader
