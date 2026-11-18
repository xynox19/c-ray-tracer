// Multithreaded path tracer. See EXPLANATION.txt for a full walkthrough.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "camera.h"
#include "hittable_list.h"
#include "material.h"
#include "png_writer.h"
#include "random.h"
#include "sphere.h"

namespace {

struct Settings {
    int width = 800;
    int samples = 100;      // rays per pixel
    int max_depth = 50;     // max bounces per ray
    unsigned threads = std::max(1u, std::thread::hardware_concurrency());
    std::string out = "render.png";
};

// Colour seen along a ray: bounce around the scene until the ray escapes or runs out of depth.
Color ray_color(const Ray& r, const Hittable& world, int depth) {
    if (depth <= 0) return Color(0, 0, 0);   // too many bounces: no light gathered

    HitRecord rec;
    // t_min = 0.001 avoids "shadow acne" caused by floating-point self-intersection.
    if (world.hit(r, 0.001, 1e30, rec)) {
        Ray scattered;
        Color attenuation;
        if (rec.material->scatter(r, rec, attenuation, scattered))
            return attenuation * ray_color(scattered, world, depth - 1);
        return Color(0, 0, 0);               // absorbed
    }

    // Sky: vertical white -> blue gradient.
    Vec3 unit_dir = unit_vector(r.direction());
    double t = 0.5 * (unit_dir.y + 1.0);
    return (1.0 - t) * Color(1, 1, 1) + t * Color(0.5, 0.7, 1.0);
}

HittableList random_scene() {
    HittableList world;

    world.add(std::make_shared<Sphere>(Point3(0, -1000, 0), 1000,
                                       std::make_shared<Lambertian>(Color(0.5, 0.5, 0.5))));

    for (int a = -11; a < 11; ++a) {
        for (int b = -11; b < 11; ++b) {
            double choose = random_double();
            Point3 center(a + 0.9 * random_double(), 0.2, b + 0.9 * random_double());
            if ((center - Point3(4, 0.2, 0)).length() <= 0.9) continue;

            std::shared_ptr<Material> mat;
            if (choose < 0.8) {
                mat = std::make_shared<Lambertian>(Color(random_double(), random_double(), random_double()) *
                                                   Color(random_double(), random_double(), random_double()));
            } else if (choose < 0.95) {
                mat = std::make_shared<Metal>(Color(random_vec(0.5, 1)), random_double(0, 0.5));
            } else {
                mat = std::make_shared<Dielectric>(1.5);
            }
            world.add(std::make_shared<Sphere>(center, 0.2, mat));
        }
    }

    world.add(std::make_shared<Sphere>(Point3(0, 1, 0), 1.0, std::make_shared<Dielectric>(1.5)));
    world.add(std::make_shared<Sphere>(Point3(-4, 1, 0), 1.0, std::make_shared<Lambertian>(Color(0.4, 0.2, 0.1))));
    world.add(std::make_shared<Sphere>(Point3(4, 1, 0), 1.0, std::make_shared<Metal>(Color(0.7, 0.6, 0.5), 0.0)));
    return world;
}

std::uint8_t to_byte(double linear) {
    double gamma = std::sqrt(std::max(0.0, linear));   // gamma 2.0 correction
    return static_cast<std::uint8_t>(256 * std::clamp(gamma, 0.0, 0.999));
}

Settings parse_args(int argc, char** argv) {
    Settings s;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << arg << "\n"; std::exit(1); }
            return argv[++i];
        };
        if (arg == "--width") s.width = std::stoi(next());
        else if (arg == "--samples") s.samples = std::stoi(next());
        else if (arg == "--depth") s.max_depth = std::stoi(next());
        else if (arg == "--threads") s.threads = static_cast<unsigned>(std::stoi(next()));
        else if (arg == "--out") s.out = next();
        else {
            std::cerr << "Usage: raytracer [--width N] [--samples N] [--depth N] [--threads N] [--out file.png]\n";
            std::exit(arg == "--help" ? 0 : 1);
        }
    }
    s.width = std::max(s.width, 16);
    s.samples = std::max(s.samples, 1);
    s.threads = std::max(s.threads, 1u);
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const Settings cfg = parse_args(argc, argv);
    const double aspect = 16.0 / 9.0;
    const int width = cfg.width;
    const int height = static_cast<int>(width / aspect);

    HittableList world = random_scene();

    Point3 look_from(13, 2, 3), look_at(0, 0, 0);
    Camera cam(look_from, look_at, Vec3(0, 1, 0), 20.0, aspect, 0.1, 10.0);

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 3);

    // Work distribution: threads grab the next unrendered row from an atomic counter.
    std::atomic<int> next_row{0};
    std::atomic<int> rows_done{0};

    auto worker = [&] {
        while (true) {
            int y = next_row.fetch_add(1);
            if (y >= height) return;
            for (int x = 0; x < width; ++x) {
                Color sum(0, 0, 0);
                for (int s = 0; s < cfg.samples; ++s) {
                    double u = (x + random_double()) / width;
                    double v = 1.0 - (y + random_double()) / height;   // flip: image y grows downward
                    sum += ray_color(cam.get_ray(u, v), world, cfg.max_depth);
                }
                sum /= cfg.samples;
                std::size_t i = (static_cast<std::size_t>(y) * width + x) * 3;
                pixels[i] = to_byte(sum.x);
                pixels[i + 1] = to_byte(sum.y);
                pixels[i + 2] = to_byte(sum.z);
            }
            ++rows_done;
        }
    };

    std::cout << "Rendering " << width << "x" << height << ", " << cfg.samples << " spp, depth "
              << cfg.max_depth << ", " << cfg.threads << " threads\n";
    auto start = std::chrono::steady_clock::now();

    std::vector<std::jthread> pool;   // jthread joins automatically on destruction (RAII)
    for (unsigned i = 0; i < cfg.threads; ++i) pool.emplace_back(worker);

    while (rows_done < height) {
        std::cout << "\rProgress: " << (100 * rows_done / height) << "%   " << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    pool.clear();   // join all threads

    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "\rProgress: 100%   \nDone in " << elapsed.count() << " s\n";

    if (!png::write(cfg.out, width, height, pixels)) {
        std::cerr << "Failed to write " << cfg.out << "\n";
        return 1;
    }
    std::cout << "Saved " << cfg.out << "\n";
}
