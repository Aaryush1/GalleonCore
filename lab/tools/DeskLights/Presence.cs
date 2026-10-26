// Whether the owner is at the desk, for the plugin's away digest: seconds since the last input of any kind
// (GetLastInputInfo) and whether the session is locked (SessionSwitch). Reads timing only; never which keys.
using System.Runtime.InteropServices;
using Microsoft.Win32;

namespace DeskLights;

public sealed class Presence : IDisposable
{
    [StructLayout(LayoutKind.Sequential)]
    struct LastInputInfo { public uint Size; public uint Time; }

    [DllImport("user32.dll")]
    static extern bool GetLastInputInfo(ref LastInputInfo info);

    volatile bool _locked;   // assumed unlocked at start; the helper starts at login, in an unlocked session

    public bool Locked => _locked;

    // Raised on lock and unlock, on a system thread.
    public event Action? Changed;

    public Presence() => SystemEvents.SessionSwitch += OnSwitch;

    void OnSwitch(object? s, SessionSwitchEventArgs e)
    {
        switch (e.Reason)
        {
            case SessionSwitchReason.SessionLock: _locked = true; break;
            case SessionSwitchReason.SessionUnlock: _locked = false; break;
            default: return;
        }
        Changed?.Invoke();
    }

    public static int IdleSeconds()
    {
        var info = new LastInputInfo { Size = (uint)Marshal.SizeOf<LastInputInfo>() };
        if (!GetLastInputInfo(ref info)) return 0;
        var ms = unchecked((uint)Environment.TickCount - info.Time);   // 32-bit tick counts wrap; unsigned subtraction handles it
        return (int)Math.Min(ms / 1000, int.MaxValue);
    }

    public void Dispose() => SystemEvents.SessionSwitch -= OnSwitch;
}
