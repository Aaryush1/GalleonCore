// The plugin's line into the helper: named pipe \\.\pipe\desk-lights, JSON lines both ways (lab/DESK-LIGHTS-PROTOCOL.md).
// One client at a time; a new client replaces the old one. Only the current user can connect.
using System.IO.Pipes;
using System.Security.AccessControl;
using System.Security.Principal;
using System.Text;
using GalleonLink;

namespace DeskLights;

public sealed class PipeServer(string name, Log log, Action<string> onLine, Func<string> greeting)
{
    StreamWriter? _out;
    readonly object _gate = new();

    public async Task Run(CancellationToken stop)
    {
        while (!stop.IsCancellationRequested)
        {
            NamedPipeServerStream? pipe = null;
            try
            {
                pipe = Create();
                await pipe.WaitForConnectionAsync(stop);
                log.Note("plugin connected");
                var writer = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true, NewLine = "\n" };
                lock (_gate) _out = writer;
                Send(greeting());
                await ReadLines(pipe, stop);
                log.Note("plugin disconnected");
            }
            catch (OperationCanceledException) { break; }
            catch (Exception e) { log.Note($"pipe: {e.Message}"); await Task.Delay(1000, stop).ContinueWith(_ => { }); }
            finally
            {
                lock (_gate) _out = null;
                pipe?.Dispose();
            }
        }
    }

    NamedPipeServerStream Create()
    {
        var security = new PipeSecurity();
        security.AddAccessRule(new PipeAccessRule(WindowsIdentity.GetCurrent().User!, PipeAccessRights.FullControl, AccessControlType.Allow));
        return NamedPipeServerStreamAcl.Create(name, PipeDirection.InOut, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous, 0, 0, security);
    }

    async Task ReadLines(Stream pipe, CancellationToken stop)
    {
        var buf = new byte[4096];
        var line = new List<byte>();
        while (true)
        {
            var n = await pipe.ReadAsync(buf, stop);
            if (n == 0) return;
            for (int i = 0; i < n; i++)
            {
                if (buf[i] != (byte)'\n') { line.Add(buf[i]); if (line.Count > Protocol.MaxLine) { log.Note("pipe: line over 64 KiB, dropping the client"); return; } continue; }
                var text = Encoding.UTF8.GetString(line.ToArray()).TrimEnd('\r');
                line.Clear();
                if (text.Length > 0) onLine(text);
            }
        }
    }

    public void Send(string line)
    {
        lock (_gate)
        {
            try { _out?.WriteLine(line); }
            catch (IOException) { _out = null; }
        }
    }
}
