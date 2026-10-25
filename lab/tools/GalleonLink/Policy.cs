// Pure decisions for the keyboard link, kept apart from I/O so they're testable without the device.
namespace GalleonLink;

// At most one frame per interval on average (20 fps cap: faster streaming stalled keystrokes on another Bragi board).
// A token bucket with a burst of 2, so a caller pacing itself at exactly 50 ms isn't dropped by timer jitter
// (Windows timers tick every ~15.6 ms; a strict per-frame gap dropped every other frame in G3).
public sealed class RateLimiter(TimeSpan interval, int burst = 2)
{
    double _tokens = burst;
    DateTime _at = DateTime.MinValue;
    public static RateLimiter Cap20Fps() => new(TimeSpan.FromMilliseconds(50));

    void Refill(DateTime now)
    {
        if (_at != DateTime.MinValue && now > _at) _tokens = Math.Min(burst, _tokens + (now - _at) / interval);
        if (now > _at) _at = now;
    }

    // How long to wait before the next frame may go out, given the current time.
    public TimeSpan WaitBefore(DateTime now) { Refill(now); return _tokens >= 1 ? TimeSpan.Zero : interval * (1 - _tokens); }

    // Call with the time the frame started going out.
    public void Sent(DateTime now) { Refill(now); _tokens -= 1; }
}

// Decides when the link must stop driving the keyboard and release it.
public sealed class FreezePolicy(int maxConsecutiveFailures = 2)
{
    int _failures;
    public bool Tripped { get; private set; }

    // Record one command outcome. A timeout or non-zero status counts as a failure; any success resets the count.
    public void Record(bool ok)
    {
        if (ok) { _failures = 0; return; }
        if (++_failures >= maxConsecutiveFailures) Tripped = true;
    }
}

public enum KeyboardMode : uint { SelfOperated = 1, HostControlled = 2 }

public static class Recovery
{
    // On start: anything but a confirmed self-operated keyboard is left over from a crash, sleep or kill, so release first.
    public static bool NeedsReleaseOnStart(byte status, uint mode) => status != 0 || mode != (uint)KeyboardMode.SelfOperated;
}

public enum ReplyKind { Ours, Stale, Foreign }

public static class Replies
{
    // What an input report on MI_01 is, given the op we're waiting for (0 when only listening) and the op of our
    // last timed-out command (0 if none). A late reply to our own timed-out command is stale, not another host.
    public static ReplyKind Classify(ReadOnlySpan<byte> raw, byte waitingFor, byte staleOp)
    {
        if (raw.Length > 5 && raw[0] == 0 && waitingFor != 0 && raw[3] == waitingFor) return ReplyKind.Ours;
        if (raw.Length > 5 && raw[0] == 0 && staleOp != 0 && raw[3] == staleOp) return ReplyKind.Stale;
        return ReplyKind.Foreign;
    }
}

// Yield, never fight (lab/G3-RESULTS.md, Web Hub check): the device gives no error when another host takes the
// keys, so the link watches for it. Another host is present when (a) the mode we set is no longer 2, or (b) a
// reply we didn't ask for shows up on the shared command interface. Then the link stops sending anything at all
// (so it can't confuse that host's replies, e.g. a Web Hub save) and resumes only after `quiet` with no foreign traffic.
public sealed class YieldPolicy(TimeSpan quiet)
{
    DateTime _quietSince;
    public static YieldPolicy Default() => new(TimeSpan.FromSeconds(60));

    public bool Yielded { get; private set; }
    public string Reason { get; private set; } = "";

    public void Foreign(DateTime now) { _quietSince = now; Yield("another host is talking to the keyboard"); }

    // A mode read while we hold the keys. A failed read says nothing about other hosts (the freeze policy handles it).
    public void ModeSeen(DateTime now, byte status, uint mode)
    {
        if (status == 0 && mode != (uint)KeyboardMode.HostControlled) { _quietSince = now; Yield($"mode changed to {mode} behind our back"); }
    }

    public bool QuietLongEnough(DateTime now) => Yielded && now - _quietSince >= quiet;

    // After the quiet period, with a fresh mode read. Mode 1: the other host is gone. Mode 2 after that long with no
    // traffic is our own leftover frame (the other host never set mode 1). Either way the caller releases (unbind +
    // mode 1, safe now that nobody else is talking) and then resumes. An unreadable mode: wait another quiet period.
    public bool TryResume(DateTime now, byte status, uint mode)
    {
        if (!QuietLongEnough(now)) return false;
        if (status == 0 && mode is (uint)KeyboardMode.SelfOperated or (uint)KeyboardMode.HostControlled) { Yielded = false; return true; }
        _quietSince = now;
        return false;
    }

    void Yield(string why) { if (!Yielded) Reason = why; Yielded = true; }
}
