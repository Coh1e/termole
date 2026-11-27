using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;

// All images are crops of fzf's right preview on the same 111 x 32 light-theme grid.
public sealed class DesktopPreviewPixels
{
    public sealed class Measurement
    {
        public string State;
        public int DifferentPixels, TailInk, FromGlossary, FromLicense;
    }

    readonly int width, height;
    readonly int[] shortImage, glossary, license;
    readonly Rectangle heading;
    readonly Rectangle tail;

    static int[] ReadRegion(Bitmap image, Rectangle region)
    {
        var bits = image.LockBits(region, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var pixels = new int[region.Width * region.Height];
        try
        {
            for (int y = 0; y < region.Height; ++y)
                Marshal.Copy(IntPtr.Add(bits.Scan0, y * bits.Stride), pixels, y * region.Width, region.Width);
        }
        finally { image.UnlockBits(bits); }
        for (int i = 0; i < pixels.Length; ++i) pixels[i] &= 0xffffff;
        return pixels;
    }

    int[] Read(string path)
    {
        using (var image = new Bitmap(path))
        {
            if (image.Width != width || image.Height != height)
                throw new InvalidDataException("Preview crop geometry changed: " + path);
            return ReadRegion(image, new Rectangle(0, 0, width, height));
        }
    }

    // A transient system overlay can cover the desktop without covering PrintWindow's
    // child View. Use the collector's physical View dimensions and crop coordinates.
    public static int ReferenceDifferences(string viewPng, string dxgiBmp, int left, int top,
                                           int viewWidth, int viewHeight, int cropWidth, int cropHeight)
    {
        using (var view = new Bitmap(viewPng))
        using (var desktop = new Bitmap(dxgiBmp))
        {
            if (view.Width != viewWidth || view.Height != viewHeight ||
                desktop.Width != cropWidth || desktop.Height != cropHeight || left < 0 || top < 0 ||
                left > view.Width - desktop.Width || top > view.Height - desktop.Height)
                throw new InvalidDataException("The DXGI reference crop does not fit the captured View.");
            var expected = ReadRegion(view, new Rectangle(left, top, desktop.Width, desktop.Height));
            var actual = ReadRegion(desktop, new Rectangle(0, 0, desktop.Width, desktop.Height));
            int differences = 0;
            for (int i = 0; i < expected.Length; ++i)
                if (expected[i] != actual[i]) ++differences;
            return differences;
        }
    }

    static bool Dark(int rgb)
    {
        return (rgb >> 16) < 146 && ((rgb >> 8) & 255) < 146 && (rgb & 255) < 146;
    }

    static bool Light(int rgb)
    {
        return (rgb >> 16) >= 200 && ((rgb >> 8) & 255) >= 200 && (rgb & 255) >= 200;
    }

    public DesktopPreviewPixels(string shortPath, string glossaryPath, string licensePath)
    {
        using (var image = new Bitmap(shortPath)) { width = image.Width; height = image.Height; }
        shortImage = Read(shortPath);
        glossary = Read(glossaryPath);
        license = Read(licensePath);
        // First text row inside the border. The page count is in the upper right.
        heading = Rectangle.FromLTRB(width / 100, height / 32, width * 4 / 5, height * 2 / 32);
        tail = FindTail();
        ValidateShort();
        Measurement g = Measure(glossary), l = Measure(license);
        if (g.State != "GLOSSARY" || l.State != "LICENSE" || g.FromGlossary == 0 || l.FromLicense == 0)
            throw new InvalidDataException("The two independent long references need distinct content and tail ink.");
    }

    // fzf can leave an extra terminal row below the preview. Locate its actual
    // closing rule in all three controls instead of inferring it from the crop edge.
    bool ClosingRule(int y)
    {
        int start = width / 10, end = width - start, shared = 0;
        for (int x = start; x < end; ++x)
        {
            int i = y * width + x;
            if (Dark(shortImage[i]) && Dark(glossary[i]) && Dark(license[i])) ++shared;
        }
        return shared >= (end - start) * 0.95;
    }

    Rectangle FindTail()
    {
        int bottom = height - 1;
        while (bottom > height / 2 && !ClosingRule(bottom)) --bottom;
        if (bottom <= height / 2) throw new InvalidDataException("Missing preview bottom border.");
        while (bottom > heading.Bottom && ClosingRule(bottom - 1)) --bottom;
        // Just above the rule, its right-hand vertical stroke is the last ink.
        int right = width - 1;
        int row = (bottom - 1) * width;
        while (right > width * 4 / 5 && !Dark(shortImage[row + right])) --right;
        while (right > width * 4 / 5 && Dark(shortImage[row + right])) --right;
        if (right <= width * 4 / 5) throw new InvalidDataException("Missing preview right border.");
        return Rectangle.FromLTRB(heading.Left, heading.Bottom, right + 1, bottom);
    }

    void ValidateShort()
    {
        const string sentence = "The lighthouse turns amber at noon.";
        var occupied = new bool[heading.Right];
        for (int y = heading.Top; y < heading.Bottom; ++y)
            for (int x = heading.Left; x < heading.Right; ++x)
                occupied[x] |= Dark(shortImage[y * width + x]);

        // Measure the monospace pitch between the starts of 'The' and 'noon.'.
        // The five word spaces also distinguish this row from a blank or wrapped reference.
        var starts = new List<int>();
        int last = -width;
        for (int x = heading.Left; x < heading.Right; ++x)
        {
            if (!occupied[x]) continue;
            if (x - last > width / 75.0) starts.Add(x);
            last = x;
        }
        if (starts.Count != 6) throw new InvalidDataException("Incomplete short reference: six words required.");
        double pitch = (starts[5] - starts[0]) / (double)sentence.LastIndexOf("noon.", StringComparison.Ordinal);
        int[] wordColumns = { 0, 4, 15, 21, 27, 30 };
        for (int i = 0; i < starts.Count; ++i)
            if (Math.Abs(starts[i] - starts[0] - wordColumns[i] * pitch) > pitch / 2)
                throw new InvalidDataException("Short reference word positions do not match the fixture.");
        // Check the visible 'n' and '.' separately; two equally clipped images cannot certify each other.
        int[] glyphHeight = { 0, 0 };
        for (int glyph = 0; glyph < 2; ++glyph)
        {
            int left = (int)Math.Round(starts[0] + (sentence.Length - 2 + glyph) * pitch);
            int right = (int)Math.Round(left + pitch * 0.92);
            if (right > heading.Right) throw new InvalidDataException("Clipped short reference.");
            for (int y = heading.Top; y < heading.Bottom; ++y)
            {
                for (int x = left; x < right; ++x)
                    if (Dark(shortImage[y * width + x])) { ++glyphHeight[glyph]; break; }
            }
        }
        if (glyphHeight[1] == 0 || glyphHeight[0] < glyphHeight[1] * 2)
            throw new InvalidDataException("Short reference is missing its final letter or period.");
        for (int y = tail.Top; y < tail.Bottom; ++y)
            for (int x = tail.Left; x < tail.Right; ++x)
                if (!Light(shortImage[y * width + x]))
                    throw new InvalidDataException("The independent short reference has a nonblank tail.");
    }

    public Measurement Compare(string path) { return Measure(Read(path)); }

    Measurement Measure(int[] actual)
    {
        var result = new Measurement();
        bool glossaryBody = true, licenseBody = true, shortTitle = true, black = true;
        bool shortOverGlossary = true, shortOverLicense = true, newShortInk = false;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                int i = y * width + x, color = actual[i];
                black &= color == 0;
                bool matchesShort = color == shortImage[i];
                if (!matchesShort) ++result.DifferentPixels;
                if (heading.Contains(x, y))
                {
                    shortTitle &= matchesShort;
                    // A partial update can replace just a few heading glyphs. Attribute it only
                    // when every heading pixel is explained by the short text and the prior title.
                    shortOverGlossary &= matchesShort || color == glossary[i];
                    shortOverLicense &= matchesShort || color == license[i];
                    newShortInk |= matchesShort && Dark(color) && color != glossary[i] && color != license[i];
                }
                // A long preview can change its scroll indicator or page count without
                // changing its text. The clean short frame is compared across the entire crop.
                bool chrome = x >= width * 49 / 50 ||
                    (x >= width * 4 / 5 && (y < height * 3 / 32 || y >= tail.Bottom - height * 2 / 32));
                if (!chrome)
                {
                    glossaryBody &= color == glossary[i];
                    licenseBody &= color == license[i];
                }
                if (tail.Contains(x, y) && Dark(color) && Light(shortImage[i]))
                {
                    ++result.TailInk;
                    if (color == glossary[i]) ++result.FromGlossary;
                    if (color == license[i]) ++result.FromLicense;
                }
            }
        }
        if (result.DifferentPixels == 0) result.State = "short";
        else if (black) result.State = "black";
        else if (glossaryBody) result.State = "GLOSSARY";
        else if (licenseBody) result.State = "LICENSE";
        else if (shortTitle || (newShortInk && (shortOverGlossary || shortOverLicense)))
        {
            bool residue = ((shortTitle || shortOverGlossary) && result.FromGlossary > 0) ||
                ((shortTitle || shortOverLicense) && result.FromLicense > 0);
            result.State = residue ? "mixed" : "short-with-differences";
        }
        else result.State = "unknown";
        return result;
    }
}
