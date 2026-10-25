// Snap: take one still photo from a camera so tests can check what the keyboard shows.
// Usage: Snap list
//        Snap <camera-name-substring> <output.jpg> [warmup-ms]
using Windows.Devices.Enumeration;
using Windows.Media.Capture;
using Windows.Media.MediaProperties;
using Windows.Storage;

var cameras = await DeviceInformation.FindAllAsync(DeviceClass.VideoCapture);
if (args.Length == 0 || args[0] == "list")
{
    foreach (var c in cameras) Console.WriteLine($"{c.Name} | enabled={c.IsEnabled}");
    return 0;
}
if (args.Length < 2) { Console.Error.WriteLine("usage: Snap <camera-name-substring> <output.jpg> [warmup-ms]"); return 2; }

var cam = cameras.FirstOrDefault(c => c.Name.Contains(args[0], StringComparison.OrdinalIgnoreCase));
if (cam is null) { Console.Error.WriteLine($"no camera matching '{args[0]}'"); return 3; }

var output = Path.GetFullPath(args[1]);
var warmup = args.Length > 2 ? int.Parse(args[2]) : 1500;   // let exposure settle before the shot

using var capture = new MediaCapture();
await capture.InitializeAsync(new MediaCaptureInitializationSettings
{
    VideoDeviceId = cam.Id,
    StreamingCaptureMode = StreamingCaptureMode.Video,
});
await Task.Delay(warmup);

Directory.CreateDirectory(Path.GetDirectoryName(output)!);
var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(output));
var file = await folder.CreateFileAsync(Path.GetFileName(output), CreationCollisionOption.ReplaceExisting);
await capture.CapturePhotoToStorageFileAsync(ImageEncodingProperties.CreateJpeg(), file);
Console.WriteLine(output);
return 0;
