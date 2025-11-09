# Simple System Monitor Tool (Assignment 3)

This project implements a simple system monitor tool in C++ for Linux systems that displays:
- Approximate overall CPU usage
- Total memory (from /proc/meminfo)
- Top processes by CPU usage (sampled over an interval)

The tool is console-based and requires reading from `/proc`. No external libraries are required.

## Files
- `src/main.cpp` - main C++ source file
- `Makefile` - build file
- `README.md` - this file
- `LICENSE` - MIT License
- `.gitignore` - ignore binary

## Build
On a Linux machine with `g++` installed:
```bash
make
# or
g++ -std=c++17 src/main.cpp -o sysmon
```

## Run
```bash
./sysmon
```
The program refreshes every 2 seconds and prints top processes by observed CPU usage. Stop with `Ctrl+C`.

## Notes & Limitations
- This program reads `/proc` and uses a simple delta-based method to estimate CPU usage per process.
- It is intended for educational/assignment use and is **not** as feature-complete or robust as `top`/`htop`.
- Parsing `/proc/[pid]/stat` is error-prone across kernels and edge cases; this code attempts to be reasonably robust but may not cover every case.

## Suggested Improvements (for future work)
- Add paging/navigation (ncurses)
- Show per-core statistics
- Add sorting options (by memory, PID, command)
- Show process owner (UID -> username)
- Add better error handling and unit tests

## Author
Generated for assignment submission.
