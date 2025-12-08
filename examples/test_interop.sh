#!/bin/bash
# Test script for C++ <-> Python interoperability

echo "========================================="
echo "  ESHM Interoperability Tests"
echo "========================================="
echo ""

# Test 1: Python Master -> C++ Slave
echo "Test 1: Python Master -> C++ Slave"
echo "-----------------------------------"
./build/examples/interop_cpp_slave test_py_cpp &
SLAVE_PID=$!
sleep 1.5
python3 py/examples/interop_py_master.py test_py_cpp 50 &
MASTER_PID=$!
wait $MASTER_PID
sleep 0.5
kill -TERM $SLAVE_PID 2>/dev/null
wait $SLAVE_PID 2>/dev/null
echo ""
echo "Test 1 Complete!"
echo ""

sleep 1

# Test 2: C++ Master -> Python Slave
echo "Test 2: C++ Master -> Python Slave"
echo "-----------------------------------"
python3 py/examples/interop_py_slave.py test_cpp_py &
SLAVE_PID=$!
sleep 1.5
./build/examples/interop_cpp_master test_cpp_py 50 &
MASTER_PID=$!
wait $MASTER_PID
sleep 0.5
kill -TERM $SLAVE_PID 2>/dev/null
wait $SLAVE_PID 2>/dev/null
echo ""
echo "Test 2 Complete!"
echo ""

echo "========================================="
echo "  All Tests Complete!"
echo "========================================="
