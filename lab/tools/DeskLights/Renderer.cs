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

    public const byte Copilot = 0x81, StripFirst = 0x82;
    public static readonly TimeSpan BreathePeriod = TimeSpan.FromSeconds(4), BlinkLength = TimeSpan.FromMilliseconds(600);

    // Breathe brightness: 1.0 at the start of each 4 s cycle, down to 0.2 halfway, back up.
    public static double BreatheLevel(TimeSpan t)
    {
        var phase = t.TotalSeconds % BreathePeriod.TotalSeconds / BreathePeriod.TotalSeconds;
        return 0.2 + 0.8 * (0.5 + 0.5 * Math.Cos(2 * Math.PI * phase));
    }

    // Blink: on for 600 ms from the moment the scene first arrived.
    public static bool BlinkOn(TimeSpan now, TimeSpan receivedAt)
    {
        var age = now - receivedAt;
        return age >= TimeSpan.Zero && age < BlinkLength;
    }

    // The color a zone shows, or null for "nothing of its own" (a blink that has ended): keys fall back to the base.
    static Rgb? ShowOrNull(Zone z, TimeSpan now, TimeSpan receivedAt) => z.Effect switch
    {
        Effect.Off => Rgb.Black,
        Effect.Pulse => Scale(z.Color, PulseLevel(now)),
        Effect.Breathe => Scale(z.Color, BreatheLevel(now)),
        Effect.Blink => BlinkOn(now, receivedAt) ? z.Color : null,
        _ => z.Color,
    };

    // Colours for every LED that differs from the base: F-keys, Copilot, strip, then the typing glow on top.
    // `keysLevel` dims the base only, so a key with no color of its own (and the strip's null entries) shows the dimmed base.
    public static Dictionary<byte, Rgb> KeyColors(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now)
    {
        var lit = new Dictionary<byte, Rgb>();
        for (int i = 0; i < 12; i++)
            if (s.FKeys[i] is { } z && ShowOrNull(z, now, s.ReceivedAt) is { } c0) lit[(byte)(F1 + i)] = c0;
        if (s.Copilot is { } cz && ShowOrNull(cz, now, s.ReceivedAt) is { } c1) lit[Copilot] = c1;
        if (s.Strip is { } strip)
            for (int i = 0; i < strip.Length; i++)
                if (strip[i] is { } c2) lit[(byte)(StripFirst + i)] = c2;
        var baseColor = BaseColor(s);
        if (s.FadeMs > 0)
            foreach (var (usage, at) in presses)
            {
                var age = (now - at).TotalMilliseconds;
                if (age < 0 || age >= s.FadeMs || usage >= Frames.Leds) continue;
                var under = lit.TryGetValue(usage, out var c) ? c : baseColor;
                lit[usage] = Mix(under, s.Glow, 1 - age / s.FadeMs);
            }
        return lit;
    }

    public static Rgb BaseColor(Scene s) => Scale(s.Base, s.KeysLevel);

    public static byte[] KeyFrame(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now) =>
        Frames.FromKeys(BaseColor(s), KeyColors(s, presses, now));

    static bool ZoneAnimating(Zone? z, TimeSpan now, TimeSpan receivedAt) => z?.Effect switch
    {
        Effect.Pulse or Effect.Breathe => true,
        // A little past the end so the frame that switches the blink off is rendered promptly.
        Effect.Blink => now - receivedAt < BlinkLength + TimeSpan.FromMilliseconds(100),
        _ => false,
    };

    // True while the keys change over time on their own (pulsing/breathing/blinking zone or a fading glow): render at 20 fps.
    public static bool KeysAnimating(Scene s, IReadOnlyDictionary<byte, TimeSpan> presses, TimeSpan now) =>
        s.FKeys.Any(z => ZoneAnimating(z, now, s.ReceivedAt)) || ZoneAnimating(s.Copilot, now, s.ReceivedAt) ||
        (s.FadeMs > 0 && presses.Values.Any(at => (now - at).TotalMilliseconds < s.FadeMs));

    // Same for the dials and the mouse.
    public static bool DevicesAnimating(Scene s, TimeSpan now) =>
        ZoneAnimating(s.LeftDial, now, s.ReceivedAt) || ZoneAnimating(s.RightDial, now, s.ReceivedAt) || ZoneAnimating(s.Mouse, now, s.ReceivedAt);

    // What a dial or the mouse shows: a blink that has ended is dark.
    public static Rgb Dial(Zone z, Scene s, TimeSpan now) => ShowOrNull(z, now, s.ReceivedAt) ?? Rgb.Black;
}
