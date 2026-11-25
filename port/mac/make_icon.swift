// Draws the macOS app icon set from the Windows icon's largest picture (package_full.py).
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The picture goes on Apple's rounded-square grid: an 824-point square with 185-point corners in the
// middle of a 1024-point canvas. macOS then shows it at the size of other app icons; a picture that
// fills its whole square is shrunk onto a plate of its own instead.
//   swift port/mac/make_icon.swift picture.png out.iconset
import AppKit

let arguments = CommandLine.arguments
guard arguments.count == 3, let picture = NSImage(contentsOfFile: arguments[1]) else {
  FileHandle.standardError.write("usage: make_icon.swift picture.png out.iconset\n".data(using: .utf8)!)
  exit(2)
}
let folder = URL(fileURLWithPath: arguments[2])
try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
let sizes = [("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64), ("128x128", 128),
             ("128x128@2x", 256), ("256x256", 256), ("256x256@2x", 512), ("512x512", 512), ("512x512@2x", 1024)]
for (name, pixels) in sizes {
  let image = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels, bitsPerSample: 8,
                               samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
                               bytesPerRow: 0, bitsPerPixel: 0)!
  NSGraphicsContext.saveGraphicsState()
  let context = NSGraphicsContext(bitmapImageRep: image)!
  NSGraphicsContext.current = context
  context.imageInterpolation = .high
  let scale = CGFloat(pixels) / 1024
  let square = NSRect(x: 100 * scale, y: 100 * scale, width: 824 * scale, height: 824 * scale)
  NSBezierPath(roundedRect: square, xRadius: 185 * scale, yRadius: 185 * scale).addClip()
  picture.draw(in: square, from: .zero, operation: .copy, fraction: 1)
  NSGraphicsContext.restoreGraphicsState()
  try image.representation(using: .png, properties: [:])!.write(to: folder.appendingPathComponent("icon_\(name).png"))
}
