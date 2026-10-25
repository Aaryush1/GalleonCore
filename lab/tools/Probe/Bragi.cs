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
    public const byte OpSet = 0x01, OpGet = 0x02, OpUnbind = 0x05, OpWriteBegin = 0x06, OpWriteNext = 0x07,
        OpRead = 0x08, OpDescribe = 0x09, OpOpen = 0x0D, OpPing = 0x12, OpSession = 0x1B;
    public const ushort LiveResource = 0x22, PropMode = 3;
    public const int MaxLiveBytes = 3 * 146 + 2;
    static readonly HashSet<byte> ReadOnlyOps = [OpGet, OpUnbind, OpRead, OpDescribe, OpOpen, OpPing, OpSession];

    readonly FileStream _dev;
    readonly int _outLen, _inLen;
    readonly Log _log;
    readonly bool _live;      // G2: also allows mode 1/2 and writes to a buffer bound to 0x22
    byte _session;
    bool _bound22;

    public Bragi(HidCollection c, Log log, bool live = false)
    {
        _dev = Hid.OpenShared(c.Path);
        _outLen = c.OutputLen; _inLen = c.InputLen; _log = log; _live = live;
    }

    public void Dispose() => _dev.Dispose();

    // Pure allowlist, testable without a device. Throws RefusedException for anything not permitted.
    public static void Check(byte op, ReadOnlySpan<byte> payload, bool live, bool bound22)
    {
        if (op == OpSet)
        {
            if (!live) throw new RefusedException("set property is not allowed outside --g2");
            if (payload.Length != 6) throw new RefusedException("set takes [prop LE16, value LE32]");
            var prop = (ushort)(payload[0] | payload[1] << 8);
            var value = BitConverter.ToUInt32(payload[2..6]);
            if (prop != PropMode) throw new RefusedException($"only property 3 may be set, not {prop}");
            if (value is not (1 or 2)) throw new RefusedException($"mode {value} refused (3/5 bootloader, 4 sleep, 6/7 unknown)");
            return;
        }
        if (op is OpWriteBegin or OpWriteNext)
        {
            if (!live) throw new RefusedException("writes are not allowed outside --g2");
            if (!bound22) throw new RefusedException("writes only while buffer 0 is bound to resource 0x22");
            if (payload.Length < 1 || payload[0] != 0) throw new RefusedException("writes only to buffer 0");
            if (op == OpWriteBegin)
            {
                if (payload.Length < 5) throw new RefusedException("write-begin needs [buf, len LE32]");
                var len = BitConverter.ToUInt32(payload[1..5]);
                if (len == 0 || len > MaxLiveBytes) throw new RefusedException($"live frame length {len} outside 1..{MaxLiveBytes}");
            }
            return;
        }
        if (!ReadOnlyOps.Contains(op)) throw new RefusedException($"op 0x{op:X2} is not allowed");
        if (op == OpSession && (payload.Length != 6 || payload[0] != 1)) throw new RefusedException("only session start [1, rnd4, 0] is allowed");
        if (op == OpPing && payload.Length != 0) throw new RefusedException("ping with a payload is pairing");
        if (op == OpUnbind && (payload.Length != 2 || payload[0] != 1 || payload[1] > 3)) throw new RefusedException("unbind must be [1, buf 0..3]");
        if (op is OpRead or OpDescribe && (payload.Length != 1 || payload[0] > 3)) throw new RefusedException("read/describe take one buffer index 0..3");
        if (op == OpOpen && (payload.Length != 3 || payload[0] > 3)) throw new RefusedException("open takes [buf, fileId LE16]");
        if (op == OpGet && payload.Length != 2) throw new RefusedException("get takes a 2-byte property id");
    }

    // Splits a frame into op-6/op-7 payloads that fit one report (Web Hub's writeBufferBegin/writeBuffer layout).
    public static List<(byte op, byte[] payload)> Chunks(byte[] data, int reportLen)
    {
        var bodyLen = reportLen - 1 - 4;                 // minus report ID and the 4-byte header
        var first = Math.Min(data.Length, bodyLen - 5);  // [buf, len LE32]
        var list = new List<(byte, byte[])> { (OpWriteBegin, [0, .. BitConverter.GetBytes((uint)data.Length), .. data[..first]]) };
        for (int o = first; o < data.Length; o += bodyLen - 1)
            list.Add((OpWriteNext, [0, .. data[o..Math.Min(data.Length, o + bodyLen - 1)]]));
        return list;
    }

    public async Task<Reply> Send(byte op, byte[] payload, int timeoutMs = 3000)
    {
        Check(op, payload, _live, _bound22);
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
            if (raw.Length > 5 && raw[3] == op && raw[0] == 0)
            {
                if (op == OpOpen) _bound22 = raw[4] == 0 && payload[0] == 0 && (payload[1] | payload[2] << 8) == LiveResource;
                if (op == OpUnbind && payload[1] == 0) _bound22 = false;
                return new Reply(raw);
            }
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

    public async Task<byte> SetMode(uint mode)
    {
        var r = await Send(OpSet, [(byte)PropMode, 0, .. BitConverter.GetBytes(mode)]);
        _log.Note($"set mode {mode}: status {r.Status}");
        return r.Status;
    }

    public async Task<byte> OpenLive()
    {
        var r = await Send(OpOpen, [0, (byte)LiveResource, (byte)(LiveResource >> 8)]);
        _log.Note($"open 0x22: status {r.Status}");
        return r.Status;
    }

    // One frame: op 6 then op 7 continuations. Returns the first non-zero status, or 0.
    public async Task<byte> WriteFrame(byte[] data)
    {
        foreach (var (op, payload) in Chunks(data, _outLen))
        {
            var r = await Send(op, payload);
            if (r.Status != 0) { _log.Note($"write op 0x{op:X2}: status {r.Status}"); return r.Status; }
        }
        return 0;
    }

    // Always safe to call: unbind buffer 0, back to self-operated mode, then confirm.
    public async Task<bool> Release()
    {
        try { await Send(OpUnbind, [1, 0]); } catch (Exception e) { _log.Note($"release: unbind failed: {e.Message}"); }
        try { await SetMode(1); } catch (Exception e) { _log.Note($"release: set mode 1 failed: {e.Message}"); }
        try { var (s, m) = await Get(PropMode); _log.Note($"release: mode now {m} (status {s})"); return s == 0 && m == 1; }
        catch (Exception e) { _log.Note($"release: could not confirm mode: {e.Message}"); return false; }
    }

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
