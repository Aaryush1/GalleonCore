// High-level link to the Galleon keyboard part: the only way the helper should drive the keys.
// Safety rules (lab/G2-RESULTS.md, crash test): the keyboard never releases itself, so this class
//  - releases on connect if a previous run left it in host mode,
//  - releases on Dispose, on Windows sleep / shutdown / sign-out, and when the freeze policy trips,
//  - caps output at 20 fps and drops (not queues) frames that arrive faster.
// The separate Guard process covers the case where this process dies without running any of that.
using Microsoft.Win32;

namespace GalleonLink;

public sealed class Keyboard : IDisposable
{
    public const ushort Vid = 0x1B1C, Pid = 0x2B0C;

    readonly Bragi _bragi;
    readonly Log _log;
    readonly RateLimiter _rate = RateLimiter.Cap20Fps();
    readonly FreezePolicy _freeze = new();
    readonly SemaphoreSlim _io = new(1, 1);
    bool _host, _disposed;

    Keyboard(Bragi bragi, Log log) { _bragi = bragi; _log = log; }

    public bool Frozen => _freeze.Tripped;

    public static async Task<Keyboard> Connect(Log log)
    {
        var cmd = Hid.Enumerate(Vid, Pid).Where(c => c.UsagePage == 0xFF42 && c.OutputLen > 0).OrderBy(c => c.Usage == 1 ? 0 : 1).FirstOrDefault()
            ?? throw new IOException("Galleon keyboard command interface (MI_01, page 0xFF42) not found");
        var kb = new Keyboard(new Bragi(cmd, log, live: true), log);
        await kb._bragi.StartSession();
        var (status, mode) = await kb._bragi.Get(Bragi.PropMode);
        if (Recovery.NeedsReleaseOnStart(status, mode))
        {
            log.Note($"connect: keyboard left in mode {mode} (status {status}) by an earlier run: releasing first");
            await kb._bragi.Release();
        }
        SystemEvents.PowerModeChanged += kb.OnPower;
        SystemEvents.SessionEnding += kb.OnSessionEnding;
        return kb;
    }

    // Show one frame. Returns false if the frame was dropped (rate cap, frozen, disposed) or failed.
    public async Task<bool> Show(byte[] frame)
    {
        if (_disposed || _freeze.Tripped) return false;
        if (_rate.WaitBefore(DateTime.UtcNow) > TimeSpan.Zero) return false;   // drop, never queue
        if (!await _io.WaitAsync(0)) return false;                             // a frame is already in flight
        try
        {
            if (!_host)
            {
                var ok = await _bragi.SetMode((uint)KeyboardMode.HostControlled) == 0 && await _bragi.OpenLive() == 0;
                _freeze.Record(ok);
                if (!ok) { await TripIfFrozen(); return false; }
                _host = true;
            }
            var sent = await _bragi.WriteFrame(frame) == 0;
            _rate.Sent(DateTime.UtcNow);
            _freeze.Record(sent);
            await TripIfFrozen();
            return sent;
        }
        catch (Exception e) when (e is TimeoutException or IOException)
        {
            _log.Note($"show: {e.Message}");
            _freeze.Record(false);
            await TripIfFrozen();
            return false;
        }
        finally { _io.Release(); }
    }

    // Hand the keys back to the onboard profile. Safe to call any number of times.
    public async Task<bool> Release()
    {
        await _io.WaitAsync();
        try { _host = false; return await _bragi.Release(); }
        finally { _io.Release(); }
    }

    async Task TripIfFrozen()
    {
        if (!_freeze.Tripped) return;
        _log.Note("freeze policy tripped: releasing and refusing further frames");
        try { _host = false; await _bragi.Release(); } catch (Exception e) { _log.Note($"release after freeze failed: {e.Message}"); }
    }

    void OnPower(object? s, PowerModeChangedEventArgs e)
    {
        if (e.Mode == PowerModes.Suspend) { _log.Note("power: suspend, releasing"); Release().GetAwaiter().GetResult(); }
    }

    void OnSessionEnding(object? s, SessionEndingEventArgs e)
    {
        _log.Note($"session ending ({e.Reason}): releasing");
        Release().GetAwaiter().GetResult();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        SystemEvents.PowerModeChanged -= OnPower;
        SystemEvents.SessionEnding -= OnSessionEnding;
        try { Release().GetAwaiter().GetResult(); } catch (Exception e) { _log.Note($"dispose: release failed: {e.Message}"); }
        _bragi.Dispose();
    }
}
