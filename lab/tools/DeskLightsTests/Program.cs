// Offline checks for the desk lights helper: scene parsing (all or nothing) and rendering. No device needed.
using DeskLights;
using GalleonLink;

int fail = 0;
void Expect(string name, bool ok) { Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {name}"); if (!ok) fail++; }
bool Rejects(string line) { try { Protocol.Parse(line); return false; } catch (SceneException) { return true; } }

const string Nulls = "null,null,null,null,null,null,null,null,null,null,null";
string SceneJson(string fkeys = $"[{{\"color\":\"#2563eb\",\"effect\":\"steady\"}},{Nulls}]", string dial = "{\"color\":\"#f59e0b\",\"effect\":\"steady\"}",
    string typing = "{\"glow\":\"#ffffff\",\"fadeMs\":350}", string extra = "", string @base = "\"#172554\"") =>
    $"{{\"type\":\"scene\",\"seq\":12,\"base\":{@base},\"fkeys\":{fkeys},\"leftDial\":{dial},\"typing\":{typing}{extra}}}";

// Parsing: the happy path.
var (kind, scene) = Protocol.Parse(SceneJson());
Expect("scene parses", kind == Protocol.Kind.Scene && scene is not null);
Expect("scene: seq, base, F1, dial, glow", scene!.Seq == 12 && scene.Base == Rgb.Hex("#172554") && scene.FKeys[0] == new Zone(Rgb.Hex("#2563eb"), Effect.Steady)
    && scene.FKeys[1] is null && scene.LeftDial.Effect == Effect.Steady && scene.Glow == Rgb.Hex("#ffffff") && scene.FadeMs == 350);
Expect("hello protocol 2", Protocol.Parse("{\"type\":\"hello\",\"client\":\"streamdeck-info\",\"protocol\":2}").kind == Protocol.Kind.Hello);
Expect("hello protocol 1", Protocol.Parse("{\"type\":\"hello\",\"client\":\"streamdeck-info\",\"protocol\":1}").kind == Protocol.Kind.Hello);
Expect("heartbeat", Protocol.Parse("{\"type\":\"heartbeat\"}").kind == Protocol.Kind.Heartbeat);
Expect("unknown type is ignored, not an error", Protocol.Parse("{\"type\":\"future-thing\"}").kind == Protocol.Kind.Unknown);
Expect("dial off needs no color", Protocol.Parse(SceneJson(dial: "{\"effect\":\"off\"}")).scene!.LeftDial.Effect == Effect.Off);
Expect("F-key pulse", Protocol.Parse(SceneJson(fkeys: $"[{{\"color\":\"#ef4444\",\"effect\":\"pulse\"}},{Nulls}]")).scene!.FKeys[0]!.Effect == Effect.Pulse);
Expect("fadeMs 0 allowed (glow off)", Protocol.Parse(SceneJson(typing: "{\"glow\":\"#ffffff\",\"fadeMs\":0}")).scene!.FadeMs == 0);

// Parsing: everything that must be rejected whole.
Expect("rejects: not JSON", Rejects("{scene"));
Expect("rejects: no type", Rejects("{\"seq\":1}"));
Expect("rejects: hello protocol 3", Rejects("{\"type\":\"hello\",\"protocol\":3}"));
Expect("rejects: hello protocol 0 / missing / fractional", Rejects("{\"type\":\"hello\",\"protocol\":0}") && Rejects("{\"type\":\"hello\"}") && Rejects("{\"type\":\"hello\",\"protocol\":1.5}"));
Expect("rejects: 11 fkeys", Rejects(SceneJson(fkeys: $"[{Nulls}]")));
Expect("rejects: 13 fkeys", Rejects(SceneJson(fkeys: $"[null,null,{Nulls}]")));
Expect("rejects: bad base color", Rejects(SceneJson(@base: "\"#12345\"")));
Expect("rejects: base color without #", Rejects(SceneJson(@base: "\"1234567\"")));
Expect("rejects: non-hex color", Rejects(SceneJson(@base: "\"#gggggg\"")));
Expect("rejects: F-key effect off", Rejects(SceneJson(fkeys: $"[{{\"color\":\"#2563eb\",\"effect\":\"off\"}},{Nulls}]")));
Expect("rejects: unknown effect", Rejects(SceneJson(dial: "{\"color\":\"#f59e0b\",\"effect\":\"strobe\"}")));
Expect("rejects: dial steady without color", Rejects(SceneJson(dial: "{\"effect\":\"steady\"}")));
Expect("rejects: unknown zone field", Rejects(SceneJson(extra: ",\"bogus\":1")));
Expect("rejects: fadeMs over 3000", Rejects(SceneJson(typing: "{\"glow\":\"#ffffff\",\"fadeMs\":5000}")));
Expect("rejects: negative fadeMs", Rejects(SceneJson(typing: "{\"glow\":\"#ffffff\",\"fadeMs\":-1}")));
Expect("rejects: missing typing", Rejects("{\"type\":\"scene\",\"seq\":1,\"base\":\"#000000\",\"fkeys\":[null," + Nulls + "],\"leftDial\":{\"effect\":\"off\"}}"));
Expect("rejects: fkeys entry not an object", Rejects(SceneJson(fkeys: $"[\"#2563eb\",{Nulls}]")));
Expect("rejects: over 64 KiB", Rejects(new string(' ', Protocol.MaxLine + 1)));

// Rendering.
var t0 = TimeSpan.FromSeconds(100);
var noPresses = new Dictionary<byte, TimeSpan>();
var frame = Renderer.KeyFrame(scene, noPresses, t0);
Rgb At(byte[] f, int led) => new(f[2 + 3 * led], f[3 + 3 * led], f[4 + 3 * led]);
Expect("render: F1 (0x3A) gets its color", At(frame, 0x3A) == Rgb.Hex("#2563eb"));
Expect("render: F2 (null) gets the base", At(frame, 0x3B) == Rgb.Hex("#172554"));
Expect("render: a letter gets the base", At(frame, 0x04) == Rgb.Hex("#172554"));
Expect("render: F12 is LED 0x45", Renderer.F1 + 11 == 0x45);
Expect("render: steady scene isn't animating", !Renderer.KeysAnimating(scene, noPresses, t0));

var press = new Dictionary<byte, TimeSpan> { [0x04] = t0 };
Expect("glow: pressed key is the glow color at once", At(Renderer.KeyFrame(scene, press, t0), 0x04) == Rgb.Hex("#ffffff"));
var half = At(Renderer.KeyFrame(scene, press, t0 + TimeSpan.FromMilliseconds(175)), 0x04);
Expect("glow: halfway through the fade it's halfway back", half.R is > 130 and < 145);
Expect("glow: after the fade it's the base again", At(Renderer.KeyFrame(scene, press, t0 + TimeSpan.FromMilliseconds(350)), 0x04) == Rgb.Hex("#172554"));
Expect("glow: animating while fading, not after", Renderer.KeysAnimating(scene, press, t0 + TimeSpan.FromMilliseconds(100)) && !Renderer.KeysAnimating(scene, press, t0 + TimeSpan.FromMilliseconds(400)));
var onF1 = new Dictionary<byte, TimeSpan> { [0x3A] = t0 };
Expect("glow: fades back to the F-key color, not the base", At(Renderer.KeyFrame(scene, onF1, t0 + TimeSpan.FromMilliseconds(349)), 0x3A).B > 0xE0);
var off = scene with { FadeMs = 0 };
Expect("glow: fadeMs 0 draws no glow", At(Renderer.KeyFrame(off, press, t0), 0x04) == Rgb.Hex("#172554"));
Expect("glow: a press on a non-LED index is ignored", Renderer.KeyFrame(scene, new Dictionary<byte, TimeSpan> { [200] = t0 }, t0).Length == 440);

var pulsing = scene with { FKeys = [new Zone(Rgb.Hex("#ef4444"), Effect.Pulse), .. new Zone?[11]] };
Expect("pulse: full brightness at the start of a period", At(Renderer.KeyFrame(pulsing, noPresses, TimeSpan.FromSeconds(10)), 0x3A) == Rgb.Hex("#ef4444"));
Expect("pulse: 35% halfway", At(Renderer.KeyFrame(pulsing, noPresses, TimeSpan.FromSeconds(10.5)), 0x3A) == Renderer.Scale(Rgb.Hex("#ef4444"), 0.35));
Expect("pulse: animating", Renderer.KeysAnimating(pulsing, noPresses, t0));
Expect("dial: off is dark", Renderer.Dial(new Zone(Rgb.Black, Effect.Off), scene, t0) == Rgb.Black);
Expect("dial: steady is its color", Renderer.Dial(new Zone(Rgb.Hex("#f59e0b"), Effect.Steady), scene, t0) == Rgb.Hex("#f59e0b"));


// ---- Protocol v2 ----
Expect("v1 scene: v2 fields take their defaults", scene.RightDial.Effect == Effect.Off && scene.Copilot is null && scene.Strip is null
    && scene.KeysLevel == 1.0 && scene.Mouse is null);
string Nulls16(int n) => string.Join(",", Enumerable.Repeat("null", n));
var strip16 = $"[\"#ff0000\",{Nulls16(14)},\"#00ff00\"]";
var v2 = Protocol.Parse(SceneJson(extra: $",\"rightDial\":{{\"color\":\"#f59e0b\",\"effect\":\"breathe\"}},\"copilot\":{{\"color\":\"#ef4444\",\"effect\":\"pulse\"}}," +
    $"\"strip\":{strip16},\"keysLevel\":0.4,\"mouse\":{{\"color\":\"#ff0000\",\"effect\":\"blink\"}}")).scene!;
Expect("v2: rightDial breathe", v2.RightDial.Effect == Effect.Breathe && v2.RightDial.Color == Rgb.Hex("#f59e0b"));
Expect("v2: copilot pulse", v2.Copilot == new Zone(Rgb.Hex("#ef4444"), Effect.Pulse));
Expect("v2: strip parsed (first, null middle, last)", v2.Strip!.Length == 16 && v2.Strip[0] == Rgb.Hex("#ff0000") && v2.Strip[1] is null && v2.Strip[15] == Rgb.Hex("#00ff00"));
Expect("v2: keysLevel", v2.KeysLevel == 0.4);
Expect("v2: mouse blink", v2.Mouse == new Zone(Rgb.Hex("#ff0000"), Effect.Blink));
Expect("v2: null copilot, strip and mouse mean absent", Protocol.Parse(SceneJson(extra: ",\"copilot\":null,\"strip\":null,\"mouse\":null")).scene is { Copilot: null, Strip: null, Mouse: null });
Expect("v2: mouse off accepted", Protocol.Parse(SceneJson(extra: ",\"mouse\":{\"effect\":\"off\"}")).scene!.Mouse!.Effect == Effect.Off);
Expect("v2: blink on a dial is accepted (dials go dark after)", Protocol.Parse(SceneJson(dial: "{\"color\":\"#22c55e\",\"effect\":\"blink\"}")).scene!.LeftDial.Effect == Effect.Blink);
Expect("v2: F-key breathe and blink accepted", Protocol.Parse(SceneJson(fkeys: $"[{{\"color\":\"#2563eb\",\"effect\":\"breathe\"}},{{\"color\":\"#2563eb\",\"effect\":\"blink\"}},{Nulls16(10)}]")).scene!.FKeys[1]!.Effect == Effect.Blink);
Expect("v2: keysLevel bounds 0.1 and 1.0 accepted", Protocol.Parse(SceneJson(extra: ",\"keysLevel\":0.1")).scene!.KeysLevel == 0.1 && Protocol.Parse(SceneJson(extra: ",\"keysLevel\":1")).scene!.KeysLevel == 1.0);
Expect("rejects: strip with 15 entries", Rejects(SceneJson(extra: $",\"strip\":[{Nulls16(15)}]")));
Expect("rejects: strip with 17 entries", Rejects(SceneJson(extra: $",\"strip\":[{Nulls16(17)}]")));
Expect("rejects: strip not an array", Rejects(SceneJson(extra: ",\"strip\":\"#ffffff\"")));
Expect("rejects: strip entry bad color", Rejects(SceneJson(extra: $",\"strip\":[\"#12345\",{Nulls16(15)}]")));
Expect("rejects: strip entry is an object", Rejects(SceneJson(extra: $",\"strip\":[{{\"color\":\"#ffffff\",\"effect\":\"steady\"}},{Nulls16(15)}]")));
Expect("rejects: keysLevel 0.09", Rejects(SceneJson(extra: ",\"keysLevel\":0.09")));
Expect("rejects: keysLevel 1.01", Rejects(SceneJson(extra: ",\"keysLevel\":1.01")));
Expect("rejects: keysLevel 0", Rejects(SceneJson(extra: ",\"keysLevel\":0")));
Expect("rejects: keysLevel as a string", Rejects(SceneJson(extra: ",\"keysLevel\":\"0.5\"")));
Expect("rejects: keysLevel null", Rejects(SceneJson(extra: ",\"keysLevel\":null")));
Expect("rejects: rightDial null", Rejects(SceneJson(extra: ",\"rightDial\":null")));
Expect("rejects: rightDial unknown effect", Rejects(SceneJson(extra: ",\"rightDial\":{\"color\":\"#fff000\",\"effect\":\"strobe\"}")));
Expect("rejects: rightDial steady without color", Rejects(SceneJson(extra: ",\"rightDial\":{\"effect\":\"steady\"}")));
Expect("rejects: copilot a string", Rejects(SceneJson(extra: ",\"copilot\":\"#ffffff\"")));
Expect("rejects: mouse blink without color", Rejects(SceneJson(extra: ",\"mouse\":{\"effect\":\"blink\"}")));
Expect("rejects: F-key off still rejected", Rejects(SceneJson(fkeys: $"[{{\"effect\":\"off\"}},{Nulls}]")));
Expect("rejects: one bad v2 field sinks the whole scene", Rejects(SceneJson(extra: ",\"rightDial\":{\"effect\":\"off\"},\"keysLevel\":5")));

// v2 rendering. 4 s breathe cycle: 100% at 0, 20% at 2 s, 60% at 1 s.
Expect("breathe: full at the start of a cycle", Renderer.BreatheLevel(TimeSpan.FromSeconds(8)) > 0.9999);
Expect("breathe: 20% halfway", Math.Abs(Renderer.BreatheLevel(TimeSpan.FromSeconds(10)) - 0.2) < 1e-9);
Expect("breathe: 60% a quarter in", Math.Abs(Renderer.BreatheLevel(TimeSpan.FromSeconds(9)) - 0.6) < 1e-9);
var amber = Rgb.Hex("#f59e0b");
var breathing = scene with { RightDial = new Zone(amber, Effect.Breathe) };
Expect("breathe: dial colour at 20% halfway", Renderer.Dial(breathing.RightDial, breathing, TimeSpan.FromSeconds(10)) == Renderer.Scale(amber, 0.2));
Expect("breathe: animating", Renderer.DevicesAnimating(breathing, t0) && !Renderer.DevicesAnimating(scene, t0));
var bk = scene with { FKeys = [new Zone(Rgb.Hex("#ef4444"), Effect.Breathe), .. new Zone?[11]] };
Expect("breathe: F-key at 20% halfway", At(Renderer.KeyFrame(bk, noPresses, TimeSpan.FromSeconds(10)), 0x3A) == Renderer.Scale(Rgb.Hex("#ef4444"), 0.2) && Renderer.KeysAnimating(bk, noPresses, t0));

// blink: the scene arrived at 50 s.
var green = Rgb.Hex("#22c55e");
var arrive = TimeSpan.FromSeconds(50);
var blinky = Scene.Adopt(null, scene with { Seq = 30, FKeys = [new Zone(green, Effect.Blink), .. new Zone?[11]], Copilot = new Zone(green, Effect.Blink),
    LeftDial = new Zone(green, Effect.Blink), Mouse = new Zone(green, Effect.Blink) }, arrive);
Expect("blink: Adopt stamps the arrival time", blinky.ReceivedAt == arrive);
Expect("blink: F-key on at arrival", At(Renderer.KeyFrame(blinky, noPresses, arrive), 0x3A) == green);
Expect("blink: F-key still on at 599 ms", At(Renderer.KeyFrame(blinky, noPresses, arrive + TimeSpan.FromMilliseconds(599)), 0x3A) == green);
Expect("blink: F-key back to base at 600 ms", At(Renderer.KeyFrame(blinky, noPresses, arrive + TimeSpan.FromMilliseconds(600)), 0x3A) == Rgb.Hex("#172554"));
Expect("blink: copilot on, then base", At(Renderer.KeyFrame(blinky, noPresses, arrive), 0x81) == green && At(Renderer.KeyFrame(blinky, noPresses, arrive + TimeSpan.FromSeconds(1)), 0x81) == Rgb.Hex("#172554"));
Expect("blink: dial and mouse on, then dark", Renderer.Dial(blinky.LeftDial, blinky, arrive) == green && Renderer.Dial(blinky.Mouse!, blinky, arrive) == green
    && Renderer.Dial(blinky.LeftDial, blinky, arrive + TimeSpan.FromSeconds(1)) == Rgb.Black && Renderer.Dial(blinky.Mouse!, blinky, arrive + TimeSpan.FromSeconds(1)) == Rgb.Black);
Expect("blink: animating during and just after, then not", Renderer.KeysAnimating(blinky, noPresses, arrive + TimeSpan.FromMilliseconds(300))
    && Renderer.DevicesAnimating(blinky, arrive + TimeSpan.FromMilliseconds(650)) && !Renderer.KeysAnimating(blinky, noPresses, arrive + TimeSpan.FromSeconds(2)) && !Renderer.DevicesAnimating(blinky, arrive + TimeSpan.FromSeconds(2)));
Expect("blink: not on before the scene arrived", Renderer.Dial(blinky.LeftDial, blinky, arrive - TimeSpan.FromSeconds(1)) == Rgb.Black);
Expect("blink: same seq re-sent keeps the first arrival (no second blink)", Scene.Adopt(blinky, blinky with { ReceivedAt = TimeSpan.Zero }, arrive + TimeSpan.FromSeconds(5)).ReceivedAt == arrive);
Expect("blink: a new seq restarts the clock", Scene.Adopt(blinky, blinky with { Seq = 31 }, arrive + TimeSpan.FromSeconds(5)).ReceivedAt == arrive + TimeSpan.FromSeconds(5));

// copilot, strip, keysLevel.
var cop = scene with { Copilot = new Zone(Rgb.Hex("#ef4444"), Effect.Steady) };
Expect("copilot: LED 0x81 gets its colour, 0x80 and 0x82 stay base", At(Renderer.KeyFrame(cop, noPresses, t0), 0x81) == Rgb.Hex("#ef4444")
    && At(Renderer.KeyFrame(cop, noPresses, t0), 0x80) == Rgb.Hex("#172554") && At(Renderer.KeyFrame(cop, noPresses, t0), 0x82) == Rgb.Hex("#172554"));
Expect("copilot: off is dark", At(Renderer.KeyFrame(scene with { Copilot = Zone.Dark }, noPresses, t0), 0x81) == Rgb.Black);
var stripScene = scene with { Strip = [Rgb.Hex("#ff0000"), null, .. new Rgb?[13], Rgb.Hex("#0000ff")] };
var sf = Renderer.KeyFrame(stripScene, noPresses, t0);
Expect("strip: first is LED 0x82, last is 0x91, null entries stay base, neighbours untouched",
    At(sf, 0x82) == Rgb.Hex("#ff0000") && At(sf, 0x83) == Rgb.Hex("#172554") && At(sf, 0x91) == Rgb.Hex("#0000ff") && At(sf, 0x90) == Rgb.Hex("#172554"));
Expect("strip: frame is still 146 LEDs", sf.Length == 2 + 3 * 146 && Frames.Leds == 146);
var dim = scene with { KeysLevel = 0.4, FKeys = [new Zone(Rgb.Hex("#2563eb"), Effect.Steady), .. new Zone?[11]], Copilot = new Zone(Rgb.Hex("#ef4444"), Effect.Steady),
    Strip = [Rgb.Hex("#ff0000"), .. new Rgb?[15]] };
var df = Renderer.KeyFrame(dim, noPresses, t0);
Expect("keysLevel: scales the base", At(df, 0x04) == Renderer.Scale(Rgb.Hex("#172554"), 0.4));
Expect("keysLevel: does not touch F-keys, copilot or strip colours", At(df, 0x3A) == Rgb.Hex("#2563eb") && At(df, 0x81) == Rgb.Hex("#ef4444") && At(df, 0x82) == Rgb.Hex("#ff0000"));
Expect("keysLevel: strip null entries show the dimmed base", At(df, 0x83) == Renderer.Scale(Rgb.Hex("#172554"), 0.4));
Expect("keysLevel: a typing glow starts white and ends at the dimmed base", At(Renderer.KeyFrame(dim, press, t0), 0x04) == Rgb.Hex("#ffffff")
    && At(Renderer.KeyFrame(dim, press, t0 + TimeSpan.FromMilliseconds(350)), 0x04) == Renderer.Scale(Rgb.Hex("#172554"), 0.4));
Expect("keysLevel 1.0 leaves the base exactly", At(Renderer.KeyFrame(scene with { KeysLevel = 1.0 }, noPresses, t0), 0x04) == Rgb.Hex("#172554"));

// status JSON shape.
using (var st = System.Text.Json.JsonDocument.Parse(Protocol.Status("live", "ours", "idle", 12, null, 42, false)))
{
    var r = st.RootElement;
    Expect("status: v2 shape", r.GetProperty("type").GetString() == "status" && r.GetProperty("protocol").GetInt32() == 2 && r.GetProperty("keys").GetString() == "live"
        && r.GetProperty("dials").GetString() == "ours" && r.GetProperty("mouse").GetString() == "idle" && r.GetProperty("seq").GetInt64() == 12
        && r.GetProperty("error").ValueKind == System.Text.Json.JsonValueKind.Null && r.GetProperty("idleSeconds").GetInt32() == 42 && r.GetProperty("locked").ValueKind == System.Text.Json.JsonValueKind.False);
    Expect("status: exactly the nine documented fields", r.EnumerateObject().Count() == 9);
}
using (var st = System.Text.Json.JsonDocument.Parse(Protocol.Status("absent", "absent", "absent", null, "boom", 700, true)))
    Expect("status: null seq, error text, locked true", st.RootElement.GetProperty("seq").ValueKind == System.Text.Json.JsonValueKind.Null
        && st.RootElement.GetProperty("error").GetString() == "boom" && st.RootElement.GetProperty("locked").GetBoolean());
string K(int idle, bool locked = false) => Protocol.StatusKey("live", "ours", "idle", 1, null, idle, locked);
Expect("status key: idle ticking under 600 s is no change", K(5) == K(500) && K(599) == K(0));
Expect("status key: crossing 600 s either way changes it", K(599) != K(600) && K(600) != K(0) && K(600) == K(9000));
Expect("status key: lock and unlock change it", K(5) != K(5, true));

var neutral = Scene.Neutral(Rgb.Hex("#ffffff"), 500);
Expect("neutral: teal base, no F-keys, dial off, glow kept", neutral.Base == Rgb.Hex("#115e59") && neutral.FKeys.All(z => z is null)
    && neutral.LeftDial.Effect == Effect.Off && neutral.FadeMs == 500);

Console.WriteLine(fail == 0 ? "DESKLIGHTS SELFTEST PASSED" : $"DESKLIGHTS SELFTEST FAILED: {fail}");
return fail == 0 ? 0 : 1;
