// Desk lights helper ("the hands"): started by Windows at login, it shows the plugin's scene on the Galleon's keys
// (GalleonLink) and dials (Dynamic Lighting), and draws the typing glow itself. See lab/DESK-LIGHTS-PROTOCOL.md.
// Safety comes from GalleonLink.Keyboard (release on exit/suspend/sign-out, yield to Web Hub, reconnect, 20 fps cap)
// plus Guard, which releases the keys if this process dies without doing it.
using System.Diagnostics;
using GalleonLink;
using DeskLights;

const string PipeName = "desk-lights";
var pluginQuietAfter = TimeSpan.FromSeconds(30);

using var single = new Mutex(true, @"Local\DeskLightsHelper", out var first);
if (!first) return 0;   // already running (startup task + the plugin's restart attempt)

var logs = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DeskLights", "logs");
Directory.CreateDirectory(logs);
foreach (var old in new DirectoryInfo(logs).GetFiles("helper-*.log").OrderByDescending(f => f.Name).Skip(14)) old.Delete();
using var log = new Log(Path.Combine(logs, $"helper-{DateTime.Now:yyyyMMdd-HHmmss}.log")) { Wire = false };
log.Note($"desk lights helper starting (pid {Environment.ProcessId})");

using var stop = new CancellationTokenSource();
Microsoft.Win32.SystemEvents.SessionEnding += (_, _) => stop.Cancel();
AppDomain.CurrentDomain.ProcessExit += (_, _) => stop.Cancel();

StartGuard();

var clock = Stopwatch.StartNew();
var gate = new object();
var scene = Scene.Initial;
var applied = scene;                   // the last scene from the plugin (used again when it comes back)
var lastLine = TimeSpan.MinValue;      // when the plugin last said anything
var stale = true;
string? lastError = null;
var presses = new Dictionary<byte, TimeSpan>();
using var wake = new SemaphoreSlim(0);

using var dials = new Dials(log);
Keyboard? kb = null;
PipeServer? pipe = null;
string lastStatus = "";

string Status()
{
    var keys = kb is null ? "absent" : kb.Frozen ? "frozen" : !kb.Connected ? "unplugged" : kb.Yielded ? "yielded" : "live";
    lock (gate) return Protocol.Status(keys, dials.State, stale ? null : applied.Seq, lastError);
}

void PushStatus(bool force = false)
{
    var s = Status();
    if (!force && s == lastStatus) return;
    lastStatus = s;
    pipe?.Send(s);
}

void OnLine(string line)
{
    try
    {
        var (kind, parsed) = Protocol.Parse(line);
        lock (gate)
        {
            lastLine = clock.Elapsed;
            if (kind == Protocol.Kind.Scene && parsed is not null)
            {
                if (stale) log.Note("plugin scene received: showing it");
                applied = scene = parsed; stale = false; lastError = null;
            }
        }
        if (kind == Protocol.Kind.Unknown) log.Note("pipe: ignored a line of unknown type");
        wake.Release();
    }
    catch (SceneException e)
    {
        lock (gate) { lastLine = clock.Elapsed; lastError = e.Message; }
        log.Note($"pipe: rejected a line: {e.Message}");
    }
    PushStatus();
}

pipe = new PipeServer(PipeName, log, OnLine, Status);
var pipeTask = pipe.Run(stop.Token);

dials.Changed += () => PushStatus();
dials.Start();

using var keys = new RawKeys((usage, _) => { lock (gate) presses[usage] = clock.Elapsed; wake.Release(); });
log.Note(keys.Failed ?? "typing glow: listening for key presses (Raw Input)");

// Keyboard: connect, and keep trying while it's absent (not plugged in at login).
_ = Task.Run(async () =>
{
    while (!stop.IsCancellationRequested && kb is null)
    {
        try { kb = await Keyboard.Connect(log); log.Note("keyboard connected"); PushStatus(); wake.Release(); }
        catch (IOException e) { log.Note($"keyboard: {e.Message}; retrying in 10 s"); await Task.Delay(10_000, stop.Token).ContinueWith(_ => { }); }
    }
});

byte[]? lastFrame = null;
var lastSent = TimeSpan.MinValue;
var lastStatusTick = TimeSpan.Zero;
try
{
    while (!stop.IsCancellationRequested)
    {
        Scene s; Dictionary<byte, TimeSpan> p;
        var now = clock.Elapsed;
        lock (gate)
        {
            if (!stale && now - lastLine > pluginQuietAfter)
            {
                stale = true;
                scene = Scene.Neutral(applied.Glow, applied.FadeMs);
                log.Note("plugin quiet for 30 s: neutral scene (base and typing keep going)");
            }
            foreach (var old in presses.Where(kv => (now - kv.Value).TotalMilliseconds > 3000).Select(kv => kv.Key).ToList()) presses.Remove(old);
            s = scene; p = new Dictionary<byte, TimeSpan>(presses);
        }

        dials.Set(Renderer.Dial(s.LeftDial, now), Rgb.Black);

        if (kb is { } k)
        {
            var frame = Renderer.KeyFrame(s, p, now);
            // Send on change, and at least once a second so the keys come back by themselves after a yield or replug.
            if ((lastFrame is null || !frame.AsSpan().SequenceEqual(lastFrame) || now - lastSent > TimeSpan.FromSeconds(1)) && await k.Show(frame))
            {
                lastFrame = frame; lastSent = now;
            }
        }

        if (now - lastStatusTick > TimeSpan.FromSeconds(30)) { lastStatusTick = now; PushStatus(force: true); } else PushStatus();

        var animating = Renderer.KeysAnimating(s, p, now) || s.LeftDial.Effect == Effect.Pulse;
        await wake.WaitAsync(animating ? TimeSpan.FromMilliseconds(50) : TimeSpan.FromMilliseconds(500), stop.Token).ContinueWith(_ => { });
    }
}
finally
{
    log.Note("stopping: releasing the keys and dials");
    kb?.Dispose();
    dials.Dispose();
}
await pipeTask.ContinueWith(_ => { });
return 0;

void StartGuard()
{
    var guard = Path.Combine(AppContext.BaseDirectory, "Guard.exe");
    if (!File.Exists(guard)) { log.Note("guard: Guard.exe not found next to the helper; running without it"); return; }
    try
    {
        Process.Start(new ProcessStartInfo(guard, Environment.ProcessId.ToString()) { UseShellExecute = false, CreateNoWindow = true });
        log.Note("guard started");
    }
    catch (Exception e) { log.Note($"guard: could not start: {e.Message}"); }
}
