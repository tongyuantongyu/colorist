# Basic Usage

```
Syntax: colorist convert  [input]        [output]       [OPTIONS]
        colorist identify [input]                       [OPTIONS]
        colorist generate                [output.icc]   [OPTIONS]
        colorist generate [image string] [output image] [OPTIONS]
Options:
    -a             : Enable automatic color grading of max luminance and gamma (disabled by default)
    -b BPP         : Output bits-per-pixel. 8, 16, or 0 for auto (default)
    -c COPYRIGHT   : ICC profile copyright string.
    -d DESCRIPTION : ICC profile description.
    -f FORMAT      : Output format. auto (default), icc, jp2, jpg, png, webp
    -g GAMMA       : Output gamma. 0 for auto (default), or "source" to force source gamma
    -h             : Display this help
    -j JOBS        : Number of jobs to use when color grading. 0 for as many as possible (default)
    -l LUMINANCE   : ICC profile max luminance. 0 for auto (default), or "source" to force source luminance
    -p PRIMARIES   : ICC profile primaries (8 floats, comma separated). rx,ry,gx,gy,bx,by,wx,wy
    -q QUALITY     : Output quality for JPG and WebP. JP2 can also use it (see -r below). (default: 90)
    -r RATE        : Output rate for JP2. If 0, JP2 codec uses -q value above instead. (default: 150)
    -t TONEMAP     : Set tonemapping. auto (default), on, or off
    -v             : Verbose mode.
    -z x,y,w,h     : Pixels to dump in identify mode. x,y,w,h
```

---

# Options

### -a

Enable automatic color grading. "Grading" is currently a bit overstated, but
the tool's name is "colorist" and it is close enough to one potential goal of
color grading, and leaves room for further enhancements. The general idea is
to choose an "optimal" (read: better) tone curve and max luminance during the
conversion process.

Turning this on and then specifying a luminance (`-l`) AND gamma (`-g`) will
make this a useless switch.

### -b BPP

Choose an output bit depth (8 or 16). By default, `convert` will try to use
the bit depth of the source image, and `generate` will choose a 16-bit image.
In either case, choosing an output file format incapable of 16-bit will
automatically force it to 8-bit.

### -c COPYRIGHT

Write a copyright into the copyright tag (`cprt`) of the ICC profile of any
output file generated. If not used, the copyright tag will default to whatever
[LittleCMS](http://www.littlecms.com/) uses for its default ("No copyright,
use freely").

### -d DESCRIPTION

Write a description into the description tag (`desc`) of the ICC profile of
any output file generated. If not used, Colorist will make one up based on the
contents of the ICC profile.

### -f FORMAT

Force a specific output file format. Most of the time this is not required as
Colorist will infer the format from the output file extension, but if you
wanted to choose a nonstandard output filename, this is the switch for you.

### -g GAMMA

Choose a specific gamma curve for the tone curves in the ICC profile (for all
channels). Similar to `-b`, `convert` will try to use the source image's gamma
by default, and `generate` will use a gamma of 2.4 as it is sRGB's gamma (very
common).

### -h

Show the help/syntax text shown in Basic Usage, and quit.

### -j JOBS

Choose the number of threads to spawn when performing any operation that has
been multithreaded, such as pixel transformations or automatic grading. By
default, Colorist chooses the number of cores available in the system. Running
`colorist -h` will show how many cores Colorist detects (and will use by
default) after displaying the syntax.

### -l LUMINANCE

Set a max luminance in the lumi tag of the ICC profile, and use this max in
any luminance scaling that needs to be performed. For example, if the source
image's max luminance claims to be 10,000 nits and you specify `-l 300` for
the output luminance, all pixels in the scene will have their luminance scaled
up and either clipped or tonemapped to 300 nits (see `-t`).

### -p PRIMARIES

