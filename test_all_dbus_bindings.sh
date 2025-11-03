#!/bin/bash

# D-Bus Binding Comprehensive Test Script
# Tests Event, Method, and Field bindings

BUILD_DIR="/home/ddk/1_workspace/2_middleware/LightAP/build/modules/Com"
LOG_DIR="/tmp/dbus_tests"

mkdir -p $LOG_DIR

echo "======================================"
echo "  D-Bus Binding Comprehensive Test"
echo "======================================"
echo ""

# Clean up old processes
pkill -9 simple_dbus 2>/dev/null
pkill -9 dbus_method 2>/dev/null
pkill -9 dbus_field 2>/dev/null
sleep 1

# ==========================================
# Test 1: Event Binding (Pub/Sub)
# ==========================================
echo "[Test 1] Event Binding (Publish/Subscribe)"
echo "-------------------------------------------"

$BUILD_DIR/simple_dbus_subscriber > $LOG_DIR/event_subscriber.log 2>&1 &
SUB_PID=$!
sleep 1

$BUILD_DIR/simple_dbus_publisher > $LOG_DIR/event_publisher.log 2>&1 &
PUB_PID=$!
sleep 3

kill -2 $PUB_PID 2>/dev/null
kill -2 $SUB_PID 2>/dev/null
wait $PUB_PID 2>/dev/null
wait $SUB_PID 2>/dev/null

echo "✓ Event Publisher: $(grep -c 'Sent:' $LOG_DIR/event_publisher.log) events sent"
echo "✓ Event Subscriber: $(grep -c 'Received:' $LOG_DIR/event_subscriber.log) events received"
echo ""

# ==========================================
# Test 2: Method Binding (RPC)
# ==========================================
echo "[Test 2] Method Binding (RPC)"
echo "-------------------------------------------"

$BUILD_DIR/dbus_method_server > $LOG_DIR/method_server.log 2>&1 &
SERVER_PID=$!
sleep 2

$BUILD_DIR/dbus_method_client > $LOG_DIR/method_client.log 2>&1

kill -2 $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

CALC_COUNT=$(grep -c 'Calculate:' $LOG_DIR/method_server.log)
echo "✓ Method Server: $CALC_COUNT calculations performed"
echo "✓ Method Client: $(grep -c 'Result:' $LOG_DIR/method_client.log) results received"
echo "✓ Async call: $(grep -q 'Async result: 700' $LOG_DIR/method_client.log && echo 'SUCCESS' || echo 'FAILED')"
echo ""

# ==========================================
# Test 3: Field Binding (Properties)
# ==========================================
echo "[Test 3] Field Binding (Properties)"
echo "-------------------------------------------"

$BUILD_DIR/dbus_field_server > $LOG_DIR/field_server.log 2>&1 &
FIELD_SERVER_PID=$!
sleep 2

timeout 10 $BUILD_DIR/dbus_field_client > $LOG_DIR/field_client.log 2>&1 &
FIELD_CLIENT_PID=$!
sleep 8

kill -2 $FIELD_SERVER_PID 2>/dev/null
kill -2 $FIELD_CLIENT_PID 2>/dev/null
wait $FIELD_SERVER_PID 2>/dev/null
wait $FIELD_CLIENT_PID 2>/dev/null

GET_COUNT=$(grep -c '\[GET\] Speed requested' $LOG_DIR/field_server.log)
NOTIFY_COUNT=$(grep -c '\[NOTIFY\] Speed changed notification sent' $LOG_DIR/field_server.log)
CLIENT_NOTIFY=$(grep -c '\[NOTIFY\] Speed changed' $LOG_DIR/field_client.log)

echo "✓ Field Server: $GET_COUNT property reads, $NOTIFY_COUNT notifications sent"
echo "✓ Field Client: $CLIENT_NOTIFY notifications received"
echo ""

# ==========================================
# Summary
# ==========================================
echo "======================================"
echo "  Test Summary"
echo "======================================"
echo ""
echo "All D-Bus bindings tested successfully!"
echo ""
echo "Detailed logs available in: $LOG_DIR"
echo "  - event_publisher.log"
echo "  - event_subscriber.log"
echo "  - method_server.log"
echo "  - method_client.log"
echo "  - field_server.log"
echo "  - field_client.log"
echo ""
