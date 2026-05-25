The closest match to your system: MilkDrop / projectM

MilkDrop is probably the most directly relevant ecosystem. It is not ARTNet-based in concept; it is an audio-reactive equation engine. Its preset authoring guide explicitly uses audio variables such as bass, mid, treb, and polar-like per-pixel/per-vertex variables such as rad and ang; the guide even shows equations such as loudness-driven zoom and zoom = zoom + rad*0.1 for spatially varying motion.  ￼

projectM is a modern open-source reimplementation of MilkDrop; its own description says it transforms music into equations and renders user-contributed visualizations.  ￼ That is exactly the mental model you want: audio changes parameters of a continuously evolving field, not isolated star/heart/explosion sprites.

What to extract from MilkDrop presets:

per_frame      -> your animation update()
per_pixel      -> your per-LED shader()
rad, ang       -> your r, θ
bass/mid/treb  -> your smoothed audio bands
zoom/rot/warp  -> your polar feedback displacement
q1..qN         -> your precomputed per-frame parameters

The single most valuable MilkDrop idea for you is feedback warping:

new_color(p) =
    decay * sample(previous_canvas, warped_position(p, audio, time))
  + injection(p, audio, time)

That one idea will immediately look more “installation-like” than most primitive LED effects because it creates persistence, smearing, turbulence, and evolving structure.

⸻

Coordinate model to standardize everything

Precompute this once:

theta = 2π * spoke / 16.0 + gyro_yaw_offset;
r     = (radial_index + 0.5) / 50.0;
x = r * cos(theta);
y = r * sin(theta);

Then every animation can expose this signature:

Color sample(float x, float y, float r, float theta,
             float t,
             Audio a,
             State* state);

For your shape, polar-native effects will usually look better than rectangular-image effects. With only 16 angular samples, avoid high angular frequencies. As a rule of thumb, keep sin(n * theta) mostly below n = 8, and often below n = 5, unless you intentionally want spoke aliasing.

⸻

High-value animation families to implement

1. MilkDrop-style polar feedback warp

This should be your first serious implementation.

Keep one previous canvas. For each LED:

float swirl = 0.15 * sin(6.0 * r - 2.0 * t + 3.0 * mid);
float zoom  = 1.0 - 0.015 * bass;
float rot   = 0.02 * sin(0.3 * t) + 0.05 * beat_bass;
float r2     = clamp(r * zoom + 0.02 * sin(theta * 3.0 + t), 0, 1);
float theta2 = theta + rot + swirl * (1.0 - r);
Color old = sample_previous_polar(r2, theta2);
Color inj = palette(theta / TAU + 0.2 * noise3(x, y, t))
          * ring_pulse(r, bass_phase);
out = old * 0.92 + inj * 0.35;

Why it works: the visual does not reset every beat. It carries memory, then music bends that memory. That is the core trick behind many classic music visualizers.

Use bass for zoom/implosion, mids for swirl/warp, highs for sparkle injection, gyro for hue or global rotation.

⸻

2. Domain-warped noise / fBM fields

Shader art is full of this. The Book of Shaders has exactly the material you need: shaping functions, colors, shapes, matrices, random, noise, cellular noise, fractional Brownian motion, and fractals.  ￼ Its noise chapter specifically frames noise as a way to animate organic-looking shapes and dancing compositions.  ￼

Core formula:

vec2 p = vec2(x, y);
vec2 q = p;
q += 0.25 * vec2(
    noise3(2.0*p.x, 2.0*p.y, 0.2*t),
    noise3(2.0*p.x + 17.1, 2.0*p.y - 4.3, 0.2*t)
);
float v = fbm(4.0 * q + vec2(0.0, 0.15 * t));
v += 0.3 * sin(10.0 * r - 2.0 * t + 4.0 * bass);
Color c = cosine_palette(v + hue_offset);
alpha = smoothstep(0.35, 0.85, v);

Looks like: aurora, liquid glass, plasma, smoke, nebula, living fabric.

For your fog machine, this is a natural match: slow domain-warped low-brightness layers behind sharper beat-driven layers.

