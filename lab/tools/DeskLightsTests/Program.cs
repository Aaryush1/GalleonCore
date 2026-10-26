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
Expect("hello protocol 1", Protocol.Parse("{\"type\":\"hello\",\"client\":\"streamdeck-info\",\"protocol\":1}").kind == Protocol.Kind.Hello);
Expect("heartbeat", Protocol.Parse("{\"type\":\"heartbeat\"}").kind == Protocol.Kind.Heartbeat);
Expect("unknown type is ignored, not an error", Protocol.Parse("{\"type\":\"future-thing\"}").kind == Protocol.Kind.Unknown);
Expect("dial off needs no color", Protocol.Parse(SceneJson(dial: "{\"effect\":\"off\"}")).scene!.LeftDial.Effect == Effect.Off);
Expect("F-key pulse", Protocol.Parse(SceneJson(fkeys: $"[{{\"color\":\"#ef4444\",\"effect\":\"pulse\"}},{Nulls}]")).scene!.FKeys[0]!.Effect == Effect.Pulse);
Expect("fadeMs 0 allowed (glow off)", Protocol.Parse(SceneJson(typing: "{\"glow\":\"#ffffff\",\"fadeMs\":0}")).scene!.FadeMs == 0);

// Parsing: everything that must be rejected whole.
Expect("rejects: not JSON", Rejects("{scene"));
Expect("rejects: no type", Rejects("{\"seq\":1}"));
Expect("rejects: hello protocol 2", Rejects("{\"type\":\"hello\",\"protocol\":2}"));
Expect("rejects: 11 fkeys", Rejects(SceneJson(fkeys: $"[{Nulls}]")));
Expect("rejects: 13 fkeys", Rejects(SceneJson(fkeys: $"[null,null,{Nulls}]")));
Expect("rejects: bad base color", Rejects(SceneJson(@base: "\"#12345\"")));
Expect("rejects: base color without #", Rejects(SceneJson(@base: "\"1234567\"")));
Expect("rejects: non-hex color", Rejects(SceneJson(@base: "\"#gggggg\"")));
Expect("rejects: F-key effect off", Rejects(SceneJson(fkeys: $"[{{\"color\":\"#2563eb\",\"effect\":\"off\"}},{Nulls}]")));
Expect("rejects: unknown effect", Rejects(SceneJson(dial: "{\"color\":\"#f59e0b\",\"effect\":\"strobe\"}")));
Expect("rejects: dial steady without color", Rejects(SceneJson(dial: "{\"effect\":\"steady\"}")));
Expect("rejects: unknown zone field", Rejects(SceneJson(extra: ",\"rightDial\":{\"effect\":\"off\"}")));
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
Expect("dial: off is dark", Renderer.Dial(new Zone(Rgb.Black, Effect.Off), t0) == Rgb.Black);
Expect("dial: steady is its color", Renderer.Dial(new Zone(Rgb.Hex("#f59e0b"), Effect.Steady), t0) == Rgb.Hex("#f59e0b"));

var neutral = Scene.Neutral(Rgb.Hex("#ffffff"), 500);
Expect("neutral: teal base, no F-keys, dial off, glow kept", neutral.Base == Rgb.Hex("#115e59") && neutral.FKeys.All(z => z is null)
    && neutral.LeftDial.Effect == Effect.Off && neutral.FadeMs == 500);

Console.WriteLine(fail == 0 ? "DESKLIGHTS SELFTEST PASSED" : $"DESKLIGHTS SELFTEST FAILED: {fail}");
return fail == 0 ? 0 : 1;