Sets the color primaries for the output ICC profile, in the form
`rx,ry,gx,gy,bx,by,wx,wy`. These values are readily available for any RGB
color space (ex. [Rec
709](https://en.wikipedia.org/wiki/Rec._709#Primary_chromaticities)'s color
space parameters). Specifying this in `convert` will almost certainly cause a
color space conversion to occur.

There are a handful of builtin primaries for convenience:

* `bt709`  - [BT. 709](https://en.wikipedia.org/wiki/Rec._709#Primary_chromaticities)
* `bt2020` - [BT. 2020](https://en.wikipedia.org/wiki/Rec._2020#System_colorimetry)
* `p3`     - [DCI-P3](https://en.wikipedia.org/wiki/DCI-P3#System_colorimetry)

### -q QUALITY

Choose a lossy quality (0-100) for any output file format that supports it
(JPG, JP2 if not using `-r`, WebP). The lower the value, the lower the file
size and quality. For WebP and JP2 (without `-r`), 100 is lossless.

### -r RATE

Choose a "rate" for JP2 output compression. This effectively puts a hard
ceiling on the size of the output JP2 file, and can be treated as a divisor on
the input source data. For example, a 3840x2160, 16-bit image is (3840 \* 2160
\* 8 == 66,355,200) bytes in size (~63 MB). Specifying `-r 200` will make the
output file size roughly (66,355,200 / 200 = 331776) bytes, or 324 KB.

The JP2 compressor will do everything it can to blur/ruin your image if you
don't give it enough breathing room, but it *will* hit your requested rate.
Also, be sure to experiment to make sure my math isn't really wrong.

### -t TONEMAP

Forces tonemapping to be on or off. When scaling from a large luminance range
down to a smaller range, any values that are too bright will not "fit" in the
new range. With tonemapping disabled, those pixels will simply be clipped to
the max value, but any pixels that do fit in the smaller range will be
untouched. With tonemapping enabled, the entire image is subtly darkened to
make room for a a bit of extra granularity in the top of the smaller range,
which is used to distinguish super bright pixels. This spares having large
white blobs of no definition from being in the converted image at the cost
of a bit of darkening.

Automatic grading will automatically turn this off if you allow it to choose a
max luminance, as it will never choose a luminance that will cause a pixel to
clip. Use this switch (`-t off`) to achieve this with a manually specified max
luminance.

### -v

Verbose mode. Ironically, that's it for this one.

### -z x,y,w,h

When using `identify`, it will dump the basic information about the image such
as the dimensions, bit depth, and embedded ICC profile. By default, it also
dumps the colors of a handful of pixels from the upper left corner. If you
want to choose an alternate rectangle for that pixel information, use this.
Choosing a width and height of 0 will disable pixel dumping during identify
(`-z 0,0,0,0`).

---

# Image Strings

The `generate` command offers a means to create basic test images, using an
"image string" as its input. The general idea of an image string is to
describe a big list of colors along with (optional) image dimensions, and to
spread those colors across the image as evenly as possible. The goal is to be
able to make **simple test images** as *tersely* as possible, but if you're
stubborn enough, you can probably make almost anything.

As a basic reminder, this is the form of the command:

`colorist generate "#ff0000" image.png`

In the above commandline `"#ff0000"` is the image string. Whether or not the
quotes are necessary is up to your shell, but it good practice to use them. It
specifies a single color in the typical web color format used in CSS, one of
many ways to specify a color. Since this is the only information in the image
string, Colorist will count a total of 1 color in the color list, and will
select a 1x1 16-bit image to house this single color. Any of the other options
specified above will inform these decisions, such as making an 8-bit image
with `-b 8`.

Let's add some more information:

`colorist generate "128x128,#ff0000" image.png`

Colorist will count a total of 1 color in the color list, and instead of
choosing a 1x1 image to spread the colors across, it'll honor the 128x128
dimensions request, so it'll just fill the whole image with red.

*Quick tips:*
* A fourth hex pair is allowed to specify an alpha channel (such as `#ff000080` for a half- transparent red).
* The dimensions request can be anywhere in the comma separated list.

How about a second color?

`colorist generate "128x128,#ff0000,#00ff00" image.png`

This creates the same 128x128 image, but it shares the pixels evenly across
the two colors (red for the first half, then green). Reordering or adding
colors will continue the pattern.

What if I wanted 75% red and 25% green?

`colorist generate "128x128,#ff0000,x3,#00ff00" image.png`

The `x3` in there says to pretend that the previous statement was listed three
times in a row. Colorist will then count a total of *four* colors, and will
spread those four colors across the requested image size.

How about a gradient?

`colorist generate "128x128,#ff0000..#000000" image.png`

The `..` syntax creates an interpolation (gradient) between the two colors.
Colorist will count 256 images in the color list, and since there are more
than 256 pixels in the requested image, it will spread those evenly across the
image, creating the gradient.

How about two gradients?

`colorist generate "128x128,#ff0000..#000000,#000000..#00ff00" image.png`

Colorist counts 512 colors here, and spreads them accordingly. What if I
wanted my red gradient to take up 75%, and the green one only 25%? Let's try
it.

`colorist generate "128x128,#ff0000..#000000,x3,#000000..#00ff00" image.png`

Oops! `x3` is making 3 red-to-black gradients, followed by 1 red-to-black
gradient. I didn't want to *repeat* my gradient, I wanted to *stretch* my
gradient. To be more precise, I wanted each color entry in the gradient itself
to repeat, or "stutter": (111222333) vs (123123123). We can achieve this
by putting a *stutter* in between our `..`, such as:

`colorist generate "128x128,#ff0000.3.#000000,#000000..#00ff00" image.png`

There we go! Remember, the whole point here is evenly spreading a big list of
colors across an image, top to bottom. By choosing to stutter the range
instead of repeat the whole thing, we create an entirely different effect.

How about a horizontal gradient? This is where basic rotation comes into play.

`colorist generate "128x128,#ff0000..#000000,ccw" image.png`

This rotates the image counter-clockwise once after generating it. You can use
multiple `cw` or `ccw` statements if you want, but ultimately their rotations
will all be summed up and potentially cancelled out, and a single rotation
will be performed at the end.

How about some color squares for testing?

`colorist generate "100x400,#ff0000,#00ff00,#0000ff,#ffffff,ccw" image.png`

Note that the final image generated here is actually 400x100! This is due to
our rotation at the end. A lot of the trick with using image strings is
describing a color count that corresponds evenly with the pixel count of the
requested image dimension. Try to make the color list's count to be a factor
of the pixel count for best results.

There are many ways to define a color. For example, simply parenthesizing
some numbers works!

`colorist generate "128x128,(255,0,0),(0,255,0)" image.png`

This should recreate our red/green image from earlier. How about a gradient?

`colorist generate "128x128,(255,0,0)..(0,0,0)" image.png`

Another red/green version:

`colorist generate "128x128,rgb(255,0,0)..#000000" image.png`

or

`colorist generate "128x128,rgb16(65535,0,0)..#000000" image.png`

All possible color formats:

```
#ffffff                               // 8-bit
(255,0,0)                             // 8-bit
rgb(255,0,0)                          // 8-bit
rgba(255,0,0,255)                     // 8-bit
rgb16(65535,0,0)                      // 16-bit
rgba16(65535,0,0,65535)               // 8-bit
f(1.0,0,0)                            // 32-bit, can't be used in a gradient
float(1.0,0,0)                        // 32-bit, can't be used in a gradient
XYZ(0.385127, 0.716909, 0.0970615)    // 32-bit, can't be used in a gradient
xyY(0.321181, 0.597874, 0.716909)     // 32-bit, can't be used in a gradient
```

---

Still want more? Read the [Cookbook](./docs/Cookbook.md)!