⸻

3. Polar kaleidoscope / mandala fields

Your physical object is radial, so exploit radial symmetry. Kaleidoscope shaders often convert to polar coordinates, fold the angle into repeated mirrored segments, then sample or generate a pattern inside the folded wedge. A good tutorial describes this exact polar-coordinate reflection approach.  ￼

Core fold:

float segments = 8.0;                 // try 4, 8, 16
float a = mod(theta + rot, TAU / segments);
a = abs(a - 0.5 * TAU / segments);    // mirror within segment
float v =
    sin(18.0 * r - 3.0 * t)
  + sin(7.0 * a + 2.0 * fbm(vec2(r, a) * 4.0));
v = smoothstep(0.2, 0.8, 0.5 + 0.5 * v);

Make it less “screensaver” by keeping one slowly evolving parameter set for 20–60 seconds, then morphing to another. Avoid changing every parameter on every beat.

⸻

4. Reaction-diffusion / Gray-Scott organisms

This is a real algorithmic pattern family, not an LED cliché. Karl Sims’ Gray-Scott tutorial gives the standard two-chemical update idea and typical parameters such as DA=1.0, DB=.5, f=.055, k=.062, a 3×3 Laplacian, and initialization with one substance seeded into another.  ￼

Equations:

A' = A + (DA * laplace(A) - A*B*B + f*(1-A)) * dt
B' = B + (DB * laplace(B) + A*B*B - (k+f)*B) * dt

LED adaptation:

// logical grid: 16 x 50, wrap theta, clamp or reflect r
A[s][i], B[s][i]
brightness = smoothstep(0.1, 0.9, B[s][i]);
hue = base_hue + 0.25 * B[s][i] + 0.05 * theta;

Audio controls:

bass hit  -> inject B into several radial positions
mid       -> modulate feed rate f
treble    -> add tiny seeds / speckles
gyro      -> rotate sampling, not necessarily the simulation itself

This will look like biological lace, coral, veins, or chemical bloom. It is stateful, so it has visual maturity.

⸻

5. Multi-scale Turing patterns

This is related to reaction-diffusion but often more controllable for art. The Softology writeup explains a practical algorithm: keep a grid, compute activator and inhibitor averages at several radii, measure variation per scale, pick the scale with the smallest variation, then nudge the cell up or down and normalize.  ￼

Core update:

for each pixel:
  for each scale:
    activator = average(grid, small_radius[scale])
    inhibitor = average(grid, large_radius[scale])
    variation = abs(activator - inhibitor)
  best_scale = scale with smallest variation
  if activator[best] > inhibitor[best]:
      grid += small_amount[best]
  else:
      grid -= small_amount[best]
normalize grid

Why this is good for your installation: it creates organic multi-scale texture without needing many pixels. Run it on a small logical canvas such as 32×64 or directly on 16×50; sample into the LED surface.

Use audio to choose which scale gets colored or energized. For example, bass biases large scales, treble biases small scales.

⸻

6. Flow-field particles with trails

Flow fields are a staple of generative art. Tyler Hobbs describes the basic structure as a grid where each point stores an angle; particles then follow those angles to create expressive curves.  ￼

Use 50–200 virtual particles, not one particle per LED.

angle = TAU * noise3(p.x * freq, p.y * freq, t * 0.1);
velocity = vec2(cos(angle), sin(angle)) * speed;
particle.pos += velocity * dt;
particle.pos = wrap_or_reflect(particle.pos);
splat(canvas, particle.pos, color, radius);
canvas *= 0.94;   // trails

Audio mapping:

bass   -> particle speed / radial outward force
mid    -> curl strength
treble -> particle birth rate or trail brightness
onset  -> inject a vortex or attractor for 0.2 sec

This is one of the best ways to give artistic friends control: let them tune “calm/current/turbulence/swarm density/trail decay” instead of formulas.

⸻

7. Boids / flocking, but rendered as light traces

