# C++ Ray Tracer

A multithreaded path tracer written in modern C++20 with **no external dependencies**. It renders spheres with matte, metal and glass materials, soft shadows, anti-aliasing and depth of field, and writes the result straight to a PNG.

![Render](docs/render.png)

## Features

- Path tracing with recursive ray bouncing
- Materials: Lambertian (matte), Metal (with fuzz), Dielectric (glass, with Schlick reflectance and total internal reflection)
- Positionable camera with field of view and depth of field
- Anti-aliasing via random sub-pixel sampling, with gamma correction
- Multithreaded rendering with dynamic row-based load balancing (`std::atomic` + `std::jthread`)
- Built-in dependency-free PNG encoder
- Command-line options for resolution, quality and thread count

## Build

Requires a C++20 compiler (GCC 10+, Clang 13+, MSVC 2019 16.11+) and, optionally, CMake 3.16+.

**With CMake**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/raytracer
```

**Without CMake**

```bash
g++ -std=c++20 -O2 -pthread src/main.cpp -o raytracer
./raytracer
```

## Usage

```bash
./raytracer [--width N] [--samples N] [--depth N] [--threads N] [--out file.png]
```

| Option      | Default      | Meaning                                  |
|-------------|--------------|------------------------------------------|
| `--width`   | `800`        | Image width in pixels (16:9 aspect)      |
| `--samples` | `100`        | Rays per pixel (more = less noise)       |
| `--depth`   | `50`         | Maximum bounces per ray                  |
| `--threads` | all cores    | Number of worker threads                 |
| `--out`     | `render.png` | Output file                              |

Examples:

```bash
./raytracer --width 400 --samples 30            # quick preview
./raytracer --width 1920 --samples 500          # high-quality final render
./raytracer --threads 1 --width 400 --samples 30   # single-thread baseline for benchmarking
```

## Project structure

```
src/
  vec3.h           3D vector maths (points, directions, colours)
  random.h         Thread-local random number helpers
  ray.h            Ray: origin + t * direction
  hittable.h       HitRecord and abstract Hittable interface
  sphere.h         Ray/sphere intersection
  hittable_list.h  Scene container (closest-hit search)
  material.h       Lambertian, Metal, Dielectric
  camera.h         Camera with depth of field
  png_writer.h     Minimal PNG encoder
  main.cpp         Scene, render loop, threading
EXPLANATION.txt    Detailed walkthrough of the maths and code
CMakeLists.txt
```

For a full explanation of how everything works, read [`EXPLANATION.txt`](EXPLANATION.txt).

## Design notes

- **Dynamic load balancing:** threads claim rows from an atomic counter, so expensive rows (glass, many bounces) don't leave other threads idle. No mutexes are needed.
- **Per-thread RNG:** `thread_local` `std::mt19937` avoids contention on random number generation.
- **RAII throughout:** `std::jthread`, `std::shared_ptr` and `std::vector` mean there is no manual `new`/`delete` or `join`.
- **Output parameter on `hit()`:** avoids constructing an `std::optional<HitRecord>` on every miss in the hottest loop.

## Performance

Add your own numbers here after benchmarking:

| Threads | Time (400x225, 30 spp) | Speedup |
|---------|------------------------|---------|
| 1       |                        | 1.0x    |
| 2       |                        |         |
| 4       |                        |         |
| 8       |                        |         |

## Possible extensions

- BVH acceleration structure (currently O(N) per ray)
- Triangle meshes and `.obj` loading
- Textures and emissive materials
- Motion blur
- Unit tests and CI with sanitizers (ASan / TSan)

## Credits

Structure inspired by Peter Shirley's [*Ray Tracing in One Weekend*](https://raytracing.github.io/), reimplemented and modernised in C++20.
