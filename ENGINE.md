# Preserving the original engine on Android

## Establishing a Windows reference

The original Windows game is the reference for simulation, animation, visibility, and rendering. We first exercised well-defined pieces alongside that version. The animation work followed data from the actor and motion graph through the job scheduler, blend commands, hierarchy and transform writes, bone palettes, and textured mesh submission. Those comparisons caught differences that a screenshot or successful compile could miss.

The [archived research](https://github.com/Mokomis/open-amalur) documents the earlier custom engine, graphics translation, format research, and initial x86 translation work. Later experiments ran matched inputs on Windows and on ARM64. Each result covered the code and inputs exercised in its test.

## Translating and hosting the game

The current Android build uses an ahead-of-time x86-to-C-to-ARM64 path for the supported Re-Reckoning Windows executable. Important work included checking instruction and flag semantics, calling conventions, floating-point behavior, temporary state, and how the translated code accesses guest memory. The game still uses its own data formats and scripts.

Android hosts the translated engine with adapters for the Windows services it calls. Those cover module loading, file access, timing, threading, synchronization and input. Direct3D 11 work goes through a Vulkan graphics layer. We tested device rotation, surface recreation and swapchain lifetime as part of getting the result onto an Android screen. Local saves use an offline profile and storage adapter.

## Threading and frame pacing

1. **Make the guest's thread primitives behave correctly.** Compare waits, signals, semaphore counts and job completion against Windows. Preserve the engine's publication order and the ordering that its x86 code expects on ARM.
2. **Measure where time goes.** Use Perfetto signals and Simpleperf call stacks to distinguish blocked time, CPU work and scheduler delay. In the tested OPPO scenes, the draw and simulation threads were the two largest CPU consumers. Workers also ran concurrently.
3. **Compare scheduler changes in the same scene.** Check actual CPU masks, clocks, thermal state, draw counts, and frame times. On the test tablet, forcing both busiest threads onto the two highest-capacity cores could reduce throughput. Giving them access to the larger core group sustained the target frame rate in one long matched trial and reduced peak core temperatures in that trial.
4. **Reduce avoidable work.** Target wait notifications to the object that changed and check the semaphore handoff under contention. Improve resource mapping by tracking the occupied range under the existing lock. Qualify targeted changes against original outputs, then exercise them in the running game. Several alternatives passed isolated correctness checks but offered no repeatable in-game improvement.
5. **Measure what reaches the display.** Separate renderer throughput from displayed frames. Keep capture-replay, stationary scenes, combat and sustained gameplay results distinct.

These are device- and scene-specific findings. They guide the Android build and help prioritize future work on compatibility and performance.
