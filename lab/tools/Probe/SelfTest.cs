// Offline checks: the allowlist refuses what it must, and frames chunk into reports correctly. No device needed.
namespace Probe;

static class SelfTest
{
    public static int Run()
    {
        int fail = 0;
        void Expect(string name, bool ok) { Console.WriteLine($"{(ok ? "ok  " : "FAIL")} {name}"); if (!ok) fail++; }
        bool Refused(byte op, byte[] p, bool live, bool bound) { try { Bragi.Check(op, p, live, bound); return false; } catch (RefusedException) { return true; } }
        byte[] SetMode(uint v) => [3, 0, .. BitConverter.GetBytes(v)];

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

        Console.WriteLine(fail == 0 ? "SELFTEST PASSED" : $"SELFTEST FAILED: {fail}");
        return fail == 0 ? 0 : 1;
    }
}
