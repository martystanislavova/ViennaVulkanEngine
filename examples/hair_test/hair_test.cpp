import std;
import VVEngine;
import VVE.ExampleSupport;

/**
 * @file
 * @brief End-to-end executable for the selected forward renderer through the public facade.
 */
namespace {

   /// @brief Reads an optional capture path, defaulting to the executable's verification directory.
   [[nodiscard]] std::filesystem::path capturePath(int argc, char **argv) {
      // Keep automated captures in their build tree when an output path is supplied.
      for (int index = 1; index + 1 < argc; ++index) {
         if (argv[index] != nullptr && argv[index + 1] != nullptr && std::string_view{argv[index]} == "--output") {
            return std::filesystem::path{argv[index + 1]};
         }
      }
      const auto executable_path =
          argc > 0 && argv[0] != nullptr ? std::filesystem::path{argv[0]} : std::filesystem::path{};
      const auto executable_directory =
          executable_path.has_parent_path() ? executable_path.parent_path() : std::filesystem::current_path();
      return executable_directory.parent_path() / "verify" / "simple_hair_demo_capture.png";
   }

   // data structures and functions
   namespace m = vve::math;
   using vve::Vec3;

   struct HairStrands {
      int strandCount{};
      int pointsPerStrand{};
      float segmentLength{};
      std::vector<vve::Vec3> pos, prevPos, restPos;
      std::vector<float> invMass;
      std::vector<float> segLen; // rest length of the segment that starts at each point
   };

   std::vector<Vec3> makeRoots(int candidate, float radius) {
      std::vector<Vec3> roots;
      constexpr float golden_angle = 2.399963F;
      for (int i = 0; i < candidate; ++i) {
         const float y = 1.0F - 2.0F * (static_cast<float>(i) + 0.5F) / static_cast<float>(candidate);
         const float r = std::sqrt(1.0F - y * y);
         const float phi = golden_angle * static_cast<float>(i);
         const Vec3 n{r * std::cos(phi), y, r * std::sin(phi)};
         if (n.y > 0.1F + 0.3F * n.z) {
            roots.push_back(m::scale(n, radius));
         }
      }
      return roots;
   }

   HairStrands growHair(const std::vector<Vec3> &roots, int pointsPerStrand, float segmentLength, float headRadius) {
      HairStrands hair;
      hair.strandCount = static_cast<int>(roots.size());
      hair.pointsPerStrand = pointsPerStrand;
      hair.segmentLength = segmentLength;
      hair.pos.resize(roots.size() * static_cast<std::size_t>(pointsPerStrand));
      hair.invMass.assign(hair.pos.size(), 1.0F);
      const Vec3 down{0.0F, -1.0F, 0.0F};

      std::mt19937 rng{42}; // fixed seed: same hair every run
      std::uniform_real_distribution<float> jitter{0.75F, 1.25F};
      std::uniform_real_distribution<float> sway{-0.15F, 0.15F};

      for (int s = 0; s < hair.strandCount; ++s) {
         Vec3 p = roots[static_cast<std::size_t>(s)];
         const float f = std::clamp((roots[static_cast<std::size_t>(s)].z - 0.2F) / 0.6F, 0.0F,
                                    1.0F); // 0 at sides/back, 1 at the forehead
         const float seg =
             segmentLength * jitter(rng) * (1.0F - 0.85F * f); // bangs: front strands are about a third as long
         const Vec3 drift{sway(rng), 0.0F, sway(rng)};

         for (int k = 0; k < pointsPerStrand; ++k) {
            hair.pos[static_cast<std::size_t>(s * pointsPerStrand + k)] = p;
            const Vec3 n = m::normalize(p);
            Vec3 dir = m::normalize(m::subtract(down, m::scale(n, m::dot(down, n)))); // downhill along the surface
            if (p.y < -0.15F) {
               dir = m::normalize(m::add(down, drift)); // small per-strand sideways bend
            } // below the "ears" just hang
            p = m::add(p, m::scale(dir, seg));
            if (p.y >= -0.15F) {
               p = m::scale(m::normalize(p), headRadius + 0.03F);
            } // stay just outside the head
         }
         hair.invMass[static_cast<std::size_t>(s * pointsPerStrand)] = 0.0F; // oot is fixed
      }
      hair.prevPos = hair.pos;
      hair.restPos = hair.pos;

      hair.segLen.assign(hair.pos.size(), 0.0F);
      for (int s = 0; s < hair.strandCount; ++s) {
         for (int k = 0; k + 1 < pointsPerStrand; ++k) {
            const std::size_t i = static_cast<std::size_t>(s * pointsPerStrand + k);
            hair.segLen[i] = m::length(m::subtract(hair.pos[i + 1], hair.pos[i]));
         }
      }
      return hair;
   }

