// What the plugin asks the lights to show (lab/DESK-LIGHTS-PROTOCOL.md). Parsing is pure and all-or-nothing:
// a scene with any problem is rejected whole, so the keys never show half of one.
using System.Text.Json;
using GalleonLink;

namespace DeskLights;

public enum Effect { Steady, Pulse, Off, Breathe, Blink }

public sealed record Zone(Rgb Color, Effect Effect)
{
    public static readonly Zone Dark = new(Rgb.Black, Effect.Off);
}

public sealed record Scene(long Seq, Rgb Base, Zone?[] FKeys, Zone LeftDial, Rgb Glow, int FadeMs)
{
    // Protocol v2 fields; each default is what an absent field means.
    public Zone RightDial { get; init; } = Zone.Dark;
    public Zone? Copilot { get; init; }          // null = base
    public Rgb?[]? Strip { get; init; }          // null = base; else 16 entries for LEDs 0x82..0x91 (null entry = base)
    public double KeysLevel { get; init; } = 1.0; // multiplies the base color only
    public Zone? Mouse { get; init; }            // null = don't drive the mouse

    // When the helper first received a scene with this seq (helper clock). `blink` counts from here.
    public TimeSpan ReceivedAt { get; init; }

    // A re-sent scene with the same seq keeps the original arrival time, so its blink doesn't fire again.
    public static Scene Adopt(Scene? previous, Scene incoming, TimeSpan now) =>
        incoming with { ReceivedAt = previous is not null && previous.Seq == incoming.Seq ? previous.ReceivedAt : now };

    // Before the first scene, and whenever the plugin has been quiet for 30 s: nothing that could be stale.
    public static Scene Neutral(Rgb glow, int fadeMs) =>
        new(-1, Rgb.Hex("#115e59"), new Zone?[12], new Zone(Rgb.Black, Effect.Off), glow, fadeMs);

    public static readonly Scene Initial = Neutral(Rgb.Hex("#ffffff"), 350);
}

public sealed class SceneException(string why) : Exception(why);

public static class Protocol
{
    public const int MaxLine = 64 * 1024;
    static readonly HashSet<string> SceneFields = ["type", "seq", "base", "fkeys", "leftDial", "typing", "rightDial", "copilot", "strip", "keysLevel", "mouse"];

    public enum Kind { Hello, Heartbeat, Scene, Unknown }

    // Returns the kind of line; for a scene, also the parsed scene. Throws SceneException for anything malformed.
    public static (Kind kind, Scene? scene) Parse(string line)
    {
        if (line.Length > MaxLine) throw new SceneException("line too long");
        JsonDocument doc;
        try { doc = JsonDocument.Parse(line); }
        catch (JsonException e) { throw new SceneException($"not JSON: {e.Message}"); }
        using (doc)
        {
            var root = doc.RootElement;
            if (root.ValueKind != JsonValueKind.Object || !root.TryGetProperty("type", out var t) || t.ValueKind != JsonValueKind.String)
                throw new SceneException("no type");
            switch (t.GetString())
            {
                case "hello":
                    if (!root.TryGetProperty("protocol", out var p) || p.ValueKind != JsonValueKind.Number || !p.TryGetInt32(out var ver) || ver is not (1 or 2))
                        throw new SceneException("unsupported protocol (want 1 or 2)");
                    return (Kind.Hello, null);
                case "heartbeat": return (Kind.Heartbeat, null);
                case "scene": return (Kind.Scene, ParseScene(root));
                default: return (Kind.Unknown, null);
            }
        }
    }

