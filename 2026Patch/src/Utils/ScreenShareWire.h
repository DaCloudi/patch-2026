#pragma once
#include <cstdint>
#include <cstring>

namespace ScreenShareWire {
    // Measured FastBufferWriter.WriterHandle fields on 20260323.14. The native
    // WriteBytesSafe body (329D830) copies at Buffer + Position and advances
    // Position; TryBeginWrite's unboxed body (3D55BC0) handles native growth.
    struct WriterHandle {
        unsigned char* buffer;
        int position, length, capacity, maxCapacity;
    };
    inline int PayloadSize(uint64_t length) {
        return length <= 4 * 1024 * 1024 ? static_cast<int>(length) + 5 : -1;
    }
    inline bool Matches(const unsigned char* wire, int size, const unsigned char* bytes, int length) {
        int count = -1;
        if (!wire || !bytes || length < 0 || size != PayloadSize(length)) {
            return false;
        }
        memcpy(&count, wire, sizeof(count));
        return count == length && !memcmp(wire + 5, bytes, length);
    }
    inline bool Write(WriterHandle* writer, const unsigned char* bytes, int length, unsigned char code) {
        const int size = length < 0 ? -1 : PayloadSize(length);
        if (!writer || !writer->buffer || !bytes || size < 0 || writer->position < 0 ||
            writer->position > writer->capacity || size > writer->capacity - writer->position) {
            return false;
        }
        auto destination = writer->buffer + writer->position;
        memcpy(destination, &length, sizeof(length));
        destination[4] = code;
        memcpy(destination + 5, bytes, length);
        writer->position += size;
        return true;
    }
}
