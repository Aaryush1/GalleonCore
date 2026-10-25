// Bragi v1.5 transport with a hard allowlist. G1 is read-only: anything that could change
// the keyboard's stored state or mode is refused before it reaches the device.
// Framing per Web Hub v1.2.42 (lab/research/webhub-protocol.md): header [sub, 0x01, session, op],
// reply [sub, flags, ?, op-echo, status, data...]; reply byte1 == 15 means the session expired.
namespace Probe;

sealed class RefusedException(string why) : Exception($"REFUSED by allowlist: {why}");

record Reply(byte[] Raw)
{
    public byte Op => Raw[3];
    public byte Status => Raw[4];
    public ReadOnlySpan<byte> Data => Raw.AsSpan(5);
}

sealed class Bragi : IDisposable
{
    public const byte OpGet = 0x02, OpUnbind = 0x05, OpRead = 0x08, OpDescribe = 0x09, OpOpen = 0x0D, OpPing = 0x12, OpSession = 0x1B;
    static readonly HashSet<byte> ReadOnlyOps = [OpGet, OpUnbind, OpRead, OpDescribe, OpOpen, OpPing, OpSession];

    readonly FileStream _dev;
    readonly int _outLen, _inLen;
    readonly Log _log;
    byte _session;

    public Bragi(HidCollection c, Log log)
    {
        _dev = Hid.OpenShared(c.Path);
        _outLen = c.OutputLen; _inLen = c.InputLen; _log = log;
    }

    public void Dispose() => _dev.Dispose();

    static void Check(byte op, ReadOnlySpan<byte> payload)
    {
        if (!ReadOnlyOps.Contains(op)) throw new RefusedException($"op 0x{op:X2} is not read-only");
        if (op == OpSession && (payload.Length != 6 || payload[0] != 1)) throw new RefusedException("only session start [1, rnd4, 0] is allowed");
        if (op == OpPing && payload.Length != 0) throw new RefusedException("ping with a payload is pairing");
        if (op == OpUnbind && (payload.Length != 2 || payload[0] != 1 || payload[1] > 3)) throw new RefusedException("unbind must be [1, buf 0..3]");
        if (op is OpRead or OpDescribe && (payload.Length != 1 || payload[0] > 3)) throw new RefusedException("read/describe take one buffer index 0..3");
        if (op == OpOpen && (payload.Length != 3 || payload[0] > 3)) throw new RefusedException("open takes [buf, fileId LE16]");
        if (op == OpGet && payload.Length != 2) throw new RefusedException("get takes a 2-byte property id");
    }

    public async Task<Reply> Send(byte op, byte[] payload, int timeoutMs = 3000)
    {
        Check(op, payload);
        var report = new byte[_outLen];              // [0] = report ID 0
        report[2] = 0x01; report[3] = op == OpSession ? (byte)0 : _session; report[4] = op;
        if (5 + payload.Length > report.Length) throw new RefusedException("payload too long");
        payload.CopyTo(report, 5);
        _log.Tx(report.AsSpan(1));
        await _dev.WriteAsync(report);

        using var cts = new CancellationTokenSource(timeoutMs);
        var buf = new byte[_inLen];
        while (true)
        {
            int n;
            try { n = await _dev.ReadAsync(buf, cts.Token); }
            catch (OperationCanceledException) { throw new TimeoutException($"no reply to op 0x{op:X2} in {timeoutMs} ms"); }
            var raw = buf.AsSpan(1, n - 1).ToArray();  // drop report ID
            _log.Rx(raw);
            if (raw.Length > 5 && raw[3] == op && raw[0] == 0) return new Reply(raw);
            _log.Note("  (not our reply, skipped)");
        }
    }

    public async Task<byte> StartSession()
    {
        var rnd = new byte[4]; Random.Shared.NextBytes(rnd);
        var r = await Send(OpSession, [1, .. rnd, 0]);
        _session = r.Raw[9];
        _log.Note($"session start: status {r.Status}, session id {_session}");
        return _session;
    }

    public async Task<(byte status, uint value)> Get(ushort prop)
    {
        var r = await Send(OpGet, [(byte)prop, (byte)(prop >> 8)]);
        return (r.Status, BitConverter.ToUInt32(r.Data[..4]));
    }

    public async Task<Reply> Ping() => await Send(OpPing, []);

    // Web Hub's readFile: open -> describe -> read loop -> unbind (always).
    public async Task<byte[]?> ReadFile(ushort fileId, int maxBytes = 64 * 1024)
    {
        const byte buf = 0;
        try
        {
            var open = await Send(OpOpen, [buf, (byte)fileId, (byte)(fileId >> 8)]);
            if (open.Status != 0) { _log.Note($"  open file {fileId}: status {open.Status}"); return null; }
            var desc = await Send(OpDescribe, [buf]);
            if (desc.Status != 0) { _log.Note($"  describe file {fileId}: status {desc.Status}"); return null; }
            var size = (int)BitConverter.ToUInt32(desc.Data.Slice(2, 4));
            if (size < 0 || size > maxBytes) { _log.Note($"  file {fileId}: implausible size {size}, not reading"); return null; }
            var bytes = new List<byte>(size);
            while (bytes.Count < size)
            {
                var r = await Send(OpRead, [buf]);
                if (r.Status != 0) { _log.Note($"  read file {fileId}: status {r.Status} after {bytes.Count} bytes"); return null; }
                var take = Math.Min(size - bytes.Count, r.Data.Length);
                bytes.AddRange(r.Data[..take].ToArray());
            }
            return bytes.ToArray();
        }
        finally
        {
            try { await Send(OpUnbind, [1, buf]); } catch (Exception e) { _log.Note($"  unbind failed: {e.Message}"); }
        }
    }
}

sealed class Log(string path) : IDisposable
{
    readonly StreamWriter _w = new(path);
    public void Tx(ReadOnlySpan<byte> b) => Write("TX " + Hex(b));
    public void Rx(ReadOnlySpan<byte> b) => Write("RX " + Hex(b));
    public void Note(string s) { Write(s); Console.WriteLine(s); }
    void Write(string s) { _w.WriteLine($"{DateTime.Now:HH:mm:ss.fff} {s}"); _w.Flush(); }
    static string Hex(ReadOnlySpan<byte> b) { var t = b.ToArray(); var last = Array.FindLastIndex(t, x => x != 0); return Convert.ToHexString(t, 0, Math.Min(t.Length, Math.Max(last + 1, 8))); }
    public void Dispose() => _w.Dispose();
}
