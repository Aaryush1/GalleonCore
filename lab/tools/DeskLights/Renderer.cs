// Turns a scene plus recent key presses into what each light shows at a given moment. Pure, so it's tested offline.
using GalleonLink;

namespace DeskLights;

public static class Renderer
{
    public const byte F1 = 0x3A;   // LED index = USB HID usage; F1..F12 = 0x3A..0x45 (desk-confirmed for F1)
    static readonly TimeSpan PulsePeriod = TimeSpan.FromSeconds(1);

    // Pulse brightness: 1.0 at the start of each period, down to 0.35 halfway, back up.
    public static double PulseLevel(TimeSpan t)
    {
        var phase = t.TotalSeconds % PulsePeriod.TotalSeconds / PulsePeriod.TotalSeconds;
        return 0.35 + 0.65 * (0.5 + 0.5 * Math.Cos(2 * Math.PI * phase));
    }

    public static Rgb Scale(Rgb c, double k)
    {
        k = Math.Clamp(k, 0, 1);
        return new Rgb((byte)Math.Round(c.R * k), (byte)Math.Round(c.G * k), (byte)Math.Round(c.B * k));
    }

    public static Rgb Mix(Rgb a, Rgb b, double t)
    {
        t = Math.Clamp(t, 0, 1);
        byte L(byte x, byte y) => (byte)Math.Round(x + (y - x) * t);
        return new Rgb(L(a.R, b.R), L(a.G, b.G), L(a.B, b.B));
    }

    static Rgb Show(Zone z, TimeSpan now) => z.Effect switch
    {
        Effect.Off => Rgb.Black,
        Effect.Pulse => Scale(z.Color, PulseLevel(now)),
        _ => z.Color,
    };

    // Colours for every LED that differs from the base: F-keys, then the typing glow on top.
    public static Dictionary<byte, Rgb> KeyColors(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now)
    {
        var lit = new Dictionary<byte, Rgb>();
        for (int i = 0; i < 12; i++)
            if (s.FKeys[i] is { } z) lit[(byte)(F1 + i)] = Show(z, now);
        if (s.FadeMs > 0)
            foreach (var (usage, at) in presses)
            {
                var age = (now - at).TotalMilliseconds;
                if (age < 0 || age >= s.FadeMs || usage >= Frames.Leds) continue;
                var under = lit.TryGetValue(usage, out var c) ? c : s.Base;
                lit[usage] = Mix(under, s.Glow, 1 - age / s.FadeMs);
            }
        return lit;
    }

    public static byte[] KeyFrame(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now) =>
        Frames.FromKeys(s.Base, KeyColors(s, presses, now));

    // True while the keys change over time on their own (pulsing F-key or a fading glow): render at 20 fps.
    public static bool KeysAnimating(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now) =>
        s.FKeys.Any(z => z?.Effect == Effect.Pulse) ||
        (s.FadeMs > 0 && presses.Values.Any(at => (now - at).TotalMilliseconds < s.FadeMs));

    public static Rgb Dial(Zone z, TimeSpan now) => Show(z, now);
}
