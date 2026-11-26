using System;
using System.Diagnostics;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Security.Cryptography;

public static class AppletWindow
{
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string className, string title);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string className, string title);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr window, uint message, IntPtr key, IntPtr flags);

    [DllImport("user32.dll")]
    public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    [DllImport("kernel32.dll")]
    public static extern void GetSystemTimePreciseAsFileTime(out long time);

    [StructLayout(LayoutKind.Sequential)]
    struct ProcessInfo
    {
        public IntPtr reserved, peb, reserved2, reserved3, id, parent;
    }

    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationProcess(IntPtr handle, int infoClass,
        out ProcessInfo info, int size, out int returned);

    public static int ParentId(Process process)
    {
        ProcessInfo info;
        int returned;
        return NtQueryInformationProcess(process.Handle, 0, out info, Marshal.SizeOf(typeof(ProcessInfo)),
                                         out returned) == 0 ? info.parent.ToInt32() : 0;
    }

    // Compare the first two lines of the 80x24 fzf preview with a plain-text control
    // rendered by the same candidate. Exclude the border, scroll indicator and file list.
    public static string PreviewSignature(string path)
    {
        using (var bitmap = new Bitmap(path))
        {
            int left = bitmap.Width * 52 / 100, right = bitmap.Width * 85 / 100;
            int top = bitmap.Height / 24, bottom = bitmap.Height * 3 / 24;
            int background = bitmap.GetPixel(left, top).ToArgb();
            var mask = new byte[(right - left) * (bottom - top)];
            int ink = 0, index = 0;
            for (int y = top; y < bottom; y++)
                for (int x = left; x < right; x++)
                {
                    if (bitmap.GetPixel(x, y).ToArgb() != background)
                    {
                        mask[index] = 1;
                        ink++;
                    }
                    index++;
                }
            if (ink < 20) return "";
            using (var hash = SHA256.Create())
                return Convert.ToBase64String(hash.ComputeHash(mask));
        }
    }
}
