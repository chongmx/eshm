"""
Python implementation of DataHandler for ASN.1 DER encoding/decoding

This module provides a Python interface to encode/decode structured data
using ASN.1 DER format, compatible with the C++ DataHandler implementation.
"""

import struct
from typing import Union, List, Dict, Any, Tuple
from enum import IntEnum


class Tag(IntEnum):
    """ASN.1 Universal Tags"""
    BOOLEAN = 0x01
    INTEGER = 0x02
    OCTET_STRING = 0x04
    NULL = 0x05
    REAL = 0x09
    UTF8_STRING = 0x0C
    SEQUENCE = 0x10


class AppTag(IntEnum):
    """Custom application tags for protocol"""
    EVENT = 0x80
    FUNCTION_CALL = 0x81
    FUNCTION_RETURN = 0x82
    IMAGE_FRAME = 0x83


class DataType(IntEnum):
    """Type descriptor for the three-sequence protocol"""
    INTEGER = 0
    BOOLEAN = 1
    REAL = 2
    STRING = 3
    BINARY = 4
    EVENT = 5
    FUNCTION_CALL = 6
    IMAGE_FRAME = 7


class DEREncoder:
    """ASN.1 DER Encoder"""

    def __init__(self):
        self.buffer = bytearray()

    def encode_tag(self, tag: int):
        """Encode ASN.1 tag"""
        self.buffer.append(tag)

    def encode_length(self, length: int):
        """Encode ASN.1 length"""
        if length < 128:
            self.buffer.append(length)
        else:
            # Long form
            num_bytes = (length.bit_length() + 7) // 8
            self.buffer.append(0x80 | num_bytes)
            for i in range(num_bytes - 1, -1, -1):
                self.buffer.append((length >> (i * 8)) & 0xFF)

    def encode_integer(self, value: int):
        """Encode INTEGER"""
        self.encode_tag(Tag.INTEGER)

        # Convert to bytes (two's complement for negative)
        if value == 0:
            data = bytes([0])
        else:
            # Determine sign
            negative = value < 0
            if negative:
                # Two's complement
                value = -value
                num_bytes = (value.bit_length() + 8) // 8
                value = (1 << (num_bytes * 8)) - value
            else:
                num_bytes = (value.bit_length() + 8) // 8

            data = value.to_bytes(num_bytes, byteorder='big', signed=False)

            # Ensure proper sign bit
            if not negative and data[0] & 0x80:
                data = b'\x00' + data

        self.encode_length(len(data))
        self.buffer.extend(data)

    def encode_boolean(self, value: bool):
        """Encode BOOLEAN"""
        self.encode_tag(Tag.BOOLEAN)
        self.encode_length(1)
        self.buffer.append(0xFF if value else 0x00)

    def encode_real(self, value: float):
        """Encode REAL using ISO 6093 NR3 format (IEEE 754 binary64)"""
        self.encode_tag(Tag.REAL)

        if value == 0.0:
            self.encode_length(0)
            return

        # ISO 6093 NR3 format: header (0x03) + 8 bytes IEEE 754 double
        data = bytearray([0x03])  # ISO 6093 NR3 marker

        # Pack IEEE 754 double in big-endian
        bits = struct.unpack('>Q', struct.pack('>d', value))[0]
        for i in range(7, -1, -1):
            data.append((bits >> (i * 8)) & 0xFF)

        self.encode_length(len(data))
        self.buffer.extend(data)

    def encode_utf8_string(self, value: str):
        """Encode UTF8_STRING"""
        self.encode_tag(Tag.UTF8_STRING)
        data = value.encode('utf-8')
        self.encode_length(len(data))
        self.buffer.extend(data)

    def encode_octet_string(self, value: bytes):
        """Encode OCTET_STRING"""
        self.encode_tag(Tag.OCTET_STRING)
        self.encode_length(len(value))
        self.buffer.extend(value)

    def begin_sequence(self) -> int:
        """Begin SEQUENCE, returns position"""
        self.encode_tag(Tag.SEQUENCE)
        start_pos = len(self.buffer)
        # Reserve space for length (we'll fill it in end_sequence)
        self.buffer.append(0)  # Placeholder
        return start_pos

    def end_sequence(self, start_pos: int):
        """End SEQUENCE, update length"""
        seq_length = len(self.buffer) - start_pos - 1

        # Encode the length
        if seq_length < 128:
            self.buffer[start_pos] = seq_length
        else:
            # Long form - need to insert bytes
            num_bytes = (seq_length.bit_length() + 7) // 8
            length_bytes = bytearray([0x80 | num_bytes])
            for i in range(num_bytes - 1, -1, -1):
                length_bytes.append((seq_length >> (i * 8)) & 0xFF)

            # Replace placeholder with actual length
            self.buffer[start_pos:start_pos+1] = length_bytes

    def get_data(self) -> bytes:
        """Get encoded data"""
        return bytes(self.buffer)

    def clear(self):
        """Clear buffer"""
        self.buffer.clear()