Processing’s flocking example implements Craig Reynolds-style boids where each agent steers by avoidance, alignment, and coherence.  ￼ Do not render literal birds. Render the density, trails, and pressure waves of the flock.

for each boid:
  acceleration =
      w_sep * separation(neighbors)
    + w_align * alignment(neighbors)
    + w_cohesion * cohesion(neighbors)
    + w_audio * force_from_audio_event
  velocity += acceleration
  position += velocity
  splat(position)

On your surface, this can become “murmuration on a radial sculpture”: clusters orbit, split, collapse inward on bass, and scatter on high-frequency hits.

Keep boid count modest: 32–96 agents is enough.

⸻

8. Signed-distance-field shapes, but not icons

Signed distance fields are functions that return distance to a shape boundary; negative values are inside, positive outside, and zero is the contour. This makes soft outlines, glows, morphs, and boolean combinations easy.  ￼

Avoid “heart/star/explosion.” Use SDFs for abstract geometry:

float d1 = abs(length(p) - radius);                      // ring
float d2 = abs(sin(6.0 * theta + 2.0 * t)) - 0.2;         // radial petals
float d  = max(d1, d2);                                  // intersection-ish
float line = 1.0 - smoothstep(0.0, width, abs(d));
float glow = exp(-8.0 * abs(d));
out = palette(theta + t*0.05) * (line + 0.4 * glow);

Good SDF concepts for your geometry:

breathing architectural rings
rotating apertures
iris / camera shutter
radial stained glass
soft geometric masks over noisy backgrounds
morphing polygonal halos

This gives structure to otherwise mushy noise.

⸻

9. Voronoi / Worley “living cells”

The Book of Shaders has a cellular-noise chapter, and Worley/Voronoi noise is a common procedural texture family.  ￼ The useful visual variable is often the difference between nearest and second-nearest seed distances:

float F1, F2 = nearest_two_seed_distances(p, moving_seeds);
float edge = smoothstep(0.02, 0.0, F2 - F1);
float cell_fill = smoothstep(0.1, 0.8, F1);
Color c = palette(seed_id + 0.1 * t) * edge;

Looks like: cracked glass, cells, honeycomb, crystal growth, stained glass, electric membranes.

Audio mapping:

bass   -> push seeds outward
mid    -> rotate seed velocity field
treble -> brighten cell borders

On a radial LED layout, Voronoi can alias if the cells are too small. Use large cells: maybe 5–12 seeds total.

⸻

10. Wave interference / cymatics-inspired fields

This is mathematically simple but can look much richer than “wave from beat” when you layer several wave sources.

float v = 0.0;
for each source j:
    float d = distance(p, source[j].pos);
    v += amp[j] * sin(k[j] * d - omega[j] * t + phase[j]);
v += 0.4 * sin(angular_mode * theta + radial_mode * r - t);
float lines = smoothstep(0.92, 1.0, abs(sin(v)));

Use standing-wave modes:

v = sin(n * theta + phase1) * sin(m * PI * r + phase2);

Audio mapping:

bass  -> low radial mode m = 1..3
mid   -> angular mode n = 2..7
high  -> threshold sharpness / shimmer

This is a good “music looks like physics” family.

⸻

11. Strange attractors and parametric traces

Use a virtual particle whose position is governed by an attractor, then splat it into the LED canvas with decay. Examples include Lorenz, Clifford, De Jong, Ikeda, and Lissajous-like systems.

Clifford attractor:

x_next = sin(a*y) + c*cos(a*x)
y_next = sin(b*x) + d*cos(b*y)

Then map (x, y) to your polar surface and draw trails. Audio should not randomly slam the coefficients every frame; instead, slowly morph them:

a = lerp(a, target_a + 0.05 * bass, 0.002);

Looks like: calligraphy, orbital insects, comet trails, unstable handwriting.

⸻

12. Lenia / continuous cellular automata

Lenia is a continuous cellular automaton derived from Game of Life by making the state, space, and time smooth and continuous; the project page describes lifelike self-organization, radial symmetries, self-repair, locomotion, and chaotic behavior.  ￼

