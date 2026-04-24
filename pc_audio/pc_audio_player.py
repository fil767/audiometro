import argparse
import threading

import numpy as np
import serial
import sounddevice as sd


class TonePlayer:
    def __init__(self, samplerate: int):
        self.samplerate = samplerate
        self.phase = 0.0
        self.freq = 440.0
        self.gain = 0.0
        self.playing = False
        self.lock = threading.Lock()

        self.stream = sd.OutputStream(
            samplerate=self.samplerate,
            channels=1,
            dtype="float32",
            callback=self._callback,
            blocksize=0,
        )
        self.stream.start()

    def _callback(self, outdata, frames, _time, status):
        if status:
            print(f"[audio] {status}")

        with self.lock:
            playing = self.playing
            freq = self.freq
            gain = self.gain
            phase = self.phase

        if not playing or gain <= 0.0:
            outdata.fill(0)
            return

        phase_inc = 2.0 * np.pi * freq / self.samplerate
        idx = np.arange(frames, dtype=np.float32)
        samples = np.sin(phase + phase_inc * idx) * gain

        phase = (phase + phase_inc * frames) % (2.0 * np.pi)
        with self.lock:
            self.phase = phase

        outdata[:, 0] = samples.astype(np.float32)

    def start(self, freq: float, gain: float):
        with self.lock:
            self.freq = max(20.0, float(freq))
            self.gain = max(0.0, min(1.0, float(gain)))
            self.playing = True
        print(f"[tone] START freq={self.freq:.1f}Hz gain={self.gain:.3f}")

    def set_gain(self, gain: float):
        with self.lock:
            self.gain = max(0.0, min(1.0, float(gain)))
        print(f"[tone] GAIN {self.gain:.3f}")

    def stop(self):
        with self.lock:
            self.playing = False
            self.gain = 0.0
        print("[tone] STOP")

    def close(self):
        self.stream.stop()
        self.stream.close()


def parse_args():
    parser = argparse.ArgumentParser(description="STM32 audiometer PC audio player")
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM5")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate")
    parser.add_argument("--samplerate", type=int, default=48000, help="Audio sample rate")
    return parser.parse_args()


def main():
    args = parse_args()
    player = TonePlayer(args.samplerate)

    print(f"[serial] Opening {args.port} @ {args.baud}")
    ser = serial.Serial(args.port, args.baud, timeout=1)

    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue

            line = raw.decode("utf-8", errors="ignore").strip()
            if not line:
                continue

            print(f"[rx] {line}")
            parts = line.split()
            if len(parts) < 2 or parts[0] != "AUDIO":
                continue

            cmd = parts[1].upper()
            try:
                if cmd == "START" and len(parts) >= 4:
                    freq = float(parts[2])
                    gain = float(parts[3])
                    player.start(freq, gain)
                elif cmd == "GAIN" and len(parts) >= 3:
                    gain = float(parts[2])
                    player.set_gain(gain)
                elif cmd in {"STOP", "DONE"}:
                    player.stop()
            except ValueError:
                print(f"[warn] Messaggio non valido: {line}")
    except KeyboardInterrupt:
        print("\n[exit] Interrotto da tastiera")
    finally:
        player.close()
        ser.close()


if __name__ == "__main__":
    main()