   // lift per strand to avoid overlapping ribbons
   float strandLift(int s) {
      return 0.02F * static_cast<float>((static_cast<std::uint32_t>(s) * 2654435761U) % 1000U) / 1000.0F;
   }

   // draw each strand as a ribbon with two vertices
   void buildRibbons(const HairStrands &h, float width, vve::MeshDescriptor &mesh) {
      const int N = h.pointsPerStrand;
      std::mt19937 rng{7};
      std::uniform_real_distribution<float> uni{0.0F, 1.0F};

      for (int s = 0; s < h.strandCount; ++s) {
         const float u = uni(rng);
         for (int k = 0; k < N; ++k) {
            const auto at = [&](int i) { return h.pos[static_cast<std::size_t>(s * N + i)]; };
            const Vec3 p = at(k);
            const Vec3 tangent = m::normalize(m::subtract(at(std::min(k + 1, N - 1)), at(std::max(k - 1, 0))));
            const Vec3 outward = m::normalize(p);
            const Vec3 side = m::normalize(m::cross(tangent, outward));
            const float w =
                width * (1.0F - 0.85F * static_cast<float>(k) / static_cast<float>(N - 1)); // thinner at the tip
            mesh.positions.push_back(m::subtract(p, m::scale(side, w)));
            mesh.positions.push_back(m::add(p, m::scale(side, w)));
            mesh.normals.push_back(outward);
            mesh.normals.push_back(outward);
            const float v = 1.0F - static_cast<float>(k) / static_cast<float>(N - 1);
            mesh.uvs.push_back(vve::Vec2{u, v});
            mesh.uvs.push_back(vve::Vec2{u, v});
            if (k > 0) {
               const auto c =
                   static_cast<std::uint32_t>(mesh.positions.size()) - 2; // current pair c (left), c+1 (right)
               const std::uint32_t a = c - 2, b = c - 1, d = c + 1;       // previous pair a, b
               for (const auto i : {a, b, c, b, d, c, a, c, b, b, c, d}) {
                  mesh.indices.push_back(i);
               } // both windings
               /*for (const auto i : {a, b, c, b, d, c}) {
                  mesh.indices.push_back(i);
               }*/
            }
         }
      }
   }

   /// sameleft/right vertex pairs but from the current particle positions
   void ribbonPositions(const HairStrands &h, float width, const Vec3 &headCenter, vve::Vector<Vec3> &out) {
      out.clear();
      const int N = h.pointsPerStrand;

      for (int s = 0; s < h.strandCount; ++s) {
         const std::size_t base = static_cast<std::size_t>(s) * static_cast<std::size_t>(N);
         for (int k = 0; k < N; ++k) {
            const Vec3 p = h.pos[base + static_cast<std::size_t>(k)];
            const Vec3 next = h.pos[base + static_cast<std::size_t>(std::min(k + 1, N - 1))];
            const Vec3 prev = h.pos[base + static_cast<std::size_t>(std::max(k - 1, 0))];
            const Vec3 tangent = m::normalize(m::subtract(next, prev));
            const Vec3 outward = m::normalize(m::subtract(p, headCenter));
            const Vec3 lift = m::scale(outward, strandLift(s));
            const Vec3 side = m::normalize(m::cross(tangent, outward));
            const float wv = 0.75F + 0.5F * (strandLift(s) / 0.02F); // 0.75 to 1.25 times the width
            const float t = static_cast<float>(k) / static_cast<float>(N - 1);
            const float taper = 1.0F - 0.85F * std::max(0.0F, (t - 0.3F) / 0.7F);
            const float w = width * wv * taper * 0.5F;
            out.push_back(m::subtract(m::add(p, lift), m::scale(side, w)));
            out.push_back(m::add(m::add(p, lift), m::scale(side, w)));
         }
      }
   }

