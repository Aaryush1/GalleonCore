// High-level link to the Galleon keyboard part: the only way the helper should drive the keys.
// Safety rules (lab/G2-RESULTS.md crash test, lab/G3-RESULTS.md desk checks): the keyboard never releases itself, so this class
//  - releases on connect if a previous run left it in host mode,
//  - releases on Dispose, on Windows sleep / shutdown / sign-out, and when the freeze policy trips,
//  - caps output at 20 fps and drops (not queues) frames that arrive faster,
//  - yields to any other host (Web Hub, iCUE): stops sending entirely, resumes after 60 s of quiet (YieldPolicy),
//  - reconnects by itself after the keyboard is unplugged and plugged back in.
// The separate Guard process covers the case where this process dies without running any of that.
using Microsoft.Win32;

namespace GalleonLink;

public sealed class Keyboard : IDisposable
{
    public const ushort Vid = 0x1B1C, Pid = 0x2B0C;
    public static readonly TimeSpan DefaultListenFirst = TimeSpan.FromSeconds(3);   // Web Hub polls about every 2.5 s
    static readonly TimeSpan ModeCheckEvery = TimeSpan.FromSeconds(1), Tick = TimeSpan.FromSeconds(1), ListenWindow = TimeSpan.FromSeconds(2);

    Bragi? _bragi;
    readonly Log _log;
    readonly RateLimiter _rate = RateLimiter.Cap20Fps();
    FreezePolicy _freeze = new();
    readonly YieldPolicy _yield;
    readonly TimeSpan _listenFirst;
    readonly SemaphoreSlim _io = new(1, 1);
    readonly CancellationTokenSource _stop = new();
    Task _supervisor = Task.CompletedTask;
    bool _host, _disposed;
    int _foreignSeen;
    DateTime _modeCheckedAt;

    Keyboard(Log log, YieldPolicy yield, TimeSpan listenFirst) { _log = log; _yield = yield; _listenFirst = listenFirst; }

    public bool Frozen => _freeze.Tripped;
    public bool Yielded => _yield.Yielded;
    public bool Connected => _bragi is not null;
    static DateTime Now => DateTime.UtcNow;

    // listenFirst: how long to listen for another host before touching the keyboard (Guard passes zero: it must be fast,
    // and it only acts when the keyboard is still in host mode).
    public static async Task<Keyboard> Connect(Log log, TimeSpan? listenFirst = null, YieldPolicy? yield = null)
    {
        var kb = new Keyboard(log, yield ?? YieldPolicy.Default(), listenFirst ?? DefaultListenFirst);
        var bragi = Open(log) ?? throw new IOException("Galleon keyboard command interface (MI_01, page 0xFF42) not found");
        try { await kb.Attach(bragi); }
        catch { bragi.Dispose(); throw; }
        SystemEvents.PowerModeChanged += kb.OnPower;
        SystemEvents.SessionEnding += kb.OnSessionEnding;
        kb._supervisor = Task.Run(kb.Supervise);
        return kb;
    }

    static HidCollection? Find() =>
        Hid.Enumerate(Vid, Pid).Where(c => c.UsagePage == 0xFF42 && c.OutputLen > 0).OrderBy(c => c.Usage == 1 ? 0 : 1).FirstOrDefault();

    static Bragi? Open(Log log) => Find() is { } c ? new Bragi(c, log, live: true) : null;

    // Take a freshly opened link into use: listen for another host first, then session + release any leftover host mode.
    async Task Attach(Bragi b)
    {
        _bragi = b; _host = false; _foreignSeen = 0; _freeze = new();
        if (_listenFirst > TimeSpan.Zero && await b.Listen(_listenFirst) > 0)
        {
            _foreignSeen = b.ForeignReplies;
            _yield.Foreign(Now);
            _log.Note("connect: another host is using the keyboard: yielding without touching it");
            return;
        }
        await b.StartSession();
        var (status, mode) = await b.Get(Bragi.PropMode);
        if (Recovery.NeedsReleaseOnStart(status, mode))
        {
            _log.Note($"connect: keyboard left in mode {mode} (status {status}) by an earlier run: releasing first");
            await b.Release();
        }
        SawForeign();
    }

    // Show one frame. Returns false if the frame was dropped (rate cap, frozen, yielded, unplugged, disposed) or failed.
    public async Task<bool> Show(byte[] frame)
    {
        if (_disposed || Frozen || Yielded || !Connected) return false;
        if (_rate.WaitBefore(DateTime.UtcNow) > TimeSpan.Zero) return false;   // drop, never queue
        if (!await _io.WaitAsync(0)) return false;                             // a frame or a check is already in flight
        try
        {
            return await Guarded(async b =>
            {
                if (Yielded) return false;
                if (_host && Now - _modeCheckedAt >= ModeCheckEvery && !await CheckMode(b)) return false;
                if (!_host)
                {
                    var ok = await b.SetMode((uint)KeyboardMode.HostControlled) == 0 && await b.OpenLive() == 0;
                    _freeze.Record(ok);
                    if (SawForeign() || !ok) return false;
                    _host = true; _modeCheckedAt = Now;
                }
                _rate.Sent(DateTime.UtcNow);
                var sent = await b.WriteFrame(frame) == 0;
                _freeze.Record(sent);
                return !SawForeign() && sent;
            });
        }
        finally { _io.Release(); }
    }

