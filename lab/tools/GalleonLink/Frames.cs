// Frame builders for resource 0x22. Confirmed format on firmware 2.3.88 (lab/G2-RESULTS.md):
// `12 00` header, then R,G,B per LED, LED index = USB HID usage code (0x3A = F1).
namespace GalleonLink;

public readonly record struct Rgb(byte R, byte G, byte B)
{
    public static readonly Rgb Black = new(0, 0, 0);
    public static Rgb Hex(string h) => new(Convert.ToByte(h[1..3], 16), Convert.ToByte(h[3..5], 16), Convert.ToByte(h[5..7], 16));
}

public static class Frames
{
    public const int Leds = 146;
    public static readonly (byte r, byte g, byte b) Blue = (0, 0, 255), Green = (0, 255, 0), Black = (0, 0, 0);

    // OpenRGB / OpenLinkHub style: `12 00` header, then RGB per LED. This is the one that works.
    public static byte[] Interleaved((byte r, byte g, byte b) c, int n = Leds, int only = -1)
    {
        var f = new byte[2 + 3 * n]; f[0] = 0x12;
        for (int i = 0; i < n; i++)
        {
            var k = only < 0 || i == only ? c : Black;
            f[2 + 3 * i] = k.r; f[3 + 3 * i] = k.g; f[4 + 3 * i] = k.b;
        }
        return f;
    }

    // galleon-deck style: all R, then all G, then all B, no header. Kept for the record; not used.
    public static byte[] Planar((byte r, byte g, byte b) c, int n = Leds)
    {
        var f = new byte[3 * n];
        Array.Fill(f, c.r, 0, n); Array.Fill(f, c.g, n, n); Array.Fill(f, c.b, 2 * n, n);
        return f;
    }

    public static byte[] ForVariant(string v, (byte, byte, byte) c) => v switch
    {
        "interleaved" => Interleaved(c),
        "planar146" => Planar(c),
        "planar138" => Planar(c, 138),
        _ => throw new ArgumentException($"unknown variant {v} (interleaved | planar146 | planar138)"),
    };

    // The frame the helper uses: a background color, with chosen keys (by HID usage) overridden.
    // Usages outside 0..Leds-1 are rejected rather than silently dropped.
    public static byte[] FromKeys(Rgb background, IReadOnlyDictionary<byte, Rgb> keys)
    {
        var f = new byte[2 + 3 * Leds]; f[0] = 0x12;
        for (int i = 0; i < Leds; i++) Put(f, i, background);
        foreach (var (usage, c) in keys)
        {
            if (usage >= Leds) throw new ArgumentOutOfRangeException(nameof(keys), $"LED index 0x{usage:X2} is outside 0..{Leds - 1}");
            Put(f, usage, c);
        }
        return f;
    }

    static void Put(byte[] f, int i, Rgb c) { f[2 + 3 * i] = c.R; f[3 + 3 * i] = c.G; f[4 + 3 * i] = c.B; }
}
