// Guard: releases the Galleon keys when the process driving them dies, for any reason.
// The keyboard never hands control back on its own (crash test, lab/G2-RESULTS.md), so a separate
// process watches the helper and, the moment it exits, opens a fresh session and releases.
// Usage: Guard <pid-to-watch>
// The helper starts Guard detached with its own PID. If the helper released cleanly, Guard finds the
// keyboard already self-operated and does nothing.
using System.Diagnostics;
using GalleonLink;

if (args.Length != 1 || !int.TryParse(args[0], out var pid)) { Console.Error.WriteLine("usage: Guard <pid>"); return 2; }

var logs = Path.Combine(AppContext.BaseDirectory, "logs");
Directory.CreateDirectory(logs);
using var log = new Log(Path.Combine(logs, $"guard-{pid}-{DateTime.Now:yyyyMMdd-HHmmss}.log"));

Process target;
try { target = Process.GetProcessById(pid); }
catch (ArgumentException) { log.Note($"pid {pid} not running at start: checking the keyboard now"); target = null!; }

if (target is not null)
{
    log.Note($"watching pid {pid} ({target.ProcessName})");
    await target.WaitForExitAsync();
    log.Note($"pid {pid} exited: making sure the keys are released");
}

var started = Stopwatch.StartNew();
for (int attempt = 1; attempt <= 3; attempt++)
{
    try
    {
        // Connect() releases by itself when it finds the keyboard in host mode.
        using var kb = await Keyboard.Connect(log, listenFirst: TimeSpan.Zero);
        log.Note($"guard done in {started.ElapsedMilliseconds} ms (attempt {attempt})");
        return 0;
    }
    catch (Exception e) when (e is IOException or TimeoutException)
    {
        log.Note($"attempt {attempt}: {e.Message}");
        await Task.Delay(500 * attempt);
    }
}
log.Note("!!! guard could not release the keyboard: unplug and replug it");
return 1;