    // Hand the keys back to the onboard profile. Safe to call any number of times. Does nothing while yielded
    // (another host owns the keys; an unbind could break its file transfer) or unplugged (a replug resets the mode).
    public async Task<bool> Release()
    {
        await _io.WaitAsync();
        try
        {
            if (Yielded || _bragi is null) { _host = false; return true; }
            return await Guarded(async b => { _host = false; return await b.Release(); });
        }
        finally { _io.Release(); }
    }

    // Must hold _io. Reads the mode while we hold the keys; yields if someone else changed it.
    async Task<bool> CheckMode(Bragi b)
    {
        var (s, m) = await b.Get(Bragi.PropMode);
        _modeCheckedAt = Now;
        _freeze.Record(s == 0);
        _yield.ModeSeen(Now, s, m);
        if (_yield.Yielded) { OnYield(); return false; }
        return !SawForeign() && s == 0;
    }

    // Must hold _io. True (and yields) if replies we didn't ask for arrived since the last look.
    bool SawForeign()
    {
        if (_bragi is null || _bragi.ForeignReplies == _foreignSeen) return false;
        _foreignSeen = _bragi.ForeignReplies;
        _yield.Foreign(Now);
        OnYield();
        return true;
    }

    void OnYield()
    {
        if (_host) _log.Note($"yield: {_yield.Reason}. Not sending anything until it has been quiet for a while");
        _host = false;
    }

    // Must hold _io. Runs one exchange with the keyboard and turns failures into policy: an unplugged keyboard
    // disconnects (not a freeze), an expired session restarts, anything else counts toward the freeze policy.
    async Task<bool> Guarded(Func<Bragi, Task<bool>> body)
    {
        if (_bragi is not { } b) return false;
        try { return await body(b); }
        catch (SessionExpiredException e)
        {
            _log.Note($"{e.Message}: starting a new session");
            _host = false; _freeze.Record(false);
            try { await b.StartSession(); } catch (Exception e2) when (e2 is TimeoutException or IOException) { _freeze.Record(false); }
        }
        catch (Exception e) when (e is TimeoutException or IOException)
        {
            _log.Note($"keyboard: {e.Message}");
            if (Find() is null) { Disconnected(); return false; }
            _freeze.Record(false);
        }
        await TripIfFrozen();
        return false;
    }

    void Disconnected()
    {
        _log.Note("keyboard unplugged: waiting for it to come back");
        _bragi?.Dispose(); _bragi = null; _host = false;
    }

    async Task TripIfFrozen()
    {
        if (!_freeze.Tripped || _bragi is null) return;
        _log.Note("freeze policy tripped: releasing and refusing further frames");
        try { _host = false; await _bragi.Release(); } catch (Exception e) { _log.Note($"release after freeze failed: {e.Message}"); }
    }

    // Background loop, once a second: reconnect after a replug, watch for quiet while yielded, check the mode while idle in host mode.
    async Task Supervise()
    {
        while (!_stop.IsCancellationRequested)
        {
            try { await Task.Delay(Tick, _stop.Token); } catch (OperationCanceledException) { return; }
            try
            {
                await _io.WaitAsync(_stop.Token);
                try
                {
                    if (Frozen && Connected) { if (Find() is null) Disconnected(); }   // a freeze is final until the keyboard is replugged
                    else if (!Connected) await TryReconnect();
                    else if (Yielded) await Guarded(WatchWhileYielded);
                    else if (_host && Now - _modeCheckedAt >= ModeCheckEvery) await Guarded(CheckMode);
                }
                finally { _io.Release(); }
            }
            catch (OperationCanceledException) { return; }
            catch (Exception e) { _log.Note($"supervisor: {e.Message}"); }
        }
    }

    async Task TryReconnect()
    {
        if (Open(_log) is not { } b) return;
        try
        {
            await Attach(b);
            _log.Note(Yielded ? "keyboard back, but another host is using it: yielding" : "keyboard back: reconnected");
        }
        catch (Exception e) when (e is TimeoutException or IOException)
        {
            _log.Note($"reconnect failed, will retry: {e.Message}");
            b.Dispose(); _bragi = null;
        }
    }

    async Task<bool> WatchWhileYielded(Bragi b)
    {
        if (await b.Listen(ListenWindow) > 0) { _foreignSeen = b.ForeignReplies; _yield.Foreign(Now); return false; }
        if (!_yield.QuietLongEnough(Now)) return false;
        await b.StartSession();
        var (s, m) = await b.Get(Bragi.PropMode);
        if (SawForeign() || !_yield.TryResume(Now, s, m)) return false;
        var released = await b.Release();
        if (SawForeign()) return false;
        _log.Note($"resume: quiet for a while, mode was {m}, released (confirmed: {released}); frames allowed again");
        _freeze = new();
        return true;
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
        _stop.Cancel();
        try { _supervisor.Wait(TimeSpan.FromSeconds(5)); } catch (AggregateException) { }
        try { Release().GetAwaiter().GetResult(); } catch (Exception e) { _log.Note($"dispose: release failed: {e.Message}"); }
        _bragi?.Dispose();
    }
}