   struct HeadMotion {
      float yaw{}, pitch{}, roll{};
   };
   const Vec3 kPivot{0.0F, -0.8F, 0.0F}; // head rotation pivot point

   Vec3 rotateHead(const Vec3 &v, const HeadMotion &motion) {
      Vec3 r = m::subtract(v, kPivot);
      const float cr = std::cos(motion.roll), sr = std::sin(motion.roll);
      r = Vec3{cr * r.x - sr * r.y, sr * r.x + cr * r.y, r.z};
      const float cp = std::cos(motion.pitch), sp = std::sin(motion.pitch);
      r = Vec3{r.x, cp * r.y - sp * r.z, sp * r.y + cp * r.z};
      const float cy = std::cos(motion.yaw), sy = std::sin(motion.yaw);
      r = Vec3{cy * r.x + sy * r.z, r.y, -sy * r.x + cy * r.z};
      return m::add(r, kPivot);
   }

   constexpr float kYawLimit = 1.31F;  // 75 degrees to each side
   constexpr float kPitchDown = 0.87F; // 50 degrees, down
   constexpr float kPitchUp = 1.05F;   // 60 degrees, up
   constexpr float kRollLimit = 0.61F; // 35 degrees of ear-to-shoulder tilt

   struct HeadKeys {
      bool left{}, right{}, up{}, down{}, tiltLeft{}, tiltRight{};
   };

   void driveHead(HeadMotion &motion, HeadMotion &target, const HeadKeys &keys, float dt) {
      constexpr float aimSpeed = 1.6F; // how fast the target moves while keydown
      constexpr float follow = 10.0F;  // how fast the head catches up with the target
      constexpr float maxSpeed = 5.0F; // the head's  turning speed
      const auto axis = [](bool plus, bool minus) { return (plus ? 1.0F : 0.0F) - (minus ? 1.0F : 0.0F); };
      target.yaw += axis(keys.right, keys.left) * aimSpeed * dt;
      target.pitch += axis(keys.down, keys.up) * aimSpeed * dt;
      target.roll += axis(keys.tiltLeft, keys.tiltRight) * aimSpeed * dt;
      target.yaw = std::clamp(target.yaw, -kYawLimit, kYawLimit);
      target.pitch = std::clamp(target.pitch, -kPitchUp, kPitchDown);
      target.roll = std::clamp(target.roll, -kRollLimit, kRollLimit);
      const auto ease = [&](float &angle, float goal) {
         angle += std::clamp((goal - angle) * follow * dt, -maxSpeed * dt, maxSpeed * dt);
      };
      ease(motion.yaw, target.yaw);
      ease(motion.pitch, target.pitch);
      ease(motion.roll, target.roll);
   }

