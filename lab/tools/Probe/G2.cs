// G2 steps (lab/G2-PLAN.md). Every step ends in Release() (unbind, mode 1, confirm), including on Ctrl-C.
namespace Probe;

static class Frames
{
    public const int Leds = 146;
    public static readonly (byte r, byte g, byte b) Blue = (0, 0, 255), Green = (0, 255, 0), Black = (0, 0, 0);

    // OpenRGB / OpenLinkHub style: `12 00` header, then RGB per LED.
    public static byte[] Interleaved((byte r, byte g, byte b) c, int n = Leds, int only = -1)
    {
        var f = new byte[2 + 3 * n]; f[0] = 0x12;
        for (int i = 0; i < n; i++)
        {
            var k = only < 0 || i == only ? c : Black;
            f[2 + 3 * i] = k.r; f[3 + 3 * i] = k.g; f[4 + 3 * i] = k.b;
        }
        return f;
    }

    // galleon-deck style: all R, then all G, then all B, no header.
    public static byte[] Planar((byte r, byte g, byte b) c, int n = Leds)
    {
        var f = new byte[3 * n];
        Array.Fill(f, c.r, 0, n); Array.Fill(f, c.g, n, n); Array.Fill(f, c.b, 2 * n, n);
        return f;
    }

    public static byte[] ForVariant(string v, (byte, byte, byte) c) => v switch
    {
        "interleaved" => Interleaved(c),
        "planar146" => Planar(c),
        "planar138" => Planar(c, 138),
        _ => throw new ArgumentException($"unknown variant {v} (interleaved | planar146 | planar138)"),
    };
}

static class G2
{
    public const byte F1Usage = 0x3A;

    public static async Task<int> Run(string step, string? variant, Bragi bragi, Log log, CancellationToken stop)
    {
        try
        {
            switch (step)
            {
                case "2.1":
                    if (await bragi.SetMode(2) != 0) return 10;
                    log.Note(">>> MODE 2 for 3 s: what do the keys show? Type a few letters now.");
                    await Hold(3000, bragi, stop);
                    return 0;

                case "2.2" or "2.3" or "2.4":
                    var v = step switch { "2.2" => "interleaved", "2.3" => "planar146", _ => "planar138" };
                    return await OneFrame(bragi, log, Frames.ForVariant(v, Frames.Blue), $"{v}, all blue", stop);

                case "2.5":
                    return await Stream(bragi, log, Frames.ForVariant(variant ?? "interleaved", Frames.Blue), stop);

                case "2.6":
                    return await OneFrame(bragi, log, Frames.Interleaved(Frames.Green, only: F1Usage), "only LED 0x3A (F1?) green", stop);

                default:
                    log.Note($"unknown step {step}"); return 2;
            }
        }
        catch (OperationCanceledException) { log.Note("stopped (Ctrl-C)"); return 130; }
        catch (Exception e) { log.Note($"STOPPED on error: {e.Message}"); return 11; }
        finally
        {
            var ok = await bragi.Release();
            log.Note(ok ? "RELEASED: keyboard back in self-operated mode." : "!!! Release not confirmed: unplug and replug the keyboard.");
        }
    }

    static async Task<int> OneFrame(Bragi bragi, Log log, byte[] frame, string what, CancellationToken stop)
    {
        if (await bragi.SetMode(2) != 0) return 10;
        if (await bragi.OpenLive() != 0) return 12;
        log.Note($"writing one frame: {what}, {frame.Length} bytes");
        var s = await bragi.WriteFrame(frame);
        if (s != 0) return 13;
        log.Note(">>> FRAME SENT. Holding 3 s: what do the keys show? Type a few letters now.");
        await Hold(3000, bragi, stop);
        return 0;
    }

    static async Task<int> Stream(Bragi bragi, Log log, byte[] frame, CancellationToken stop)
    {
        if (await bragi.SetMode(2) != 0) return 10;
        if (await bragi.OpenLive() != 0) return 12;
        log.Note(">>> STREAMING 10 fps for 10 s. Type normally and watch for lag.");
        for (int i = 0; i < 100; i++)
        {
            stop.ThrowIfCancellationRequested();
            var t = Task.Delay(100, stop);          // 10 fps, well under the 20 fps cap
            if (await bragi.WriteFrame(frame) != 0) return 13;
            await t;
        }
        return 0;
    }

    // Wait, pinging once a second so the session stays alive.
    static async Task Hold(int ms, Bragi bragi, CancellationToken stop)
    {
        for (int t = 0; t < ms; t += 1000) { await Task.Delay(Math.Min(1000, ms - t), stop); await bragi.Ping(); }
    }
}
