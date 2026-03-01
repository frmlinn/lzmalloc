# lzmalloc

[![CI Build](https://github.com/frmlinn/lzmalloc/actions/workflows/ci.yml/badge.svg)](https://github.com/YOUR_USERNAME/lzmalloc/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey)](#)

A high-performance, lock-free, NUMA-aware memory allocator written in C11. Designed as a drop-in replacement for the standard POSIX `malloc` via `LD_PRELOAD`.

> ⚠️ **Development Status: Work In Progress (v1.0-alpha)** > This project is currently in active development. While the core engine and POSIX hooks are functional, rigorous stress testing, edge-case handling, and advanced telemetry are still underway. Comprehensive documentation, architecture diagrams, and UML models will be added in future updates.

## Architecture Overview

`lzmalloc` is structured in distinct layers to separate OS interactions, concurrent state, and the public POSIX API.