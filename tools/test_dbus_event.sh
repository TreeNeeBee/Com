#!/bin/bash

# D-Bus Event Binding Test Script
# Usage: ./test_dbus_event.sh

BUILD_DIR="/home/ddk/1_workspace/2_middleware/LightAP/build/modules/Com"

echo "=== D-Bus Event Communication Test ==="
echo ""

# Check if executables exist
if [ ! -f "$BUILD_DIR/simple_dbus_publisher" ]; then
    echo "Error: simple_dbus_publisher not found"
    exit 1
fi

if [ ! -f "$BUILD_DIR/simple_dbus_subscriber" ]; then
    echo "Error: simple_dbus_subscriber not found"
    exit 1
fi

# Clean up any existing processes
echo "Cleaning up old processes..."
pkill -9 simple_dbus 2>/dev/null

# Start subscriber in background
echo "Starting subscriber..."
$BUILD_DIR/simple_dbus_subscriber > /tmp/subscriber.log 2>&1 &
SUBSCRIBER_PID=$!
sleep 1

# Check if subscriber started successfully
if ! ps -p $SUBSCRIBER_PID > /dev/null; then
    echo "Error: Failed to start subscriber"
    cat /tmp/subscriber.log
    exit 1
fi

echo "Subscriber started (PID: $SUBSCRIBER_PID)"
echo ""

# Start publisher for 5 seconds
echo "Starting publisher (will run for 5 seconds)..."
$BUILD_DIR/simple_dbus_publisher > /tmp/publisher.log 2>&1 &
PUBLISHER_PID=$!

# Wait 5 seconds
sleep 5

# Stop publisher
echo ""
echo "Stopping publisher..."
kill -2 $PUBLISHER_PID 2>/dev/null
wait $PUBLISHER_PID 2>/dev/null

# Show results
echo ""
echo "=== Publisher Output ==="
tail -10 /tmp/publisher.log

echo ""
echo "=== Subscriber Output ==="
tail -10 /tmp/subscriber.log

# Stop subscriber
echo ""
echo "Stopping subscriber..."
kill -2 $SUBSCRIBER_PID 2>/dev/null
wait $SUBSCRIBER_PID 2>/dev/null

echo ""
echo "=== Test Complete ==="
echo "Full logs:"
echo "  Publisher: /tmp/publisher.log"
echo "  Subscriber: /tmp/subscriber.log"
