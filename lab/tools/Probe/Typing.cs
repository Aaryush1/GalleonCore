// Typing-glow demo (#21): in host mode the keyboard's own type-lighting stops, so the helper has to draw key presses.
// Raw Input (RIDEV_INPUTSINK) gets a copy of each key press, never sits in the input path, so a stall here can't slow typing.
// Key codes stay in memory for the glow only: never logged, never stored. The log gets latency numbers only.
using System.Diagnostics;
using System.Runtime.InteropServices;
using GalleonLink;

namespace Probe;

static class Typing
{
    static readonly Rgb Base = Rgb.Hex("#0f766e"), Glow = Rgb.Hex("#ffffff");
    static TimeSpan Fade = TimeSpan.FromMilliseconds(350);   // first demo: "goes back to normal too fast"

    public static async Task<int> Run(Keyboard kb, double minutes, Log log, CancellationToken stop, double? fadeMs = null)
    {
        if (fadeMs is { } f) Fade = TimeSpan.FromMilliseconds(f);
        var sw = Stopwatch.StartNew();
        var presses = new System.Collections.Concurrent.ConcurrentQueue<(byte usage, TimeSpan at)>();
        using var wake = new SemaphoreSlim(0);
        using var input = new RawKeys((usage, _) => { presses.Enqueue((usage, sw.Elapsed)); wake.Release(); });

        var glowing = new Dictionary<byte, TimeSpan>();      // usage -> press time
        var unshown = new List<TimeSpan>();                  // press times not yet on the keys
        var latencies = new List<double>();
        int frames = 0, retries = 0, unmapped = input.Unmapped;
        log.Note($">>> typing demo for {minutes} min: keys glow white under your fingers and fade over {Fade.TotalMilliseconds:F0} ms.");
        await kb.Show(Frames.FromKeys(Base, new Dictionary<byte, Rgb>()));

        while (sw.Elapsed < TimeSpan.FromMinutes(minutes) && !kb.Frozen)
        {
            stop.ThrowIfCancellationRequested();
            var animating = glowing.Count > 0 || unshown.Count > 0;
            await wake.WaitAsync(animating ? TimeSpan.FromMilliseconds(unshown.Count > 0 ? 5 : 50) : TimeSpan.FromMilliseconds(500), stop);
            while (presses.TryDequeue(out var p)) { glowing[p.usage] = p.at; unshown.Add(p.at); }

            var now = sw.Elapsed;
            foreach (var k in glowing.Where(g => now - g.Value > Fade).Select(g => g.Key).ToList()) glowing.Remove(k);
            if (glowing.Count == 0 && unshown.Count == 0) continue;

            var lit = glowing.ToDictionary(g => g.Key, g => Mix(Base, Glow, 1 - (now - g.Value) / Fade));
            if (await kb.Show(Frames.FromKeys(Base, lit)))
            {
                frames++;
                var done = sw.Elapsed;
                foreach (var t in unshown.Where(t => t <= now)) latencies.Add((done - t).TotalMilliseconds);
                unshown.RemoveAll(t => t <= now);
            }
            else retries++;   // rate cap or a frame in flight: try again in 5 ms
        }

        await kb.Show(Frames.FromKeys(Base, new Dictionary<byte, Rgb>()));
        latencies.Sort();
        double P(double q) => latencies.Count == 0 ? 0 : latencies[Math.Min(latencies.Count - 1, (int)(q * latencies.Count))];
        log.Note($"typing: {latencies.Count} presses shown, {input.Unmapped - unmapped} presses on keys without a mapped LED, {frames} frames, {retries} retries");
        log.Note($"typing: press -> on the keys  p50 {P(0.5):F0} ms  p95 {P(0.95):F0} ms  max {(latencies.Count > 0 ? latencies[^1] : 0):F0} ms");
        return 0;
    }

    static Rgb Mix(Rgb a, Rgb b, double t)
    {
        t = Math.Clamp(t, 0, 1);
        byte L(byte x, byte y) => (byte)Math.Round(x + (y - x) * t);
        return new Rgb(L(a.R, b.R), L(a.G, b.G), L(a.B, b.B));
    }
}