class DERDecoder:
    """ASN.1 DER Decoder"""

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def decode_tag(self) -> int:
        """Decode ASN.1 tag"""
        if self.pos >= len(self.data):
            raise ValueError("Unexpected end of data")
        tag = self.data[self.pos]
        self.pos += 1
        return tag

    def decode_length(self) -> int:
        """Decode ASN.1 length"""
        if self.pos >= len(self.data):
            raise ValueError("Unexpected end of data")

        first_byte = self.data[self.pos]
        self.pos += 1

        if first_byte < 128:
            return first_byte
        else:
            # Long form
            num_bytes = first_byte & 0x7F
            length = 0
            for _ in range(num_bytes):
                if self.pos >= len(self.data):
                    raise ValueError("Unexpected end of data")
                length = (length << 8) | self.data[self.pos]
                self.pos += 1
            return length

    def decode_integer(self) -> int:
        """Decode INTEGER"""
        tag = self.decode_tag()
        if tag != Tag.INTEGER:
            raise ValueError(f"Expected INTEGER tag, got {tag}")

        length = self.decode_length()
        if self.pos + length > len(self.data):
            raise ValueError("Unexpected end of data")

        data = self.data[self.pos:self.pos + length]
        self.pos += length

        # Convert from bytes (two's complement)
        value = int.from_bytes(data, byteorder='big', signed=True)
        return value

    def decode_boolean(self) -> bool:
        """Decode BOOLEAN"""
        tag = self.decode_tag()
        if tag != Tag.BOOLEAN:
            raise ValueError(f"Expected BOOLEAN tag, got {tag}")

        length = self.decode_length()
        if length != 1:
            raise ValueError(f"Invalid BOOLEAN length: {length}")

        if self.pos >= len(self.data):
            raise ValueError("Unexpected end of data")

        value = self.data[self.pos]
        self.pos += 1
        return value != 0

    def decode_real(self) -> float:
        """Decode REAL"""
        tag = self.decode_tag()
        if tag != Tag.REAL:
            raise ValueError(f"Expected REAL tag, got {tag}")

        length = self.decode_length()

        if length == 0:
            return 0.0

        if self.pos >= len(self.data):
            raise ValueError("Unexpected end of data")

        header = self.data[self.pos]
        self.pos += 1

        # ISO 6093 NR3 format
        if header == 0x03:
            if self.pos + 8 > len(self.data):
                raise ValueError("Unexpected end of data")

            # Read 8 bytes of IEEE 754 double in big-endian
            bits = 0
            for _ in range(8):
                bits = (bits << 8) | self.data[self.pos]
                self.pos += 1

            value = struct.unpack('>d', struct.pack('>Q', bits))[0]
            return value
        else:
            # Legacy format (if needed)
            raise ValueError(f"Unsupported REAL encoding: {header}")

    def decode_utf8_string(self) -> str:
        """Decode UTF8_STRING"""
        tag = self.decode_tag()
        if tag != Tag.UTF8_STRING:
            raise ValueError(f"Expected UTF8_STRING tag, got {tag}")

        length = self.decode_length()
        if self.pos + length > len(self.data):
            raise ValueError("Unexpected end of data")

        data = self.data[self.pos:self.pos + length]
        self.pos += length
        return data.decode('utf-8')

    def decode_octet_string(self) -> bytes:
        """Decode OCTET_STRING"""
        tag = self.decode_tag()
        if tag != Tag.OCTET_STRING:
            raise ValueError(f"Expected OCTET_STRING tag, got {tag}")

        length = self.decode_length()
        if self.pos + length > len(self.data):
            raise ValueError("Unexpected end of data")

        data = self.data[self.pos:self.pos + length]
        self.pos += length
        return bytes(data)

    def begin_sequence(self) -> int:
        """Begin SEQUENCE, returns end position"""
        tag = self.decode_tag()
        if tag != Tag.SEQUENCE:
            raise ValueError(f"Expected SEQUENCE tag, got {tag}")

        length = self.decode_length()
        end_pos = self.pos + length
        return end_pos

    def has_more_data(self) -> bool:
        """Check if more data available"""
        return self.pos < len(self.data)


class DataItem:
    """Data item in the exchange"""

    def __init__(self, data_type: DataType, key: str, value: Any):
        self.type = data_type
        self.key = key
        self.value = value


