// The mouse's 8 lamps through Windows Dynamic Lighting (LampArray, PID 16537). See lab/DESK-LIGHTS-MOUSE-NOTES.md.
// Least invasive by design: nothing is enumerated or opened until the plugin's scene asks for the mouse, and when
// the scene stops asking (or goes stale) the LampArray is disabled and released so the device's own effect
// (G HUB Screen Sampler) can come back. Whether it does is a desk test, not something this code can know.
using Windows.Devices.Enumeration;
using Windows.Devices.Lights;
using GalleonLink;
using WinColor = Windows.UI.Color;

namespace DeskLights;

public sealed class Mouse(Log log) : IDisposable
{
    const int MousePid = 16537;
    static readonly Rgb Unpainted = new(1, 2, 3);   // never equals a wanted color by accident of being first
    LampArray? _lamps;
    DeviceWatcher? _watcher;
    Rgb? _want;                  // null = not driving
    Rgb _painted = Unpainted;
    int _generation;             // bumps on every release, so a late-finishing open can't resurrect a released mouse
    readonly object _gate = new();

    public event Action? Changed;

    // idle = not driving; absent = asked to drive but no mouse lamp array found; ours / not-ours as for the dials.
    public string State
    {
        get { lock (_gate) return _want is null ? "idle" : _lamps is null ? "absent" : _lamps.IsAvailable ? "ours" : "not-ours"; }
    }

    // null releases the mouse; a color drives all 8 lamps.
    public void Set(Rgb? color)
    {
        bool stateChanged, start = false;
        lock (_gate)
        {
            if (color == _want) return;
            stateChanged = (color is null) != (_want is null);
            _want = color;
            if (color is null) Release();
            else if (_watcher is null) start = true;
        }
        if (start) Start();
        else Paint();
        if (stateChanged) Changed?.Invoke();
    }

    void Start()
    {
        log.Note("mouse: scene wants it, looking for the lamp array");
        int gen;
        lock (_gate) gen = _generation;
        var w = DeviceInformation.CreateWatcher(LampArray.GetDeviceSelector());
        w.Added += async (_, d) => await TryOpen(d.Id, gen);
        w.Removed += (_, d) =>
        {
            lock (_gate) { if (_lamps?.DeviceId != d.Id) return; _lamps = null; }
            log.Note("mouse: removed");
            Changed?.Invoke();
        };
        lock (_gate) _watcher = w;
        w.Start();
    }

    async Task TryOpen(string id, int gen)
    {
        try
        {
            var la = await LampArray.FromIdAsync(id);
            if (la is null || la.HardwareProductId != MousePid) return;   // anything else is left exactly as found
            la.AvailabilityChanged += (s, _) =>
            {
                log.Note($"mouse: {(s.IsAvailable ? "control granted" : "control taken by another app")}");
                if (s.IsAvailable) Paint();
                Changed?.Invoke();
            };
            lock (_gate)
            {
                if (gen != _generation || _want is null) return;   // released while opening: drop it
                _lamps = la; _painted = Unpainted;
            }
            log.Note($"mouse: found ({la.LampCount} lamps, {(la.IsAvailable ? "ours" : "not ours yet")})");
            Paint();
            Changed?.Invoke();
        }
        catch (Exception e) { log.Note($"mouse: open failed: {e.Message}"); }
    }

    void Paint()
    {
        lock (_gate)
        {
            if (_want is not { } c || _lamps is not { IsAvailable: true } la || c == _painted) return;
            try { la.SetColor(WinColor.FromArgb(255, c.R, c.G, c.B)); _painted = c; }
            catch (Exception e) { log.Note($"mouse: paint failed: {e.Message}"); }
        }
    }

    // Must hold _gate. Stop sending, stop watching, drop the reference. Deliberately paints nothing (not black):
    // the aim is to let the device go back to its own effect.
    void Release()
    {
        _generation++;
        if (_lamps is { } la)
        {
            try { la.IsEnabled = false; } catch (Exception e) { log.Note($"mouse: disable failed: {e.Message}"); }
            log.Note("mouse: released");
        }
        _lamps = null;
        try { _watcher?.Stop(); } catch (InvalidOperationException) { }
        _watcher = null;
    }

    public void Dispose()
    {
        lock (_gate) { _want = null; Release(); }
    }
}
