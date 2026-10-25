// G3 desk checks (lab/G3-PLAN.md). All go through GalleonLink.Keyboard, so its safety rules apply.
using System.Diagnostics;
using GalleonLink;

namespace Probe;

static class G3
{
    static readonly Rgb Base = Rgb.Hex("#0f766e"), Blue = Rgb.Hex("#2563eb"), Magenta = Rgb.Hex("#ff00ff"), Off = Rgb.Black;

    public static async Task<int> Run(string step, string lab, Log log, CancellationToken stop)
    {
        using var kb = await Keyboard.Connect(log);
        switch (step)
        {
            case "g3-crash":   // D4 with the watchdog: Guard must restore the onboard profile within 2 s
            {
                var guard = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "Guard", "bin", "Release", "net10.0-windows", "Guard.exe"));
                Process.Start(new ProcessStartInfo(guard, Environment.ProcessId.ToString()) { UseShellExecute = false, CreateNoWindow = true });
                log.Note(">>> guard started. Streaming blue for 5 s, then this process dies WITHOUT releasing.");
                await Stream(kb, _ => Frames.FromKeys(Blue, new Dictionary<byte, Rgb>()), TimeSpan.FromSeconds(5), stop);
                log.Note($">>> CRASHING NOW at {DateTime.Now:HH:mm:ss.fff}. The keys should go back to your green within 2 s.");
                Environment.FailFast("deliberate crash for the G3 watchdog test");
                return 0;
            }

            case "g3-sweep":   // which LED indices are not keys: light them in groups of 8, magenta, everything else off
            {
                var keys = KnownKeys(lab);
                var unknown = Enumerable.Range(0, Frames.Leds).Select(i => (byte)i).Where(i => !keys.Contains(i)).ToList();
                log.Note($"{keys.Count} indices are keys in his layers; sweeping the other {unknown.Count}");
                foreach (var group in unknown.Chunk(8))
                {
                    stop.ThrowIfCancellationRequested();
                    log.Note($">>> GROUP 0x{group[0]:X2}..0x{group[^1]:X2} ({string.Join(" ", group.Select(g => g.ToString("X2")))}) lit magenta for 6 s: what lights?");
                    await Stream(kb, _ => Frames.FromKeys(Off, group.ToDictionary(g => g, _ => Magenta)), TimeSpan.FromSeconds(6), stop);
                }
                return 0;
            }

            case "g3-endurance":   // D2 + D3: 20 fps for 10 min, a wave moving across the keys; he types for 5 of them
            {
                log.Note(">>> 10-minute run at 20 fps. Type continuously for 5 minutes at some point; glance at the start and end.");
                var shown = await Stream(kb, n => Wave(n), TimeSpan.FromMinutes(10), stop);
                log.Note($"endurance: {shown} frames shown, frozen={kb.Frozen}");
                return kb.Frozen ? 20 : 0;
            }

            default: log.Note($"unknown step {step}"); return 2;
        }
    }

    // Offer frames as fast as the 20 fps cap allows; returns how many were shown.
    static async Task<int> Stream(Keyboard kb, Func<int, byte[]> frame, TimeSpan length, CancellationToken stop)
    {
        // Pace by deadline, not by a fixed sleep after each frame: the first endurance run slept 50 ms
        // on top of each write and only reached 12.8 fps.
        var sw = Stopwatch.StartNew(); int shown = 0, n = 0;
        var next = TimeSpan.Zero;
        while (sw.Elapsed < length && !kb.Frozen)
        {
            stop.ThrowIfCancellationRequested();
            if (await kb.Show(frame(n++))) shown++;
            next += TimeSpan.FromMilliseconds(50);
            var wait = next - sw.Elapsed;
            if (wait > TimeSpan.Zero) await Task.Delay(wait, stop); else next = sw.Elapsed;   // running late: don't try to catch up
        }
        return shown;
    }

    // A slow blue band moving across the HID usage range over the base color.
    static byte[] Wave(int n)
    {
        var head = (n / 2) % Frames.Leds;
        var lit = Enumerable.Range(0, 6).Select(k => (byte)((head + k) % Frames.Leds)).ToDictionary(i => i, _ => Blue);
        return Frames.FromKeys(Base, lit);
    }

    // The LED ids that his onboard layers treat as keys (layer files from the latest read-only backup).
    static HashSet<byte> KnownKeys(string lab)
    {
        var latest = Directory.GetDirectories(Path.Combine(lab, "backups")).OrderDescending().First();
        var keys = new HashSet<byte>();
        foreach (var f in Directory.GetFiles(latest, "file-2800*.bin"))
        {
            var b = File.ReadAllBytes(f);
            if (b.Length < 7 || (b[0], b[1]) is not ((0x7e, 0x20) or (0xf9, 0xb1))) continue;   // static / type-lighting layers only
            int colors = b[6], at = 7 + 4 * colors;
            if (at >= b.Length) continue;
            int count = b[at];
            for (int i = 0; i < count && at + 1 + i < b.Length; i++) keys.Add(b[at + 1 + i]);
        }
        return keys;
    }
}
