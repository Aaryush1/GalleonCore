// What the plugin asks the lights to show (lab/DESK-LIGHTS-PROTOCOL.md). Parsing is pure and all-or-nothing:
// a scene with any problem is rejected whole, so the keys never show half of one.
using System.Text.Json;
using GalleonLink;

namespace DeskLights;

public enum Effect { Steady, Pulse, Off }

public sealed record Zone(Rgb Color, Effect Effect);

public sealed record Scene(long Seq, Rgb Base, Zone?[] FKeys, Zone LeftDial, Rgb Glow, int FadeMs)
{
    // Before the first scene, and whenever the plugin has been quiet for 30 s: nothing that could be stale.
    public static Scene Neutral(Rgb glow, int fadeMs) =>
        new(-1, Rgb.Hex("#0f766e"), new Zone?[12], new Zone(Rgb.Black, Effect.Off), glow, fadeMs);

    public static readonly Scene Initial = Neutral(Rgb.Hex("#ffffff"), 350);
}

public sealed class SceneException(string why) : Exception(why);

public static class Protocol
{
    public const int MaxLine = 64 * 1024;
    static readonly HashSet<string> SceneFields = ["type", "seq", "base", "fkeys", "leftDial", "typing"];

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
                    if (!root.TryGetProperty("protocol", out var p) || p.ValueKind != JsonValueKind.Number || p.GetInt32() != 1)
                        throw new SceneException("unsupported protocol (want 1)");
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

        return new Scene(seq, b, fkeys, dial, glow, fadeMs);
    }

    static Zone ParseZone(JsonElement e, string where, bool allowOff)
    {
        if (e.ValueKind != JsonValueKind.Object) throw new SceneException($"{where} must be an object or null");
        var effect = Need(e, "effect", JsonValueKind.String).GetString() switch
        {
            "steady" => Effect.Steady,
            "pulse" => Effect.Pulse,
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

    public static string Status(string keys, string dials, long? seq, string? error) =>
        JsonSerializer.Serialize(new { type = "status", keys, dials, seq, error });
}
