import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is required. Install it with: python -m pip install pyserial", file=sys.stderr)
    raise


def parse_args():
    parser = argparse.ArgumentParser(
        description="Forward EEG bytes from a PC serial port to the Microduino ESP32 serial port."
    )
    parser.add_argument("--source", required=True, help="EEG source serial port, for example COM3")
    parser.add_argument("--target", required=True, help="Microduino serial port, for example COM7")
    parser.add_argument("--source-baud", type=int, default=57600, help="EEG source baud rate")
    parser.add_argument("--target-baud", type=int, default=115200, help="Microduino baud rate")
    parser.add_argument("--chunk", type=int, default=128, help="Maximum bytes to forward per read")
    parser.add_argument("--print-rate", type=float, default=1.0, help="Status print interval in seconds")
    return parser.parse_args()


def open_port(name, baud):
    return serial.Serial(
        port=name,
        baudrate=baud,
        timeout=0.02,
        write_timeout=0.2,
    )


def main():
    args = parse_args()
    forwarded = 0
    last_print = time.monotonic()

    with open_port(args.source, args.source_baud) as source, open_port(args.target, args.target_baud) as target:
        print(
            f"Forwarding EEG serial {args.source}@{args.source_baud} -> "
            f"{args.target}@{args.target_baud}. Press Ctrl+C to stop."
        )
        target.reset_input_buffer()

        while True:
            data = source.read(args.chunk)
            if data:
                target.write(data)
                forwarded += len(data)

            now = time.monotonic()
            if now - last_print >= args.print_rate:
                last_print = now
                while target.in_waiting:
                    line = target.readline().decode("utf-8", errors="replace").strip()
                    if line:
                        print(f"MIC: {line}")
                print(f"forwarded_bytes={forwarded}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
