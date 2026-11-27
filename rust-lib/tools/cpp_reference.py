"""Check fixtures against the unchanged C++ parser using a host Arduino shim.

Run: python rust-lib/tools/cpp_reference.py [--write]
Requires g++ with GNU C++17 extensions. No repository C++ source is modified.
The shim tests protocol logic, not Arduino String edge cases or FreeRTOS behavior.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

tools = Path(__file__).resolve().parent
root = tools.parent.parent
fixture = tools.parent / "tests" / "fixtures" / "cpp_frames.txt"

with tempfile.TemporaryDirectory(prefix="packet-device-cpp-reference-") as directory:
    exe = Path(directory) / ("reference.exe" if sys.platform == "win32" else "reference")
    cmd = ["g++", "-std=gnu++17", "-Wno-narrowing", "-static-libgcc", "-static-libstdc++", "-I", str(tools / "cpp"),
           "-I", str(root / "src"), str(tools / "cpp" / "reference.cpp"),
           str(root / "src" / "DevicePacketParser.cpp"),
           str(root / "src" / "CyclicRedundancyCheck.cpp"), "-o", str(exe)]
    compiled = subprocess.run(cmd, capture_output=True, text=True)
    if compiled.returncode:
        raise SystemExit(compiled.stderr or f"Compiler exited with {compiled.returncode}")
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(result.stderr or f"C++ reference exited with {result.returncode}")

text = result.stdout.replace("\r\n", "\n")
if "--write" in sys.argv:
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_text(text, encoding="utf-8", newline="\n")
    print("Wrote C++ reference fixtures.")
else:
    if fixture.read_text(encoding="utf-8").replace("\r\n", "\n") != text:
        raise SystemExit("C++ reference output changed. Review before regenerating fixtures.")
    print("PASS: fixtures match the current C++ parser; short CRC-disabled command accepted.")
