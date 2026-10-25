// Pure decisions for the keyboard link, kept apart from I/O so they're testable without the device.
namespace GalleonLink;

// Never more than one frame per interval (20 fps cap: faster streaming stalled keystrokes on another Bragi board).
public sealed class RateLimiter(TimeSpan minInterval)
{
    DateTime _last = DateTime.MinValue;
    public static RateLimiter Cap20Fps() => new(TimeSpan.FromMilliseconds(50));

    // How long to wait before the next frame may go out, given the current time.
    public TimeSpan WaitBefore(DateTime now) => _last == DateTime.MinValue || now - _last >= minInterval ? TimeSpan.Zero : minInterval - (now - _last);
    public void Sent(DateTime now) => _last = now;
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
