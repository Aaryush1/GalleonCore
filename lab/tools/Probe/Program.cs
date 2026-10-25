// G1 read-only probe for the Galleon 100 SD keyboard part (1b1c:2b0c). See lab/G0-SPEC.md.
// Usage: Probe a          list HID collections (no commands sent)
//        Probe b          session, read properties, ping
//        Probe c          b, then a read-only backup of the onboard profiles
using System.Text.Json;
using Probe;

const ushort Vid = 0x1B1C, Pid = 0x2B0C;
var stage = args.Length > 0 ? args[0] : "a";
var lab = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", ".."));
var stamp = DateTime.Now.ToString("yyyyMMdd-HHmmss");
Directory.CreateDirectory(Path.Combine(lab, "logs"));
using var log = new Log(Path.Combine(lab, "logs", $"g1-{stage}-{stamp}.log"));

var cols = Hid.Enumerate(Vid, Pid);
log.Note($"{cols.Count} HID collections for {Vid:x4}:{Pid:x4}");
foreach (var c in cols)
    log.Note($"  {c.Interface} page 0x{c.UsagePage:X4} usage 0x{c.Usage:X2} in {c.InputLen} out {c.OutputLen} feature {c.FeatureLen}");
if (stage == "a") return 0;

var cmd = cols.Where(c => c.UsagePage == 0xFF42 && c.OutputLen > 0).OrderBy(c => c.Usage == 1 ? 0 : 1).FirstOrDefault();
if (cmd is null) { log.Note("No 0xFF42 collection with an output report: stopping."); return 3; }
log.Note($"Command collection: {cmd.Interface} usage 0x{cmd.Usage:X2}, report length {cmd.OutputLen}");

using var bragi = new Bragi(cmd, log);
await bragi.StartSession();

var (st, mode) = await bragi.Get(3);
log.Note($"operating mode (prop 3): status {st}, value {mode}");
if (st != 0 || mode != 1) { log.Note("Keyboard is not in self-operated mode (1): stopping without touching anything else."); return 4; }

var props = new (ushort id, string name)[]
{
    (261, "bragi version"), (19, "firmware"), (110, "active profile"), (2, "brightness (0-1000)"),
    (65, "physical layout"), (62, "free storage"), (58, "?58"), (69, "?69"), (0x11, "?0x11"), (0x12, "?0x12 (pid per OpenRGB)"),
};
var values = new Dictionary<string, object>();
foreach (var (id, name) in props)
{
    try { var (s, v) = await bragi.Get(id); log.Note($"prop {id} {name}: status {s}, value {v} (0x{v:X8})"); values[$"{id}"] = new { name, status = s, value = v }; }
    catch (TimeoutException e) { log.Note($"prop {id} {name}: {e.Message}"); }
}
var ping = await bragi.Ping();
log.Note($"ping: status {ping.Status}");
if (stage == "b") return 0;

if (stage == "d")   // describe the candidate live-colour resources without reading or writing them
{
    foreach (ushort res in new ushort[] { 0x22, 0x01 })
    {
        try
        {
            var open = await bragi.Send(Bragi.OpOpen, [0, (byte)res, (byte)(res >> 8)]);
            log.Note($"resource 0x{res:X2}: open status {open.Status}");
            if (open.Status == 0)
            {
                var d = await bragi.Send(Bragi.OpDescribe, [0]);
                log.Note($"resource 0x{res:X2}: describe status {d.Status}, data {Convert.ToHexString(d.Data[..8])}, size {BitConverter.ToUInt32(d.Data.Slice(2, 4))}");
            }
        }
        finally { await bragi.Send(Bragi.OpUnbind, [1, 0]); }
    }
    return 0;
}

// Stage c: read-only backup, following Web Hub's own export walk.
var outDir = Path.Combine(lab, "backups", stamp);
Directory.CreateDirectory(outDir);
var seen = new HashSet<ushort>();
// Fixed ids plus the whole user-file range in use (28000+). A missing file just answers status 6.
var queue = new Queue<ushort>(new ushort[] { 38, 31000, 15 }.Concat(Enumerable.Range(28000, 31).Select(i => (ushort)i)));
var budget = 80;
while (queue.Count > 0 && budget-- > 0)
{
    var id = queue.Dequeue();
    if (id == 0 || !seen.Add(id)) continue;
    var bytes = await bragi.ReadFile(id);
    if (bytes is null) continue;
    await File.WriteAllBytesAsync(Path.Combine(outDir, $"file-{id}.bin"), bytes);
    log.Note($"file {id}: {bytes.Length} bytes saved");

}
await File.WriteAllTextAsync(Path.Combine(outDir, "properties.json"), JsonSerializer.Serialize(values, new JsonSerializerOptions { WriteIndented = true }));
log.Note($"backup: {seen.Count} files tried, saved to {outDir}");
return 0;