    static Scene ParseScene(JsonElement root)
    {
        foreach (var prop in root.EnumerateObject())
            if (!SceneFields.Contains(prop.Name)) throw new SceneException($"unknown field '{prop.Name}'");

        var seq = Need(root, "seq", JsonValueKind.Number).GetInt64();
        var b = Color(Need(root, "base", JsonValueKind.String), "base");

        var fk = Need(root, "fkeys", JsonValueKind.Array);
        if (fk.GetArrayLength() != 12) throw new SceneException($"fkeys must have 12 entries, not {fk.GetArrayLength()}");
        var fkeys = new Zone?[12];
        int i = 0;
        foreach (var e in fk.EnumerateArray())
        {
            fkeys[i] = e.ValueKind == JsonValueKind.Null ? null : ParseZone(e, $"fkeys[{i}]", allowOff: false);
            i++;
        }

        var dial = ParseZone(Need(root, "leftDial", JsonValueKind.Object), "leftDial", allowOff: true);

        var typing = Need(root, "typing", JsonValueKind.Object);
        var glow = Color(Need(typing, "glow", JsonValueKind.String), "typing.glow");
        var fade = Need(typing, "fadeMs", JsonValueKind.Number);
        if (!fade.TryGetInt32(out var fadeMs) || fadeMs is < 0 or > 3000) throw new SceneException("typing.fadeMs must be 0..3000");

        var scene = new Scene(seq, b, fkeys, dial, glow, fadeMs);

        if (root.TryGetProperty("rightDial", out var rd))
            scene = scene with { RightDial = ParseZone(rd, "rightDial", allowOff: true) };
        if (root.TryGetProperty("copilot", out var cp) && cp.ValueKind != JsonValueKind.Null)
            scene = scene with { Copilot = ParseZone(cp, "copilot", allowOff: true) };
        if (root.TryGetProperty("mouse", out var mo) && mo.ValueKind != JsonValueKind.Null)
            scene = scene with { Mouse = ParseZone(mo, "mouse", allowOff: true) };

        if (root.TryGetProperty("strip", out var st) && st.ValueKind != JsonValueKind.Null)
        {
            if (st.ValueKind != JsonValueKind.Array) throw new SceneException("strip must be an array or null");
            if (st.GetArrayLength() != 16) throw new SceneException($"strip must have 16 entries, not {st.GetArrayLength()}");
            var strip = new Rgb?[16];
            int j = 0;
            foreach (var e in st.EnumerateArray())
            {
                strip[j] = e.ValueKind switch
                {
                    JsonValueKind.Null => null,
                    JsonValueKind.String => Color(e, $"strip[{j}]"),
                    _ => throw new SceneException($"strip[{j}] must be #rrggbb or null"),
                };
                j++;
            }
            scene = scene with { Strip = strip };
        }

        if (root.TryGetProperty("keysLevel", out var kl))
        {
            if (kl.ValueKind != JsonValueKind.Number || !kl.TryGetDouble(out var level) || level is < 0.1 or > 1.0)
                throw new SceneException("keysLevel must be a number 0.1..1.0");
            scene = scene with { KeysLevel = level };
        }
        return scene;
    }

    static Zone ParseZone(JsonElement e, string where, bool allowOff)
    {
        if (e.ValueKind != JsonValueKind.Object) throw new SceneException($"{where} must be an object or null");
        var effect = Need(e, "effect", JsonValueKind.String).GetString() switch
        {
            "steady" => Effect.Steady,
            "pulse" => Effect.Pulse,
            "breathe" => Effect.Breathe,
            "blink" => Effect.Blink,
            "off" when allowOff => Effect.Off,
            var x => throw new SceneException($"{where}.effect '{x}' not allowed"),
        };
        if (effect == Effect.Off) return new Zone(Rgb.Black, Effect.Off);
        return new Zone(Color(Need(e, "color", JsonValueKind.String), $"{where}.color"), effect);
    }

    static JsonElement Need(JsonElement o, string name, JsonValueKind kind)
    {
        if (!o.TryGetProperty(name, out var v)) throw new SceneException($"missing '{name}'");
        if (v.ValueKind != kind) throw new SceneException($"'{name}' must be {kind}");
        return v;
    }

    static Rgb Color(JsonElement e, string where)
    {
        var s = e.GetString()!;
        if (s.Length != 7 || s[0] != '#' || !s[1..].All(Uri.IsHexDigit)) throw new SceneException($"{where} '{s}' is not #rrggbb");
        return Rgb.Hex(s);
    }

    public const int Version = 2;
    public const int IdleThresholdSeconds = 600;

    // idleSeconds is sampled when the line is built and may be up to 30 s stale (the periodic status cadence).
    public static string Status(string keys, string dials, string mouse, long? seq, string? error, int idleSeconds, bool locked) =>
        JsonSerializer.Serialize(new { type = "status", protocol = Version, keys, dials, mouse, seq, error, idleSeconds, locked });

    // Two statuses with the same key are "no change": idleSeconds itself is left out (it moves every second), but
    // crossing the 600 s threshold either way changes the key, so that crossing sends a status at once.
    public static string StatusKey(string keys, string dials, string mouse, long? seq, string? error, int idleSeconds, bool locked) =>
        $"{keys}|{dials}|{mouse}|{seq}|{error}|{locked}|{idleSeconds >= IdleThresholdSeconds}";
}