   // step the hair simulation for one fixed time step 1/60s
   void stepHair(HairStrands &h, const HeadMotion &motion) {
      const Vec3 headOffset = rotateHead(Vec3{0.0F, 0.0F, 0.0F}, motion);
      const float dt = 1.0F / 60.0F;
      const float damping = 0.94F;                          // 94% of the speed each step
      const float shapeStiffness = 0.0F;                    // hair resists stretching
      const Vec3 gravityStep{0.0F, -60.0F * dt * dt, 0.0F}; // gravity impact per step
      const int N = h.pointsPerStrand;
      for (int s = 0; s < h.strandCount; ++s) {
         const std::size_t base = static_cast<std::size_t>(s) * static_cast<std::size_t>(N);

         // 1.point nested loop to move every free point by its own speed plus gravity, root (k = 0) is skipped.
         for (int k = 1; k < N; ++k) {
            const std::size_t i = base + static_cast<std::size_t>(k);
            const Vec3 speed = m::scale(m::subtract(h.pos[i], h.prevPos[i]), damping);
            h.prevPos[i] = h.pos[i];
            h.pos[i] = m::add(m::add(h.pos[i], speed), gravityStep);
         }

         // 2.pin the root to the head.
         h.pos[base] = rotateHead(h.restPos[base], motion);
         h.prevPos[base] = h.pos[base];

         // 3.keep each segment at its rest length. repeat the sweep several times, because fixing one segment disturbs
         // its neighbour
         for (int pass = 0; pass < 6; ++pass) {

            // sweep from root to tip: move each pair of neighbouring points so their distance equals the rest length
            for (int k = 0; k + 1 < N; ++k) {
               const std::size_t a = base + static_cast<std::size_t>(k), b = a + 1;
               const Vec3 delta = m::subtract(h.pos[b], h.pos[a]); // from point a to point b
               const float len = m::length(delta);
               if (len < 1e-6F) {
                  continue;
               }
               const float weights = h.invMass[a] + h.invMass[b]; // the pinned root has weight 0 and does not move
               const Vec3 fix = m::scale(delta, (len - h.segLen[a]) / (len * weights));
               h.pos[a] = m::add(h.pos[a], m::scale(fix, h.invMass[a]));
               h.pos[b] = m::subtract(h.pos[b], m::scale(fix, h.invMass[b]));
            }
            // shape constraint nested loop
            for (int k = 1; k < N; ++k) {
               const std::size_t i = base + static_cast<std::size_t>(k);
               const Vec3 goal = m::add(h.pos[i - 1], m::subtract(h.restPos[i], h.restPos[i - 1]));
               const float stiff =
                   shapeStiffness *
                   (1.0F - static_cast<float>(k) / static_cast<float>(N)); // strong at the root, free at the tip
               h.pos[i] = m::add(h.pos[i], m::scale(m::subtract(goal, h.pos[i]), stiff));
            }
            // collision with t he head nested loop
            for (int k = 1; k < N; ++k) {
               const std::size_t i = base + static_cast<std::size_t>(k);
               const Vec3 fromCenter = m::subtract(h.pos[i], headOffset); // from the head's centre to the particle
               const float dist = m::length(fromCenter);
               const float minDist = 1.05F; // head radius (1.0) + hair thickness
               if (dist < minDist && dist > 1e-6F) {
                  h.pos[i] = m::add(headOffset, m::scale(fromCenter, minDist / dist)); // same direction, exact distance
               }
            }
         }
      }
   }

} // namespace

/**
 * @brief Exercises the selected renderer through the official facade.
 *
 * @return Zero after the configured frame count, non-zero with the failing facade stage.
 */
