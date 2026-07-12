#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <windows.h>

constexpr std::uint32_t BridgeStartMagicV1 = 0x3153434D;
constexpr std::uint32_t BridgeStartAbiV1 = 1;
constexpr std::uint32_t BridgeBootstrapProtocolV1 = 1;

enum BridgeStartResultV1 : std::uint32_t
{
	BRIDGE_START_UNINITIALIZED = 0,
	BRIDGE_START_STARTING = 1,
	BRIDGE_START_LISTENING = 2,
	BRIDGE_START_INVALID_BLOCK = 3,
	BRIDGE_START_PROCESS_MISMATCH = 4,
	BRIDGE_START_ALREADY_STARTED = 5,
	BRIDGE_START_WINSOCK_FAILED = 6,
	BRIDGE_START_SOCKET_FAILED = 7,
	BRIDGE_START_BIND_FAILED = 8,
	BRIDGE_START_LISTEN_FAILED = 9,
	BRIDGE_START_WORKER_FAILED = 10,
};

#pragma pack(push, 1)
struct BridgeStartBlockV1
{
	std::uint32_t magic;
	std::uint32_t size;
	std::uint32_t abi;
	std::uint32_t pid;
	std::uint8_t instance_guid[16];
	std::uint8_t token[32];
	std::uint8_t sha256[32];
	std::uint32_t requested_port;
	std::uint32_t result_state;
	std::uint32_t bound_port;
	std::uint32_t protocol;
	std::uint32_t win32_error;
	std::uint32_t winsock_error;
	std::uint32_t reserved0;
	std::uint32_t reserved1;
};
#pragma pack(pop)

using BridgeStartV1Fn = DWORD(WINAPI*)(void*);

static_assert(std::is_standard_layout_v<BridgeStartBlockV1>);
static_assert(sizeof(BridgeStartBlockV1) == 128);
