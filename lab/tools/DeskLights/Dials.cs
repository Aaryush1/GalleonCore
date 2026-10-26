// The Galleon's two dial rings through Windows Dynamic Lighting (LampArray, PID 11032; left = lamp 1, right = lamp 0: desk-verified 2026-10-26, Phase 0 had them the other way).
// Phase 0: they obey Windows, one lamp each, and Windows hands control over ~5 s after start, so repaint on
// AvailabilityChanged. Needs the packaged app to be the background light controller. Never touches the keyboard's
// own LampArray (PID 11020): the keys are driven through GalleonLink.
using Windows.Devices.Enumeration;
using Windows.Devices.Lights;
using GalleonLink;
using WinColor = Windows.UI.Color;

namespace DeskLights;

public sealed class Dials(Log log) : IDisposable
{
    const int DialsPid = 11032;
    LampArray? _lamps;
    DeviceWatcher? _watcher;
    Rgb _left = Rgb.Black, _right = Rgb.Black;
    readonly object _gate = new();

    public string State
    {
        get { lock (_gate) return _lamps is null ? "absent" : _lamps.IsAvailable ? "ours" : "not-ours"; }
    }

    public event Action? Changed;

    public void Start()
    {
        _watcher = DeviceInformation.CreateWatcher(LampArray.GetDeviceSelector());
        _watcher.Added += async (_, d) => await TryOpen(d.Id);
        _watcher.Removed += (_, d) =>
        {
            lock (_gate) { if (_lamps?.DeviceId != d.Id) return; _lamps = null; }
            log.Note("dials: removed");
            Changed?.Invoke();
        };
        _watcher.Start();
    }

    async Task TryOpen(string id)
    {
        try
        {
            var la = await LampArray.FromIdAsync(id);
            if (la is null || la.HardwareProductId != DialsPid) return;
            la.AvailabilityChanged += (s, _) =>
            {
                log.Note($"dials: {(s.IsAvailable ? "control granted" : "control taken by another app")}");
                if (s.IsAvailable) Paint();
                Changed?.Invoke();
            };
            lock (_gate) _lamps = la;
            log.Note($"dials: found ({la.LampCount} lamps, {(la.IsAvailable ? "ours" : "not ours yet")})");
            Paint();
            Changed?.Invoke();
        }
        catch (Exception e) { log.Note($"dials: open failed: {e.Message}"); }
    }

    public void Set(Rgb left, Rgb right)
    {
        lock (_gate)
        {
            if (left == _left && right == _right) return;
            _left = left; _right = right;
        }
        Paint();
    }

    void Paint()
    {
        lock (_gate)
        {
            if (_lamps is not { IsAvailable: true } la) return;
            try { la.SetColorsForIndices([C(_left), C(_right)], [1, 0]); }   // desk-verified 2026-10-26: lamp 1 is the LEFT dial
            catch (Exception e) { log.Note($"dials: paint failed: {e.Message}"); }
        }
    }

    static WinColor C(Rgb c) => WinColor.FromArgb(255, c.R, c.G, c.B);

    public void Dispose()
    {
        try { _watcher?.Stop(); } catch (InvalidOperationException) { }
        lock (_gate) _lamps = null;   // Windows hands the dials back to their own effect when the process exits
    }
}
