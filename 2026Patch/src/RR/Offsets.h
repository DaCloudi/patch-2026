#pragma once

#include <cstdint>

// Field offsets verified for fingerprinted client 20260323.14 only.
// Hook-specific schema and disassembly evidence remains beside each use.
// These are byte displacements, not RVAs. Equal numbers in different groups
// describe different objects; do not merge them into a generic shared offset.
// The cast at each use supplies the measured field width/reference type.

namespace RR::Offsets::AccountModel {
    inline constexpr uintptr_t Relationship = 0x120;
    inline constexpr uintptr_t Id = 0x290;
}

namespace RR::Offsets::Relationship {
    inline constexpr uintptr_t Type = 0x14;
}

namespace RR::Offsets::ScreenShare {
    inline constexpr uintptr_t PrimaryPreset = 0x80;
    inline constexpr uintptr_t SecondaryPreset = 0x88;
}

namespace RR::Offsets::ScreenSharePreset {
    inline constexpr uintptr_t FramesPerSecond = 0x10;
}

namespace RR::Offsets::CircuitRegistry {
    inline constexpr uintptr_t Declarations = 0x78;
}

namespace RR::Offsets::RoomieManager {
    inline constexpr uintptr_t ConversationAccess = 0x4A;
    inline constexpr uintptr_t CompanionAccess = 0x4C;
}

namespace RR::Offsets::RoomieTool {
    inline constexpr uintptr_t Controller = 0x80;
}

namespace RR::Offsets::RoomieController {
    inline constexpr uintptr_t Manager = 0x70;
}

namespace RR::Offsets::ByteArray {
    inline constexpr uintptr_t Data = 0x20;
}

namespace RR::Offsets::Tachyon {
    inline constexpr uintptr_t RsaProvider = 0x48;
}

namespace RR::Offsets::RsaClass {
    inline constexpr uintptr_t ImportXml = 0x208;
    inline constexpr uintptr_t ImportXmlMethod = 0x210;
}

namespace RR::Offsets::ConnectRoomState {
    inline constexpr uintptr_t TargetRoom = 0x30;
}

namespace RR::Offsets::RoomInstance {
    inline constexpr uintptr_t VoiceAddress = 0x40;
    inline constexpr uintptr_t VoiceServerId = 0x48;
}

namespace RR::Offsets::String {
    inline constexpr uintptr_t Length = 0x10;
    inline constexpr uintptr_t Characters = 0x14;
}

namespace RR::Offsets::HTTPRequest {
    inline constexpr uintptr_t Uri = 0x10;
    inline constexpr uintptr_t Response = 0x70;
}

namespace RR::Offsets::TlsAuthentication {
    inline constexpr uintptr_t TargetUri = 0x20;
}

namespace RR::Offsets::HTTPResponse {
    inline constexpr uintptr_t StatusCode = 0x18;
}
