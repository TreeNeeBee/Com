/**
 * @file        test_coreipc_binding_full.cpp
 * @brief       Core IPC binding full功能验证 (Event + Method + Field)
 * @date        2026-02-02
 * @details     v2.0 IPC-based Registry architecture:
 *              CRegistryDispatcher must be running before CoreIPCBinding
 *              can initialize (single-writer model).
 *
 * <table>
 * <tr><th>Date        <th>Version  <th>Author  <th>Description
 * <tr><td>2026-02-02  <td>1.0      <td>Team    <td>Initial implementation
 * <tr><td>2026-02-10  <td>2.0      <td>Aii     <td>Add CRegistryDispatcher for v2.0 architecture
 * </table>
 */

#include "CoreIPCBinding.hpp"
#include "CRegistryDispatcher.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

using namespace lap::com::binding;
using namespace lap::com::registry;
using lap::core::ScopedLock;
using lap::core::Mutex;

namespace
{
constexpr uint64_t kServiceId = 0x1234;
constexpr uint64_t kInstanceId = 0x0001;
constexpr uint32_t kEventId = 0x0101;
constexpr uint32_t kMethodId = 0x0202;
constexpr uint32_t kFieldId = 0x0303;
}

int main()
{
	// =====================================================================
	// Phase 0: Start CRegistryDispatcher (v2.0 single-writer model)
	// =====================================================================
	CRegistryDispatcher dispatcher;
	auto dispatcherInit = dispatcher.Initialize();
	if ( !dispatcherInit.HasValue() ) {
		std::cerr << "Dispatcher Initialize failed: "
		          << dispatcherInit.Error().Message() << std::endl;
		return 1;
	}

	std::thread dispatcherThread( [&dispatcher]() {
		auto runResult = dispatcher.Run();
		if ( !runResult.HasValue() ) {
			std::cerr << "[WARN] Dispatcher Run exited: "
			          << runResult.Error().Message() << std::endl;
		}
	} );

	// Give dispatcher time to enter event loop
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

	// =====================================================================
	// Phase 1: Initialize CoreIPC bindings
	// =====================================================================
	CoreIPCBinding server;
	CoreIPCBinding client;

	auto server_init = server.Initialize();
	if (!server_init) {
		std::cerr << "Server Initialize failed: " << server_init.Error().Message() << std::endl;
		dispatcher.Shutdown();
		if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
		return 1;
	}
	auto client_init = client.Initialize();
	if (!client_init) {
		std::cerr << "Client Initialize failed: " << client_init.Error().Message() << std::endl;
		server.Shutdown();
		dispatcher.Shutdown();
		if ( dispatcherThread.joinable() ) { dispatcherThread.join(); }
		return 1;
	}

	// Wait for IPC scanners to discover channels
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

	auto offer_result = server.OfferService(kServiceId, kInstanceId);
	if (!offer_result) {
		std::cerr << "OfferService failed: " << offer_result.Error().Message() << std::endl;
		return 1;
	}

	// Allow publisher scanner to observe active subscribers later
	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// ---------------------------------------------------------------------
	// Event 测试
	// ---------------------------------------------------------------------
	std::atomic< bool > event_received{false};
	ByteBuffer event_payload;

	auto sub_result = client.SubscribeEvent< ByteBuffer >(kServiceId, kInstanceId, kEventId,
		[&event_received, &event_payload](uint64_t, uint64_t, uint32_t, const ByteBuffer& data) {
			event_payload = data;
			event_received.store(true);
		});

	if (!sub_result) {
		std::cerr << "SubscribeEvent failed: " << sub_result.Error().Message() << std::endl;
		return 1;
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	ByteBuffer event_data{0x11, 0x22, 0x33, 0x44};
	const auto event_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
	while (!event_received.load() && std::chrono::steady_clock::now() < event_deadline) {
		auto send_result = server.SendEvent(kServiceId, kInstanceId, kEventId, event_data);
		if (!send_result) {
			std::cerr << "SendEvent failed: " << send_result.Error().Message() << std::endl;
			return 1;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	if (!event_received.load()) {
		std::cerr << "Event not received in time" << std::endl;
		return 1;
	}

	if (event_payload != event_data) {
		std::cerr << "Event payload mismatch" << std::endl;
		return 1;
	}

	// ---------------------------------------------------------------------
	// Method 测试
	// ---------------------------------------------------------------------
	auto register_result = server.RegisterMethod< ByteBuffer, ByteBuffer >(
		kServiceId, kInstanceId, kMethodId,
		[](uint64_t, uint64_t, uint32_t, const ByteBuffer& request) -> ByteBuffer {
			ByteBuffer response(request.rbegin(), request.rend());
			return response;
		});

	if (!register_result) {
		std::cerr << "RegisterMethod failed: " << register_result.Error().Message() << std::endl;
		return 1;
	}

	ByteBuffer request{0xAA, 0xBB, 0xCC};
	auto call_result = client.CallMethod< ByteBuffer >(kServiceId, kInstanceId, kMethodId, request);
	if (!call_result) {
		std::cerr << "CallMethod failed: " << call_result.Error().Message() << std::endl;
		return 1;
	}

	ByteBuffer expected_response{0xCC, 0xBB, 0xAA};
	if (call_result.Value() != expected_response) {
		std::cerr << "Method response mismatch" << std::endl;
		return 1;
	}

	// ---------------------------------------------------------------------
	// Field 测试 (Getter/Setter via Method + Notifier via Event)
	// ---------------------------------------------------------------------
	Mutex field_mutex;
	ByteBuffer field_value{0x01, 0x02};

	const uint32_t getter_method_id = kFieldId | 0x10000U;
	const uint32_t setter_method_id = kFieldId | 0x20000U;

	auto reg_get = server.RegisterMethod< ByteBuffer, ByteBuffer >(
		kServiceId, kInstanceId, getter_method_id,
		[&field_mutex, &field_value](uint64_t, uint64_t, uint32_t, const ByteBuffer&) -> ByteBuffer {
			ScopedLock< Mutex > lock(field_mutex);
			return field_value;
		});

	if (!reg_get) {
		std::cerr << "Register getter failed: " << reg_get.Error().Message() << std::endl;
		return 1;
	}

	auto reg_set = server.RegisterMethod< ByteBuffer, ByteBuffer >(
		kServiceId, kInstanceId, setter_method_id,
		[&field_mutex, &field_value](uint64_t, uint64_t, uint32_t, const ByteBuffer& data) -> ByteBuffer {
			ScopedLock< Mutex > lock(field_mutex);
			field_value = data;
			return ByteBuffer{};
		});

	if (!reg_set) {
		std::cerr << "Register setter failed: " << reg_set.Error().Message() << std::endl;
		return 1;
	}

	ByteBuffer new_value{0x10, 0x20, 0x30};
	auto set_result = client.SetField(kServiceId, kInstanceId, kFieldId, new_value);
	if (!set_result) {
		std::cerr << "SetField failed: " << set_result.Error().Message() << std::endl;
		return 1;
	}

	auto get_result = client.GetField< ByteBuffer >(kServiceId, kInstanceId, kFieldId);
	if (!get_result) {
		std::cerr << "GetField failed: " << get_result.Error().Message() << std::endl;
		return 1;
	}

	if (get_result.Value() != new_value) {
		std::cerr << "Field value mismatch" << std::endl;
		return 1;
	}

	// Cleanup
	client.UnsubscribeEvent(kServiceId, kInstanceId, kEventId);
	server.StopOfferService(kServiceId, kInstanceId);
	client.Shutdown();
	server.Shutdown();

	// Shutdown dispatcher
	dispatcher.Shutdown();
	if ( dispatcherThread.joinable() ) {
		dispatcherThread.join();
	}

	std::cout << "Core IPC binding full test PASSED" << std::endl;
	return 0;
}
