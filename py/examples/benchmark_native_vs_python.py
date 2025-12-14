#!/usr/bin/env python3
"""
Benchmark: Native C++ DataHandler vs Pure Python DataHandler

Compares encoding/decoding performance between:
1. Pure Python implementation (py/data_handler.py)
2. Native C++ implementation via ctypes (py/data_handler_native.py)
"""

import sys
import time
import os

# Add parent directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

# Import both implementations
from data_handler import DataHandler as PythonDataHandler, DataItem as PyDataItem, DataType as PyDataType
from data_handler_native import NativeDataHandler, DataItem as NativeDataItem, DataType as NativeDataType

def benchmark_python(iterations=10000):
    """Benchmark pure Python implementation"""
    handler = PythonDataHandler()

    # Create test data
    items = [
        PythonDataHandler.create_integer("counter", 42),
        PythonDataHandler.create_boolean("enabled", True),
        PythonDataHandler.create_real("temperature", 23.5),
        PythonDataHandler.create_string("status", "OK"),
        PythonDataHandler.create_string("source", "Python"),
    ]

    # Warm-up
    for _ in range(100):
        buffer = handler.encode_data_buffer(items)
        decoded = handler.decode_data_buffer(buffer)

    # Benchmark encoding
    start_time = time.perf_counter()
    for _ in range(iterations):
        buffer = handler.encode_data_buffer(items)
    encode_time = time.perf_counter() - start_time

    # Benchmark decoding
    start_time = time.perf_counter()
    for _ in range(iterations):
        decoded = handler.decode_data_buffer(buffer)
    decode_time = time.perf_counter() - start_time

    # Benchmark round-trip
    start_time = time.perf_counter()
    for _ in range(iterations):
        buffer = handler.encode_data_buffer(items)
        decoded = handler.decode_data_buffer(buffer)
    roundtrip_time = time.perf_counter() - start_time

    return {
        'encode_time': encode_time,
        'decode_time': decode_time,
        'roundtrip_time': roundtrip_time,
        'buffer_size': len(buffer),
        'iterations': iterations
    }


def benchmark_native(iterations=10000):
    """Benchmark native C++ implementation"""
    handler = NativeDataHandler()

    # Create test data
    items = [
        NativeDataHandler.create_integer("counter", 42),
        NativeDataHandler.create_boolean("enabled", True),
        NativeDataHandler.create_real("temperature", 23.5),
        NativeDataHandler.create_string("status", "OK"),
        NativeDataHandler.create_string("source", "C++"),
    ]

    # Warm-up
    for _ in range(100):
        buffer = handler.encode_data_buffer(items)
        decoded = handler.decode_data_buffer(buffer)

    # Benchmark encoding
    start_time = time.perf_counter()
    for _ in range(iterations):
        buffer = handler.encode_data_buffer(items)
    encode_time = time.perf_counter() - start_time

    # Benchmark decoding
    start_time = time.perf_counter()
    for _ in range(iterations):
        decoded = handler.decode_data_buffer(buffer)
    decode_time = time.perf_counter() - start_time

    # Benchmark round-trip
    start_time = time.perf_counter()
    for _ in range(iterations):
        buffer = handler.encode_data_buffer(items)
        decoded = handler.decode_data_buffer(buffer)
    roundtrip_time = time.perf_counter() - start_time

    handler.close()

    return {
        'encode_time': encode_time,
        'decode_time': decode_time,
        'roundtrip_time': roundtrip_time,
        'buffer_size': len(buffer),
        'iterations': iterations
    }


def print_results(python_results, native_results):
    """Print comparison results"""
    iterations = python_results['iterations']

    print("=" * 70)
    print("  DataHandler Performance Benchmark")
    print("=" * 70)
    print(f"  Iterations: {iterations:,}")
    print(f"  Buffer size: {python_results['buffer_size']} bytes (Python), {native_results['buffer_size']} bytes (C++)")
    print("=" * 70)
    print()

    # Encoding
    py_encode_rate = iterations / python_results['encode_time']
    native_encode_rate = iterations / native_results['encode_time']
    speedup_encode = python_results['encode_time'] / native_results['encode_time']

    print("ENCODING:")
    print(f"  Python:  {python_results['encode_time']:.4f}s  ({py_encode_rate:,.0f} ops/sec)")
    print(f"  Native:  {native_results['encode_time']:.4f}s  ({native_encode_rate:,.0f} ops/sec)")
    print(f"  Speedup: {speedup_encode:.2f}x faster")
    print()

    # Decoding
    py_decode_rate = iterations / python_results['decode_time']
    native_decode_rate = iterations / native_results['decode_time']
    speedup_decode = python_results['decode_time'] / native_results['decode_time']

    print("DECODING:")
    print(f"  Python:  {python_results['decode_time']:.4f}s  ({py_decode_rate:,.0f} ops/sec)")
    print(f"  Native:  {native_results['decode_time']:.4f}s  ({native_decode_rate:,.0f} ops/sec)")
    print(f"  Speedup: {speedup_decode:.2f}x faster")
    print()

    # Round-trip
    py_roundtrip_rate = iterations / python_results['roundtrip_time']
    native_roundtrip_rate = iterations / native_results['roundtrip_time']
    speedup_roundtrip = python_results['roundtrip_time'] / native_results['roundtrip_time']

    print("ROUND-TRIP (Encode + Decode):")
    print(f"  Python:  {python_results['roundtrip_time']:.4f}s  ({py_roundtrip_rate:,.0f} ops/sec)")
    print(f"  Native:  {native_results['roundtrip_time']:.4f}s  ({native_roundtrip_rate:,.0f} ops/sec)")
    print(f"  Speedup: {speedup_roundtrip:.2f}x faster")
    print()

    print("=" * 70)
    print("SUMMARY:")
    print(f"  Native C++ is {speedup_encode:.1f}x faster at encoding")
    print(f"  Native C++ is {speedup_decode:.1f}x faster at decoding")
    print(f"  Native C++ is {speedup_roundtrip:.1f}x faster overall")
    print("=" * 70)


def main():
    iterations = 10000

    if len(sys.argv) > 1:
        iterations = int(sys.argv[1])

    print(f"\nRunning benchmark with {iterations:,} iterations...")
    print("This may take a few seconds...\n")

    try:
        print("Benchmarking Pure Python implementation...")
        python_results = benchmark_python(iterations)

        print("Benchmarking Native C++ implementation...")
        native_results = benchmark_native(iterations)

        print()
        print_results(python_results, native_results)

    except Exception as e:
        print(f"Error: {e}")
        import traceback
        traceback.print_exc()
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