class DataHandler:
    """
    DataHandler for encoding/decoding structured data using ASN.1 DER

    Compatible with C++ DataHandler implementation.
    """

    def __init__(self):
        pass

    @staticmethod
    def create_integer(key: str, value: int) -> DataItem:
        """Create INTEGER item"""
        return DataItem(DataType.INTEGER, key, value)

    @staticmethod
    def create_boolean(key: str, value: bool) -> DataItem:
        """Create BOOLEAN item"""
        return DataItem(DataType.BOOLEAN, key, value)

    @staticmethod
    def create_real(key: str, value: float) -> DataItem:
        """Create REAL item"""
        return DataItem(DataType.REAL, key, value)

    @staticmethod
    def create_string(key: str, value: str) -> DataItem:
        """Create STRING item"""
        return DataItem(DataType.STRING, key, value)

    @staticmethod
    def create_binary(key: str, value: bytes) -> DataItem:
        """Create BINARY item"""
        return DataItem(DataType.BINARY, key, value)

    def encode_data_buffer(self, items: List[DataItem]) -> bytes:
        """
        Encode data items to buffer using three-sequence protocol

        Returns:
            Encoded bytes
        """
        encoder = DEREncoder()

        # Outer sequence
        start = encoder.begin_sequence()

        # 1. Type sequence
        type_start = encoder.begin_sequence()
        for item in items:
            encoder.encode_integer(item.type)
        encoder.end_sequence(type_start)

        # 2. Key sequence
        key_start = encoder.begin_sequence()
        for item in items:
            encoder.encode_utf8_string(item.key)
        encoder.end_sequence(key_start)

        # 3. Data sequence
        data_start = encoder.begin_sequence()
        for item in items:
            if item.type == DataType.INTEGER:
                encoder.encode_integer(item.value)
            elif item.type == DataType.BOOLEAN:
                encoder.encode_boolean(item.value)
            elif item.type == DataType.REAL:
                encoder.encode_real(item.value)
            elif item.type == DataType.STRING:
                encoder.encode_utf8_string(item.value)
            elif item.type == DataType.BINARY:
                encoder.encode_octet_string(item.value)
            else:
                raise ValueError(f"Unsupported data type: {item.type}")
        encoder.end_sequence(data_start)

        encoder.end_sequence(start)

        return encoder.get_data()

    def decode_data_buffer(self, buffer: bytes) -> List[DataItem]:
        """
        Decode data buffer into items using three-sequence protocol

        Args:
            buffer: Encoded bytes

        Returns:
            List of DataItem objects
        """
        decoder = DERDecoder(buffer)

        # Outer sequence
        outer_end = decoder.begin_sequence()

        # 1. Type sequence
        type_end = decoder.begin_sequence()
        types = []
        while decoder.pos < type_end:
            types.append(DataType(decoder.decode_integer()))

        # 2. Key sequence
        key_end = decoder.begin_sequence()
        keys = []
        while decoder.pos < key_end:
            keys.append(decoder.decode_utf8_string())

        # 3. Data sequence
        data_end = decoder.begin_sequence()
        items = []
        for i, (data_type, key) in enumerate(zip(types, keys)):
            if data_type == DataType.INTEGER:
                value = decoder.decode_integer()
            elif data_type == DataType.BOOLEAN:
                value = decoder.decode_boolean()
            elif data_type == DataType.REAL:
                value = decoder.decode_real()
            elif data_type == DataType.STRING:
                value = decoder.decode_utf8_string()
            elif data_type == DataType.BINARY:
                value = decoder.decode_octet_string()
            else:
                raise ValueError(f"Unsupported data type: {data_type}")

            items.append(DataItem(data_type, key, value))

        return items

    @staticmethod
    def extract_simple_values(items: List[DataItem]) -> Dict[str, Any]:
        """
        Extract simple values from items into a dictionary

        Args:
            items: List of DataItem objects

        Returns:
            Dictionary mapping keys to values
        """
        return {item.key: item.value for item in items}


# Example usage and test
if __name__ == "__main__":
    print("Testing DataHandler...")

    handler = DataHandler()

    # Create items
    items = [
        DataHandler.create_integer("counter", 42),
        DataHandler.create_boolean("enabled", True),
        DataHandler.create_real("temperature", 23.5),
        DataHandler.create_string("status", "OK"),
    ]

    # Encode
    buffer = handler.encode_data_buffer(items)
    print(f"Encoded {len(items)} items to {len(buffer)} bytes")

    # Decode
    decoded = handler.decode_data_buffer(buffer)
    values = DataHandler.extract_simple_values(decoded)

    print(f"Decoded values:")
    for key, value in values.items():
        print(f"  {key}: {value} (type: {type(value).__name__})")

    # Verify
    assert values["counter"] == 42
    assert values["enabled"] == True
    assert abs(values["temperature"] - 23.5) < 0.0001
    assert values["status"] == "OK"

    print("\nAll tests passed!")
