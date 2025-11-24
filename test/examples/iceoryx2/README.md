# iceoryx2 Examples and Tests

This directory contains comprehensive examples and tests for the iceoryx2 binding.

## Building

```bash
cd build
cmake .. -DENABLE_BUILD_TESTS=ON -DENABLE_BUILD_EXAMPLES=ON
make test_iceoryx2_pubsub
make iceoryx2_publisher_example
make iceoryx2_subscriber_example
make iceoryx2_ping_pong_example
```

## Test Suite

### Running the Test Suite

```bash
cd build/modules/Com
./test_iceoryx2_pubsub
```

### Test Cases

1. **Basic Pub/Sub** - Verifies basic publish/subscribe functionality
   - Sends 10 messages
   - Validates reception
   - Checks metrics

2. **High Frequency Messages** - Tests throughput under load
   - Sends 100 messages rapidly
   - Measures throughput
   - Allows 5% message loss tolerance

3. **Multiple Subscribers** - Validates 1-to-N pub/sub pattern
   - One publisher, two subscribers
   - Ensures both receive all messages
   - Tests fanout capability

4. **Subscribe Before Offer** - Tests service discovery
   - Subscriber starts before publisher
   - Validates reconnection
   - Tests service availability handling

5. **Cleanup and Restart** - Tests resource management
   - Creates and destroys bindings
   - Validates proper cleanup
   - Tests session isolation

### Expected Output

```
==========================================
  iceoryx2 Binding Test Suite
==========================================

========================================
TEST 1: Basic Pub/Sub
========================================
...
Result: ✓ PASSED

========================================
TEST 2: High Frequency Messages
========================================
...
Result: ✓ PASSED

...

==========================================
  Test Summary
==========================================
Passed: 5/5
Result: ✓ ALL TESTS PASSED
==========================================
```

## Examples

### 1. Publisher/Subscriber Example

Demonstrates real-world radar object publishing and subscription.

**Start the subscriber** (Terminal 1):
```bash
cd build/modules/Com
./iceoryx2_subscriber_example
```

**Start the publisher** (Terminal 2):
```bash
cd build/modules/Com
./iceoryx2_publisher_example
```

**Publisher Output:**
```
========================================
  iceoryx2 Publisher Example
  Radar Object Publisher
========================================

1. Initializing iceoryx2 binding...
   ✓ Initialized

2. Offering radar service...
   Service ID:  0x1234
   Instance ID: 0x1
   ✓ Service offered

3. Starting to publish radar objects...
   Press Ctrl+C to stop

Published object #   0 | dist= 10.0m | vel= -5.0m/s | angle=-30.0° | conf= 70%
Published object #   1 | dist= 10.5m | vel= -4.5m/s | angle=-29.0° | conf= 71%
...
```

**Subscriber Output:**
```
========================================
  iceoryx2 Subscriber Example
  Radar Object Subscriber
========================================

1. Initializing iceoryx2 binding...
   ✓ Initialized

2. Subscribing to radar service...
   Service ID:  0x1234
   Instance ID: 0x1
   ✓ Subscribed

3. Waiting for radar objects...
   Press Ctrl+C to stop

Received object #   0 | dist= 10.0m | vel= -5.0m/s | angle=-30.0° | conf= 70% | latency=  45μs
Received object #   1 | dist= 10.5m | vel= -4.5m/s | angle=-29.0° | conf= 71% | latency=  38μs
...
```

### 2. Ping-Pong Latency Test

Measures round-trip latency with bidirectional communication.

**Start the PONG node** (Terminal 1):
```bash
cd build/modules/Com
./iceoryx2_ping_pong_example pong
```

**Start the PING node** (Terminal 2):
```bash
cd build/modules/Com
./iceoryx2_ping_pong_example ping 10
```

**Output:**
```
========================================
  iceoryx2 Ping-Pong Example
  Bidirectional Latency Test
========================================

=== PING NODE ===
Sending 10 pings...
  Sent PING #0
  Received PONG #0 | RTT=156μs
  Sent PING #1
  Received PONG #1 | RTT=143μs
  ...

=== Latency Statistics ===
  Samples: 10
  Min RTT: 128 μs
  Max RTT: 201 μs
  Avg RTT: 152 μs
=========================
```

## Code Structure

### RadarObject Structure

```cpp
struct RadarObject {
    uint32_t object_id;
    float distance;      // meters
    float velocity;      // m/s
    float angle;         // degrees
    uint8_t confidence;  // 0-100%
    uint64_t timestamp;  // microseconds
} __attribute__((packed));
```

### PingPong Message

```cpp
struct PingPongMessage {
    uint32_t sequence;
    uint64_t send_timestamp_us;
    uint8_t payload[8];
} __attribute__((packed));
```

## Usage Patterns

### Basic Publisher

```cpp
Iceoryx2Binding binding;
binding.Initialize();
binding.OfferService(service_id, instance_id);

ByteBuffer data = /* your data */;
binding.SendEvent(service_id, instance_id, event_id, data);

binding.StopOfferService(service_id, instance_id);
binding.Shutdown();
```

### Basic Subscriber

```cpp
void callback(uint64_t service_id, uint64_t instance_id, 
             uint32_t event_id, const ByteBuffer& data) {
    // Process received data
}

Iceoryx2Binding binding;
binding.Initialize();
binding.SubscribeEvent(service_id, instance_id, event_id, callback);

// Wait for messages...

binding.UnsubscribeEvent(service_id, instance_id, event_id);
binding.Shutdown();
```

## Performance Notes

- **Latency**: Typical round-trip latency is 150-200 μs
- **Throughput**: Can handle 100+ messages/second easily
- **Zero-copy**: All data transfer uses shared memory
- **Message Size**: Currently optimized for small messages (< 100 bytes)

## Troubleshooting

### "Failed to loan sample" Error

This usually indicates buffer size mismatch. Make sure:
1. Message size matches payload type configuration
2. Publisher and subscriber use compatible type settings
3. Message size is reasonable (< 100 bytes recommended)

### No Messages Received

Check:
1. Publisher started before subscriber (or subscriber reconnected)
2. Service ID and Instance ID match on both sides
3. Both processes are running with proper permissions
4. iceoryx2 daemon is not blocking (it's not required for iceoryx2)

### Compilation Errors

Ensure:
1. iceoryx2 C FFI library is compiled (`libiceoryx2_ffi_c.so`)
2. CMake found the library (check CMake output)
3. RPATH is set correctly in build configuration

## Next Steps

- Experiment with different message sizes
- Try multiple publishers to one subscriber
- Measure latency under different loads
- Implement your own data structures
- Add QoS settings (history depth, etc.)

## Support

For issues or questions:
1. Check the main integration document: `ICEORYX2_INTEGRATION_COMPLETE.md`
2. Review iceoryx2 documentation: https://iceoryx.io/latest/
3. Examine the test suite for usage patterns
