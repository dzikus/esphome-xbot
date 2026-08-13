"""Wire-level constants shared by the entity platforms.

Two register maps answer on different sources and are addressed separately;
the same register number means different things in each. Keep these here
rather than per platform: the values drifted apart once already.
"""

# Source byte a reply carries, and the address a request goes to.
SRC_MAIN = 0x23
SRC_MODULE = 0x21
DEST_MAIN = 0x20
DEST_MODULE = 0x21

READ_CMD_MAIN = 0x61
READ_CMD_MODULE = 0x01
WRITE_CMD = 0x03

# Speed limits are km/h scaled by this divisor over the wheel factor register.
REG_WHEEL_FACTOR = 0xEE
WHEEL_FACTOR_DIVISOR = 5794.65283203125

# (dest, cmd, first register, how many bytes to ask for). One request each, in
# this order. The wheel factor leads because every speed limit is a count scaled
# by it, and one read or written before it lands means a different speed.
POLL_TABLE = [
    (DEST_MAIN, READ_CMD_MAIN, REG_WHEEL_FACTOR, 0x0C),
    (DEST_MAIN, READ_CMD_MAIN, 0x46, 0x2A),
    (DEST_MAIN, READ_CMD_MAIN, 0x1A, 0x34),
    (DEST_MAIN, READ_CMD_MAIN, 0x6E, 0x32),
    (DEST_MAIN, READ_CMD_MAIN, 0xC2, 0x2C),
    (DEST_MAIN, READ_CMD_MAIN, 0xEA, 0x20),
    (DEST_MODULE, READ_CMD_MODULE, 0x14, 0x2E),
    (DEST_MAIN, READ_CMD_MAIN, 0x14, 0x1E),
    (DEST_MAIN, READ_CMD_MAIN, 0x5A, 0x2A),
    (DEST_MAIN, READ_CMD_MAIN, 0x32, 0x32),
    (DEST_MAIN, READ_CMD_MAIN, 0xBB, 0x06),
    (DEST_MAIN, READ_CMD_MAIN, 0x58, 0x10),
    (DEST_MAIN, READ_CMD_MAIN, 0x2A, 0x22),
    (DEST_MAIN, READ_CMD_MAIN, 0xBA, 0x0E),
    (DEST_MAIN, READ_CMD_MAIN, 0x66, 0x0E),
    (DEST_MAIN, READ_CMD_MAIN, 0xA0, 0x3A),
    (DEST_MAIN, READ_CMD_MAIN, 0xD4, 0x1A),
    (DEST_MAIN, READ_CMD_MAIN, 0x7B, 0x0A),
    (DEST_MAIN, READ_CMD_MAIN, 0xF2, 0x06),
    (DEST_MAIN, READ_CMD_MAIN, 0xFD, 0x06),
]
