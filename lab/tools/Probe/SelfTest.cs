// Offline checks: the allowlist refuses what it must, and frames chunk into reports correctly. No device needed.
using GalleonLink;

namespace Probe;

static class SelfTest
{
    public static int Run()
    {
        int fail = 0;
        void Expect(string name, bool ok) { Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {name}"); if (!ok) fail++; }
        bool Refused(byte op, byte[] p, bool live, bool bound) { try { Bragi.Check(op, p, live, bound); return false; } catch (RefusedException) { return true; } }
        byte[] SetMode(uint v) => [3, 0, .. BitConverter.GetBytes(v)];
        bool Throws(Action a) { try { a(); return false; } catch (ArgumentOutOfRangeException) { return true; } }

        // Modes: only 1 and 2, only in live mode.
        Expect("set mode 2 refused outside --g2", Refused(Bragi.OpSet, SetMode(2), false, false));
        Expect("set mode 1 allowed in --g2", !Refused(Bragi.OpSet, SetMode(1), true, false));
        Expect("set mode 2 allowed in --g2", !Refused(Bragi.OpSet, SetMode(2), true, false));
        foreach (var v in new uint[] { 0, 3, 4, 5, 6, 7, 255 })
            Expect($"set mode {v} refused", Refused(Bragi.OpSet, SetMode(v), true, false));
        Expect("set property 110 refused", Refused(Bragi.OpSet, [110, 0, 1, 0, 0, 0], true, false));
        Expect("set property 2 (brightness) refused", Refused(Bragi.OpSet, [2, 0, 0, 0, 0, 0], true, false));

        // Dangerous opcodes never pass, live or not.
        foreach (var op in new byte[] { 0x0A, 0x0B, 0x0C, 0x0F, 0x03, 0x04, 0x0E, 0x10, 0x11 })
            Expect($"op 0x{op:X2} refused", Refused(op, [0, 0, 0], true, true));
        Expect("ping with payload (pairing) refused", Refused(Bragi.OpPing, [1], true, true));
        Expect("session stop refused", Refused(Bragi.OpSession, [2, 0, 0, 0, 0, 1], true, true));

        // Writes: only live, only bound to 0x22, only buffer 0, bounded length.
        byte[] Begin(uint len) => [0, .. BitConverter.GetBytes(len), 1, 2, 3];
        Expect("write refused outside --g2", Refused(Bragi.OpWriteBegin, Begin(440), false, true));
        Expect("write refused when not bound to 0x22", Refused(Bragi.OpWriteBegin, Begin(440), true, false));
        Expect("write allowed when bound to 0x22", !Refused(Bragi.OpWriteBegin, Begin(440), true, true));
        Expect("write to buffer 1 refused", Refused(Bragi.OpWriteBegin, [1, .. BitConverter.GetBytes(440u)], true, true));
        Expect("oversized frame refused", Refused(Bragi.OpWriteBegin, Begin(Bragi.MaxLiveBytes + 1), true, true));
        Expect("zero-length frame refused", Refused(Bragi.OpWriteBegin, Begin(0), true, true));

        // Chunking for 129-byte reports (128 + report ID): first chunk 119 data bytes, then 123.
        foreach (var frame in new[] { Frames.Interleaved(Frames.Blue), Frames.Planar(Frames.Blue), Frames.Planar(Frames.Blue, 138) })
        {
            var chunks = Bragi.Chunks(frame, 129);
            var rebuilt = chunks[0].payload[5..].Concat(chunks.Skip(1).SelectMany(c => c.payload[1..])).ToArray();
            Expect($"{frame.Length}-byte frame reassembles exactly", rebuilt.SequenceEqual(frame));
            Expect($"{frame.Length}-byte frame: every chunk fits a report", chunks.All(c => 5 + c.payload.Length <= 129));
            Expect($"{frame.Length}-byte frame: declared length matches", BitConverter.ToUInt32(chunks[0].payload, 1) == frame.Length);
            Expect($"{frame.Length}-byte frame: op 6 then op 7s", chunks[0].op == Bragi.OpWriteBegin && chunks.Skip(1).All(c => c.op == Bragi.OpWriteNext));
        }
        var inter = Frames.Interleaved(Frames.Blue);
        Expect("interleaved header is 12 00", inter[0] == 0x12 && inter[1] == 0 && inter.Length == 440);
        var planar = Frames.Planar(Frames.Blue);
        Expect("planar is R block, G block, B block", planar[..146].All(b => b == 0) && planar[292..].All(b => b == 255));
        var one = Frames.Interleaved(Frames.Green, only: G2.F1Usage);
        Expect("single-LED frame lights only index 0x3A", one[2 + 3 * 0x3A + 1] == 255 && one.Count(b => b == 255) == 1);

        // G3: per-key frames by HID usage.
        var keyed = Frames.FromKeys(Rgb.Hex("#0f766e"), new Dictionary<byte, Rgb> { [0x3A] = Rgb.Hex("#2563eb"), [0x04] = Rgb.Hex("#ffffff") });
        Expect("FromKeys: 440 bytes with 12 00 header", keyed.Length == 440 && keyed[0] == 0x12 && keyed[1] == 0);
        Expect("FromKeys: F1 (0x3A) blue", keyed[2 + 3 * 0x3A] == 0x25 && keyed[3 + 3 * 0x3A] == 0x63 && keyed[4 + 3 * 0x3A] == 0xEB);
        Expect("FromKeys: A (0x04) white", keyed[2 + 3 * 4] == 255 && keyed[3 + 3 * 4] == 255 && keyed[4 + 3 * 4] == 255);
        Expect("FromKeys: other LEDs get the background", keyed[2 + 3 * 0x05] == 0x0f && keyed[3 + 3 * 0x05] == 0x76 && keyed[4 + 3 * 0x05] == 0x6e);
        Expect("FromKeys: index >= 146 rejected", Throws(() => Frames.FromKeys(Rgb.Black, new Dictionary<byte, Rgb> { [146] = Rgb.Black })));

        // G3: 20 fps cap.
        var rate = RateLimiter.Cap20Fps(); var t0 = new DateTime(2026, 10, 25, 12, 0, 0, DateTimeKind.Utc);
        Expect("rate: first frame goes at once", rate.WaitBefore(t0) == TimeSpan.Zero);
        rate.Sent(t0);
        Expect("rate: a frame 10 ms later must wait 40 ms", rate.WaitBefore(t0.AddMilliseconds(10)) == TimeSpan.FromMilliseconds(40));
        Expect("rate: a frame 50 ms later goes", rate.WaitBefore(t0.AddMilliseconds(50)) == TimeSpan.Zero);

        // G3: freeze policy trips on 2 consecutive failures, a success resets.
        var fp = new FreezePolicy();
        fp.Record(false); Expect("freeze: one failure does not trip", !fp.Tripped);
        fp.Record(true); fp.Record(false); Expect("freeze: a success in between resets the count", !fp.Tripped);
        fp.Record(false); Expect("freeze: two in a row trips", fp.Tripped);
        fp.Record(true); Expect("freeze: once tripped it stays tripped", fp.Tripped);

        // G3: release on start unless the keyboard is confirmed self-operated.
        Expect("recovery: mode 1 status 0 needs no release", !Recovery.NeedsReleaseOnStart(0, 1));
        Expect("recovery: mode 2 left over needs release", Recovery.NeedsReleaseOnStart(0, 2));
        Expect("recovery: unreadable mode needs release", Recovery.NeedsReleaseOnStart(5, 0));

        Console.WriteLine(fail == 0 ? "SELFTEST PASSED" : $"SELFTEST FAILED: {fail}");
        return fail == 0 ? 0 : 1;
    }
}
