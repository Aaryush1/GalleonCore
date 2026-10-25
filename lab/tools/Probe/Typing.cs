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
    static readonly TimeSpan Fade = TimeSpan.FromMilliseconds(350);

    public static async Task<int> Run(Keyboard kb, double minutes, Log log, CancellationToken stop)
    {
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

// Raw Input on a message-only window on its own thread. Calls back with the LED index (USB HID usage) of each key-down.
sealed class RawKeys : IDisposable
{
    const uint WM_INPUT = 0x00FF, WM_QUIT = 0x0012, RID_INPUT = 0x10000003, RIDEV_INPUTSINK = 0x100;
    static readonly IntPtr HWND_MESSAGE = new(-3);

    [StructLayout(LayoutKind.Sequential)] struct RAWINPUTDEVICE { public ushort UsagePage, Usage; public uint Flags; public IntPtr Target; }
    [StructLayout(LayoutKind.Sequential)] struct MSG { public IntPtr hwnd; public uint message; public IntPtr wParam, lParam; public uint time; public int x, y; }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)] static extern IntPtr CreateWindowEx(uint ex, string cls, string? name, uint style, int x, int y, int w, int h, IntPtr parent, IntPtr menu, IntPtr inst, IntPtr param);
    [DllImport("user32.dll")] static extern bool DestroyWindow(IntPtr h);
    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterRawInputDevices(RAWINPUTDEVICE[] d, uint n, uint size);
    [DllImport("user32.dll")] static extern uint GetRawInputData(IntPtr h, uint cmd, IntPtr data, ref uint size, uint header);
    [DllImport("user32.dll")] static extern int GetMessage(out MSG m, IntPtr h, uint min, uint max);
    [DllImport("user32.dll")] static extern IntPtr DispatchMessage(ref MSG m);
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint thread, uint msg, IntPtr w, IntPtr l);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();

    readonly Thread _thread;
    readonly Action<byte, bool> _onKey;
    uint _threadId;
    readonly ManualResetEventSlim _ready = new();
    public int Unmapped;

    public RawKeys(Action<byte, bool> onKey)
    {
        _onKey = onKey;
        _thread = new Thread(Loop) { IsBackground = true, Name = "raw-keys" };
        _thread.Start();
        _ready.Wait(TimeSpan.FromSeconds(5));
    }

    void Loop()
    {
        _threadId = GetCurrentThreadId();
        var hwnd = CreateWindowEx(0, "Message", null, 0, 0, 0, 0, 0, HWND_MESSAGE, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero);
        var dev = new[] { new RAWINPUTDEVICE { UsagePage = 1, Usage = 6, Flags = RIDEV_INPUTSINK, Target = hwnd } };
        if (hwnd == IntPtr.Zero || !RegisterRawInputDevices(dev, 1, (uint)Marshal.SizeOf<RAWINPUTDEVICE>()))
            throw new InvalidOperationException($"raw input setup failed: {Marshal.GetLastWin32Error()}");
        _ready.Set();
        var header = (uint)(8 + 2 * IntPtr.Size);
        var buf = Marshal.AllocHGlobal(256);
        try
        {
            while (GetMessage(out var m, IntPtr.Zero, 0, 0) > 0)
            {
                if (m.message == WM_INPUT)
                {
                    uint size = 256;
                    if (GetRawInputData(m.lParam, RID_INPUT, buf, ref size, header) != uint.MaxValue && Marshal.ReadInt32(buf) == 1)   // RIM_TYPEKEYBOARD
                    {
                        var k = buf + (int)header;
                        ushort make = (ushort)Marshal.ReadInt16(k), flags = (ushort)Marshal.ReadInt16(k, 2), vkey = (ushort)Marshal.ReadInt16(k, 6);
                        if ((flags & 1) == 0)   // key down only
                        {
                            if (KeyMap.Usage(vkey, make, (flags & 2) != 0) is { } u) _onKey(u, true); else Unmapped++;
                        }
                    }
                }
                DispatchMessage(ref m);
            }
        }
        finally { Marshal.FreeHGlobal(buf); DestroyWindow(hwnd); }
    }

    public void Dispose() { PostThreadMessage(_threadId, WM_QUIT, IntPtr.Zero, IntPtr.Zero); _thread.Join(TimeSpan.FromSeconds(2)); }
}

// Windows virtual-key -> Galleon LED index. The LED index is the USB HID usage (G2: F1 = 0x3A); modifiers use
// Web Hub's codes (105-111), the Copilot key is 129 (desk-confirmed).
static class KeyMap
{
    public static byte? Usage(ushort vk, ushort make, bool e0)
    {
        if (vk is >= 0x41 and <= 0x5A) return (byte)(0x04 + vk - 0x41);          // A-Z
        if (vk is >= 0x31 and <= 0x39) return (byte)(0x1E + vk - 0x31);          // 1-9
        if (vk is >= 0x70 and <= 0x7B) return (byte)(0x3A + vk - 0x70);          // F1-F12
        if (vk is >= 0x61 and <= 0x69) return (byte)(0x59 + vk - 0x61);          // numpad 1-9
        return vk switch
        {
            0x30 => 0x27, 0x0D => e0 ? (byte)0x58 : (byte)0x28, 0x1B => 0x29, 0x08 => 0x2A, 0x09 => 0x2B, 0x20 => 0x2C,
            0xBD => 0x2D, 0xBB => 0x2E, 0xDB => 0x2F, 0xDD => 0x30, 0xDC => 0x31, 0xBA => 0x33, 0xDE => 0x34, 0xC0 => 0x35,
            0xBC => 0x36, 0xBE => 0x37, 0xBF => 0x38, 0x14 => 0x39,
            0x2C => 0x46, 0x91 => 0x47, 0x13 => 0x48, 0x2D => 0x49, 0x24 => 0x4A, 0x21 => 0x4B, 0x2E => 0x4C, 0x23 => 0x4D, 0x22 => 0x4E,
            0x27 => 0x4F, 0x25 => 0x50, 0x28 => 0x51, 0x26 => 0x52,
            0x90 => 0x53, 0x6F => 0x54, 0x6A => 0x55, 0x6D => 0x56, 0x6B => 0x57, 0x60 => 0x62, 0x6E => 0x63, 0x5D => 101,
            0x11 => e0 ? (byte)109 : (byte)105,
            0x10 => make == 0x36 ? (byte)110 : (byte)106,
            0x12 => e0 ? (byte)111 : (byte)107,
            0x5B => 108,
            _ => null,
        };
    }
}
