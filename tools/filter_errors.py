#!/usr/bin/env python3
"""
tools/filter_errors.py
Filter QEMU output stream to show only errors, warnings, panics, and faults in the terminal.
"""
import sys
import re

ERROR_PATTERN = re.compile(
    r"(error|fail|fault|panic|exception|crash|fatal|unhandled|critical|abort)",
    re.IGNORECASE
)

def main():
    try:
        for line in sys.stdin:
            if ERROR_PATTERN.search(line):
                sys.stdout.write(line)
                sys.stdout.flush()
    except (KeyboardInterrupt, BrokenPipeError):
        pass

if __name__ == "__main__":
    main()