int main(int argc, char **argv) {
   std::cout << std::unitbuf;
   std::cerr << std::unitbuf;
   std::println("simple_hair_demo engine={}", vve::engineImplementationNamespaceName);
   constexpr float hairWidth = 0.016F; // world units at the root; the head radius is 1
   const int numHairRoots = 12000;   // 12000 strands, each with 20 segments
   constexpr bool hasNose = true;

   auto engine = vve::EngineBuilder<>{}
                     .applicationName("simple_hair_demo")
                     .addWindow(vve::WindowSetup{}
                                    .id("main")
                                    .title("VVE Simple Hair Simulation Demo")
                                    .extent(vve::PixelExtent{.width = 800, .height = 600})
                                    .renderer(vve::RendererId{.value = "forward"}))
                     .build();

   if (const auto result = engine.init(); !result) {
      std::cerr << "simple_hair_demo failed: stage=engine init, error=" << vve::errorName(result.error()) << '\n';
      return 1;
   }
   std::println("simple_hair_demo engine init");

   auto render_system = engine.world().get<vve::RenderSystem>();
   if (!vve::example::selectRenderer(render_system, argc, argv)) {
      return 1;
   }

   if (const auto aa = render_system.setAntialiasing(vve::Antialiasing::msaa4); !aa) {
      std::cerr << "simple_hair_demo: msaa4 failed, error=" << vve::errorName(aa.error()) << '\n';
   }

   // grey background
   render_system.setBackgroundColor(vve::LinearColor{.value = vve::Vec3{0.18F, 0.20F, 0.23F}});

   // sphere head for hair to grow from
   const auto head = render_system.addSphere(1.0F, vve::LinearColor{.value = vve::Vec3{0.75F, 0.55F, 0.45F}});
   if (!head) {
      std::cerr << "simple_hair_demo failed: stage=head sphere, error=" << vve::errorName(head.error()) << '\n';
      return 2;
   }
   const vve::RenderObjectHandle head_handle = *head;
   std::println("simple_hair_demo scene loaded before frame step");
   const Vec3 nose_rest{0.0F, -0.1F, 1.0F};
   std::optional<vve::RenderObjectHandle> nose_handle;
   if (hasNose) {
      const auto nose = render_system.addSphere(0.15F, vve::LinearColor{.value = Vec3{0.8F, 0.3F, 0.3F}},
                                                vve::Transform{.translation = vve::Position{.value = nose_rest}});
      if (!nose) {
         return 8;
      }
      nose_handle = *nose;
   }

   render_system.setDirectionalLight(
       vve::Direction{.value = vve::Vec3{0.4F, -0.6F, -0.7F}}, vve::LinearColor{.value = vve::Vec3{1.0F, 1.0F, 1.0F}},
       vve::LightIntensity{.value = 1.2F}, vve::LinearColor{.value = vve::Vec3{0.15F, 0.15F, 0.17F}});

   auto camera = vve::Camera::lookAt(vve::Position{.value = vve::Vec3{0.0F, 0.3F, 4.0F}},
                                     vve::Position{.value = vve::Vec3{0.0F, 0.0F, 0.0F}});
   camera.clip = vve::ClipPlanes{.near_plane = 1.5F, .far_plane = 12.0F};
   render_system.setCamera(camera);

   const auto roots = makeRoots(numHairRoots, 1.0F);
   auto hair = growHair(roots, 20, 0.05F, 1.0F);
   vve::MeshDescriptor hair_mesh;
   buildRibbons(hair, hairWidth, hair_mesh);
   hair_mesh.base_color_texture = "assets/hair/hair_strands.png";
   std::println("hair: {} strands, {} vertices", hair.strandCount, hair_mesh.positions.size());

   const auto hair_object = render_system.addMesh(hair_mesh);
   if (!hair_object) {
      std::cerr << "simple_hair_demo failed: stage=hair mesh, error=" << vve::errorName(hair_object.error()) << '\n';
      return 5;
   }
   const vve::RenderObjectHandle hair_handle = *hair_object;

   //(void)render_system.setMaterialLighting(vve::MaterialLighting::physically_based);
   (void)render_system.setObjectSurface(hair_handle, vve::SurfaceMaterial{.roughness = 0.5F, .metalness = 0.0F});
   //(void)render_system.setObjectCastsShadow(hair_handle, false);
   //(void)render_system.setObjectUnlit(hair_handle, true);
   const int max_frames = vve::example::frameLimit(argc, argv);
   const std::filesystem::path capture_path = capturePath(argc, argv);
   const std::string capture_path_text = capture_path.string();
   bool captured{};

   const auto render_start_time = std::chrono::steady_clock::now();

   vve::Vector<Vec3> ribbon_positions;
   float sim_time = 0.0F;
   double ms_sim = 0.0, ms_ribbon = 0.0, ms_upload = 0.0;
   HeadMotion head_motion{}; // head current position
   auto window_system = engine.world().get<vve::WindowSystem>();
   const vve::InputState input = window_system.input(); // live view: the current key state
   HeadMotion head_target{};
   HeadKeys head_keys{};
   float accumulator = 0.0F;                // the bucket of unused physics time
   constexpr float fixed_dt = 1.0F / 60.0F; // must match the dt inside stepHair
   auto last_time = std::chrono::steady_clock::now();

   for (int frame{}; max_frames == 0 || frame < max_frames; ++frame) {
      // real elapsed time into the bucket
      const auto now = std::chrono::steady_clock::now();
      accumulator += std::min(std::chrono::duration<float>(now - last_time).count(), 0.1F);
      last_time = now;

      // head movement with key down listeners
      head_keys.left = input.isKeyDown(vve::key('j'));
      head_keys.right = input.isKeyDown(vve::key('l'));
      head_keys.up = input.isKeyDown(vve::key('i'));
      head_keys.down = input.isKeyDown(vve::key('k'));
      head_keys.tiltLeft = input.isKeyDown(vve::key('u'));
      head_keys.tiltRight = input.isKeyDown(vve::key('o'));

      // take out fixed 1/60s chunks and run one physics step per chunk
      int steps = 0;
      const auto t0 = std::chrono::steady_clock::now();
      while (accumulator >= fixed_dt && steps < 2) {
         sim_time += fixed_dt;
         driveHead(head_motion, head_target, head_keys, fixed_dt);
         stepHair(hair, head_motion);
         accumulator -= fixed_dt;
         ++steps;
      }
      const auto t1 = std::chrono::steady_clock::now();

      if (accumulator >= fixed_dt) {
         accumulator = 0.0F;
      } // can't keep up: drop the backlog

      // place the head and the nose where the physics left them
      const Vec3 head_center = rotateHead(Vec3{0.0F, 0.0F, 0.0F}, head_motion);
      (void)render_system.setObjectTransform(head_handle,
                                             vve::Transform{.translation = vve::Position{.value = head_center}});
      if (nose_handle) {
         (void)render_system.setObjectTransform(
             *nose_handle, vve::Transform{.translation = vve::Position{.value = rotateHead(nose_rest, head_motion)}});
      }
      ribbonPositions(hair, hairWidth, head_center, ribbon_positions);
      if (const auto r = render_system.setObjectMeshPositions(hair_handle, ribbon_positions); !r) {
         std::cerr << "simple_hair_demo: hair update failed, error=" << vve::errorName(r.error()) << '\n';
         return 7;
      }
      const auto t2 = std::chrono::steady_clock::now();
      const auto status = engine.step();

      const auto t3 = std::chrono::steady_clock::now();
      ms_sim += std::chrono::duration<double, std::milli>(t1 - t0).count();    // all physics steps this frame
      ms_ribbon += std::chrono::duration<double, std::milli>(t2 - t1).count(); // ribbon corners + mesh update call
      ms_upload += std::chrono::duration<double, std::milli>(t3 - t2).count(); // engine.step(): upload + drawing
      if (frame % 60 == 59) {
         std::println("per frame: physics {:.1f} ms, ribbons {:.1f} ms, engine.step {:.1f} ms", ms_sim / 60,
                      ms_ribbon / 60, ms_upload / 60);
         ms_sim = ms_ribbon = ms_upload = 0.0;
      }

      if (!status) {
         std::cerr << "simple_hair_demo failed: stage=frame, frame=" << (frame + 1)
                   << ", error=" << vve::errorName(status.error()) << '\n';
         return 3;
      }

      // Capture the first completed frame so verification observes the same point in the run.
      if (!captured) {
         const std::expected<void, vve::Error> capture_result = render_system.captureFrameToPng(capture_path);
         if (!capture_result) {
            std::cerr << "simple_hair_demo failed: stage=readback capture, error="
                      << vve::errorName(capture_result.error()) << ", path=" << capture_path_text << '\n';
            return 4;
         }
         captured = true;
         std::println("simple_hair_demo readback png_written=true, path={}", capture_path_text);
      }

      if (*status == vve::FrameStatus::stopped) {
         break;
      }
   }
   const auto render_end_time = std::chrono::steady_clock::now();
   const auto rendered_frames = render_system.renderedFrameCount();
   const double elapsed_seconds = std::chrono::duration<double>{render_end_time - render_start_time}.count();
   const double average_fps = elapsed_seconds > 0.0 ? static_cast<double>(rendered_frames) / elapsed_seconds : 0.0;

   std::println("simple_hair_demo frames={}", max_frames);
   std::println("Rendered {} frames, Render FPS: {:.1f}, Whole-run average FPS: {:.1f}", rendered_frames,
                render_system.renderingFramesPerSecond(), average_fps);
   std::println("simple_hair_demo cleanup done");
   return 0;
}
