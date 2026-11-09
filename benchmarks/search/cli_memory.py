"""Observe only an explicitly spawned benchmark child; no process enumeration."""

import ctypes
from ctypes import wintypes
import os
import subprocess


class Counters(ctypes.Structure):
    _fields_ = [("length", wintypes.DWORD), ("faults", wintypes.DWORD),
                *[(name, ctypes.c_size_t) for name in
                  ("peak_working_set", "working_set", "peak_paged", "paged",
                   "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile")]]


class WindowsProcessMemory:
    def __init__(self):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.query = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        self.query.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        self.query.restype = wintypes.BOOL

    def open(self, pid):
        handle = self.kernel.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
        return handle, ctypes.get_last_error() if not handle else None

    def read(self, handle):
        counters = Counters()
        counters.length = ctypes.sizeof(counters)
        if not self.query(handle, ctypes.byref(counters), counters.length):
            return {"status": "query_failed", "peak_bytes": None, "error_code": ctypes.get_last_error()}
        if counters.peak_working_set == 0:
            return {"status": "invalid_os_value", "peak_bytes": None, "error_code": None}
        return {"status": "available", "peak_bytes": counters.peak_working_set, "error_code": None}

    def close(self, handle):
        if not self.kernel.CloseHandle(handle):
            raise ctypes.WinError(ctypes.get_last_error())


def run_observed(command, timeout=300):
    """Return CompletedProcess and a post-exit OS reading, or an explicit null reason."""
    observer = None
    unavailable = {"status": "unsupported_platform", "peak_bytes": None, "error_code": None}
    if os.name == "nt":
        try:
            observer = WindowsProcessMemory()
        except (OSError, AttributeError) as error:
            unavailable = {"status": "api_unavailable", "peak_bytes": None,
                           "error_code": getattr(error, "winerror", None)}
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) as process:
        handle = None
        reading = unavailable
        try:
            if observer:
                handle, code = observer.open(process.pid)
                if not handle:
                    reading = {"status": "open_process_failed", "peak_bytes": None, "error_code": code}
            stdout, stderr = process.communicate(timeout=timeout)
            if handle:
                reading = observer.read(handle)
            completed = subprocess.CompletedProcess(command, process.returncode, stdout, stderr)
        except BaseException:
            if process.poll() is None:
                process.kill()
            process.communicate()
            raise
        finally:
            if handle:
                observer.close(handle)
    return completed, reading


def summarize_readings(readings):
    """Keep partial coverage distinct from a complete six-case observation."""
    if not readings:
        raise ValueError("CLI memory observations must not be empty")
    available = []
    for reading in readings:
        if reading["status"] == "available":
            if type(reading["peak_bytes"]) is not int or reading["peak_bytes"] <= 0:
                raise ValueError("Available CLI memory must be positive integer bytes")
            available.append(reading["peak_bytes"])
        elif reading["peak_bytes"] is not None:
            raise ValueError("Unavailable CLI memory must be null")
    status = ("available" if len(available) == len(readings) else "partial" if available
              else "unsupported_platform" if all(item["status"] == "unsupported_platform" for item in readings)
              else "unavailable")
    return {"status": status, "unit": "bytes", "metric": "peak_working_set",
            "source": "none" if status == "unsupported_platform" else "GetProcessMemoryInfo.PeakWorkingSetSize",
            "capture_point": "after_process_exit_with_retained_query_handle",
            "scope": "separate_search_observation_processes_outside_timed_samples",
            "successful_observations": len(available), "total_observations": len(readings),
            "observed_max_bytes": max(available) if available else None, "samples": readings}
