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
        Expect("rate: a second frame right after goes (burst of 2)", rate.WaitBefore(t0) == TimeSpan.Zero);
        rate.Sent(t0);
        Expect("rate: a third frame right after waits 50 ms", rate.WaitBefore(t0) == TimeSpan.FromMilliseconds(50));
        Expect("rate: 10 ms later it waits 40 ms", rate.WaitBefore(t0.AddMilliseconds(10)) == TimeSpan.FromMilliseconds(40));
        Expect("rate: 50 ms later it goes", rate.WaitBefore(t0.AddMilliseconds(50)) == TimeSpan.Zero);
        int Greedy(int ms) { var r = RateLimiter.Cap20Fps(); int k = 0; for (int t = 0; t <= ms; t++) if (r.WaitBefore(t0.AddMilliseconds(t)) == TimeSpan.Zero) { r.Sent(t0.AddMilliseconds(t)); k++; } return k; }
        Expect("rate: a greedy caller gets at most 20 fps + burst over 10 s", Greedy(10_000) <= 202);
        int Jittery() { var r = RateLimiter.Cap20Fps(); int k = 0; for (int i = 1; i <= 200; i++) { var t = t0.AddMilliseconds(50 * i + (i % 2 == 0 ? 15 : 0)); if (r.WaitBefore(t) == TimeSpan.Zero) { r.Sent(t); k++; } } return k; }
        Expect("rate: a caller pacing at 50 ms with 15 ms jitter loses no frames", Jittery() == 200);

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

        // G3 follow-up: telling our replies from another host's.
        byte[] R(byte sub, byte op) => [sub, 0, 0, op, 0, 0, 0, 0];
        Expect("replies: echo of the op we wait for is ours", Replies.Classify(R(0, 0x07), 0x07, 0) == ReplyKind.Ours);
        Expect("replies: a different op is foreign", Replies.Classify(R(0, 0x02), 0x07, 0) == ReplyKind.Foreign);
        Expect("replies: anything while only listening is foreign", Replies.Classify(R(0, 0x02), 0, 0) == ReplyKind.Foreign);
        Expect("replies: late echo of our timed-out op is stale", Replies.Classify(R(0, 0x06), 0x02, 0x06) == ReplyKind.Stale);
        Expect("replies: non-zero sub byte is foreign", Replies.Classify(R(1, 0x07), 0x07, 0) == ReplyKind.Foreign);
        Expect("replies: a short report is foreign", Replies.Classify([0, 0, 0, 7], 0x07, 0) == ReplyKind.Foreign);

        // G3 follow-up: yield, never fight; resume only after 60 s of quiet.
        var y = YieldPolicy.Default();
        y.ModeSeen(t0, 0, 2); Expect("yield: mode still 2 keeps going", !y.Yielded);
        y.ModeSeen(t0, 5, 0); Expect("yield: an unreadable mode is not a yield (freeze policy's job)", !y.Yielded);
        y.ModeSeen(t0, 0, 1); Expect("yield: mode changed to 1 behind our back yields", y.Yielded);
        Expect("yield: no resume before 60 s", !y.TryResume(t0.AddSeconds(59), 0, 1) && y.Yielded);
        Expect("yield: resume after 60 s quiet with mode 1", y.TryResume(t0.AddSeconds(60), 0, 1) && !y.Yielded);
        y = YieldPolicy.Default();
        y.Foreign(t0); Expect("yield: a foreign reply yields", y.Yielded);
        y.Foreign(t0.AddSeconds(50)); Expect("yield: more foreign traffic restarts the quiet clock", !y.TryResume(t0.AddSeconds(100), 0, 1));
        Expect("yield: 60 s after the last foreign reply resumes", y.TryResume(t0.AddSeconds(110), 0, 1));
        y = YieldPolicy.Default(); y.Foreign(t0);
        Expect("yield: mode 2 after the quiet period resumes (our leftover frame, caller releases)", y.TryResume(t0.AddSeconds(60), 0, 2));
        y = YieldPolicy.Default(); y.Foreign(t0);
        Expect("yield: unreadable mode after quiet waits", !y.TryResume(t0.AddSeconds(60), 5, 0) && y.Yielded);
        Expect("yield: ...and waits a whole new quiet period", !y.TryResume(t0.AddSeconds(100), 0, 1) && y.TryResume(t0.AddSeconds(120), 0, 1));
        foreach (var odd in new uint[] { 0, 3, 4, 7 })
        {
            y = YieldPolicy.Default(); y.Foreign(t0);
            Expect($"yield: never resumes into mode {odd}", !y.TryResume(t0.AddSeconds(61), 0, odd));
        }

        Console.WriteLine(fail == 0 ? "SELFTEST PASSED" : $"SELFTEST FAILED: {fail}");
        return fail == 0 ? 0 : 1;
    }
}