This is more tuning-heavy than Gray-Scott, but potentially very good for “living” installations. I would not start here unless you enjoy parameter hunting.

A simplified Lenia-like update:

U = convolution(state, radial_kernel)
growth = exp(-((U - mu)^2) / (2*sigma^2)) * 2 - 1
state = clamp(state + dt * growth, 0, 1)

Audio can perturb mu, sigma, or seed new organisms.

⸻

Resource map: where to look

Resource family	Why it matters for your project
MilkDrop / projectM presets	Closest conceptual match: audio-reactive equations with polar variables, feedback, warping, per-frame/per-pixel logic.  ￼
The Book of Shaders	Best foundation for turning (x, y, t) into color using shaping functions, noise, cellular noise, fBM, and fractals.  ￼
ISF / Shadertoy-style shaders	ISF formalizes passing waveform and FFT data into shaders; the mental model maps cleanly to your per-LED canvas.  ￼
Pixelblaze patterns	LED-native pattern thinking; its 2D/3D render functions use mapped coordinates rather than wiring order, which is similar to your abstraction layer.  ￼
Reaction-diffusion / Turing patterns	Produces organic, non-iconic evolving texture from small state grids.  ￼
Flow fields / boids / agents	Gives motion that looks intentional without keyframing.  ￼
SDF shader art	Gives crisp structure, masks, glows, outlines, morphing geometry.  ￼

⸻

What I would build first

First: a proper “field engine”

Before adding more named effects, implement a small shared toolkit:

noise2/noise3
fbm
smoothstep pulse
ring pulse
angular fold / kaleidoscope fold
cosine palette
sample_previous_polar
splat particle into canvas
HSV/RGB blending with gamma-aware output

Then implement only four “serious” layers:

1. polar feedback warp
2. domain-warped noise field
3. reaction-diffusion or multi-scale Turing state
4. flow-field particle trails

Those four will outperform twenty object-style animations.

Second: use audio as continuous parameter pressure

Do this:

bass envelope     -> spatial scale, zoom, radial force
bass onset        -> inject energy / seed / impulse
mid envelope      -> rotation, swirl, turbulence
high envelope     -> sparkle density, edge sharpness
overall RMS       -> brightness / contrast
tempo phase       -> slow breathing, not hard resets

Avoid this:

bass hit -> start explosion sprite
snare    -> star sprite
hihat    -> twinkle sprite

The latter almost always looks like an engineering demo.

Third: add curation tooling for artistic friends

Do not ask artistic friends to express themselves as formulas. Give them 8–12 named sliders per algorithm:

mood
density
turbulence
symmetry
decay
glow
contrast
palette
motion speed
beat sensitivity

Then add a randomize/mutate button and save presets. This matches how generative-art systems are actually curated: define a rule space, explore it, then select the compelling results. Leo Villareal’s LED work is a useful reference point here: his practice is described as building from simple pixels/rules, using chance and emergent behavior, then selecting and refining compelling sequences through layering operations.  ￼

⸻

Specific design recommendation for your surface

Make the whole piece feel like a radial instrument, not a low-resolution display.

A strong default scene stack could be:

Layer 0: slow domain-warped ember/aurora background
Layer 1: polar feedback warp carrying memory from previous frames
Layer 2: reaction-diffusion/Turing “organism” texture, low opacity
Layer 3: flow-field particles, additive, audio-responsive
Layer 4: rare SDF geometry masks: aperture, iris, mandala, rings
Layer 5: incandescent bulbs as slow macro accents, not beat strobes
Layer 6: fog triggered by sustained high energy or breakdown transitions

That gives you a vocabulary:

ambient    -> noise + slow feedback
build      -> increase flow speed, contrast, angular symmetry
drop       -> bass zoom impulse + bright injection + fog/bulbs
breakdown  -> decay trails, reduce particle birth, show Turing texture
climax     -> feedback warp + particles + SDF aperture all active

The main correction is conceptual: stop thinking in primitives and start thinking in evolving fields with memory. Your hardware and buffer architecture are already well suited for that.
