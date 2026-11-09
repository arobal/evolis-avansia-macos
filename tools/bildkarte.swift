// bildkarte.swift - beidseitige CR80-Bildkarte (Hochformat) als PDF.
//
//   swift tools/bildkarte.swift BILD ausgabe.pdf ["Titel"]
//
// Vorderseite: Bild randlos (formatfuellend) mit Textband oben.
// Rueckseite:  Bild verkleinert, Titel, Text und ein Pfeil "OBEN"
//              zur Kontrolle der Ausrichtung.
//
// SPDX-License-Identifier: MIT
import CoreGraphics
import CoreText
import Foundation
import ImageIO

let mm = 72.0 / 25.4
let width = 53.98 * mm, height = 85.6 * mm
let args = CommandLine.arguments
guard args.count >= 3 else {
    FileHandle.standardError.write("Aufruf: swift bildkarte.swift BILD ausgabe.pdf [\"Titel\"]\n".data(using: .utf8)!)
    exit(2)
}
let title = args.count > 3 ? args[3] : "Meine Karte"
guard let src = CGImageSourceCreateWithURL(URL(fileURLWithPath: args[1]) as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(src, 0, nil) else {
    FileHandle.standardError.write("Bild nicht lesbar: \(args[1])\n".data(using: .utf8)!)
    exit(1)
}
var box = CGRect(x: 0, y: 0, width: width, height: height)
guard let ctx = CGContext(URL(fileURLWithPath: args[2]) as CFURL, mediaBox: &box, nil) else { exit(1) }

func text(_ s: String, _ x: Double, _ y: Double, size: Double, bold: Bool = false,
          color: CGColor = CGColor(gray: 0, alpha: 1), centered: Bool = false) {
    let font = CTFontCreateWithName((bold ? "Helvetica-Bold" : "Helvetica") as CFString, size, nil)
    let attr = NSAttributedString(string: s, attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): color,
    ])
    let line = CTLineCreateWithAttributedString(attr)
    let w = CTLineGetTypographicBounds(line, nil, nil, nil)
    ctx.textPosition = CGPoint(x: centered ? x - w / 2 : x, y: y)
    CTLineDraw(line, ctx)
}

/// Bild formatfuellend (ggf. beschnitten) in rect zeichnen
func drawCover(_ rect: CGRect) {
    let iw = Double(image.width), ih = Double(image.height)
    let s = max(rect.width / iw, rect.height / ih)
    let w = iw * s, h = ih * s
    ctx.saveGState()
    ctx.clip(to: rect)
    ctx.draw(image, in: CGRect(x: rect.midX - w / 2, y: rect.midY - h / 2, width: w, height: h))
    ctx.restoreGState()
}

let white = CGColor(gray: 1, alpha: 1)
let dark = CGColor(red: 0.25, green: 0.12, blue: 0.05, alpha: 1)

// ---- Vorderseite: Bild randlos + Textband ----
ctx.beginPDFPage(nil)
drawCover(box)
ctx.setFillColor(CGColor(gray: 0, alpha: 0.55))
ctx.fill(CGRect(x: 0, y: height - 7 * mm, width: width, height: 7 * mm))
text("Evolis Avansia · Vorderseite", width / 2, height - 4.8 * mm, size: 7, bold: true, color: white, centered: true)
ctx.endPDFPage()

// ---- Rueckseite: Bild klein + Text ----
ctx.beginPDFPage(nil)
ctx.setFillColor(CGColor(red: 1, green: 0.96, blue: 0.88, alpha: 1))
ctx.fill(box)
ctx.setFillColor(CGColor(red: 0.85, green: 0.45, blue: 0.1, alpha: 1))
ctx.fill(CGRect(x: 0, y: height - 4 * mm, width: width, height: 4 * mm))
let thumb = CGRect(x: (width - 30 * mm) / 2, y: height - 50 * mm, width: 30 * mm, height: 44 * mm)
drawCover(thumb)
ctx.setStrokeColor(dark)
ctx.setLineWidth(0.5)
ctx.stroke(thumb)
// Pfeil "OBEN" oben rechts
ctx.setFillColor(CGColor(gray: 0, alpha: 1))
ctx.beginPath()
let ax = width - 5 * mm, ay = height - 12 * mm
ctx.move(to: CGPoint(x: ax, y: ay + 5 * mm))
ctx.addLine(to: CGPoint(x: ax - 2.5 * mm, y: ay + 1.5 * mm))
ctx.addLine(to: CGPoint(x: ax - 0.8 * mm, y: ay + 1.5 * mm))
ctx.addLine(to: CGPoint(x: ax - 0.8 * mm, y: ay - 2.5 * mm))
ctx.addLine(to: CGPoint(x: ax + 0.8 * mm, y: ay - 2.5 * mm))
ctx.addLine(to: CGPoint(x: ax + 0.8 * mm, y: ay + 1.5 * mm))
ctx.addLine(to: CGPoint(x: ax + 2.5 * mm, y: ay + 1.5 * mm))
ctx.closePath()
ctx.fillPath()
text("OBEN", ax, ay - 5 * mm, size: 4.5, bold: true, centered: true)

text(title, width / 2, height - 57 * mm, size: 10, bold: true, color: dark, centered: true)
text("Rückseite – Bild und Text", width / 2, height - 62 * mm, size: 6.5, centered: true)
let lines = ["Gedruckt mit dem inoffiziellen", "macOS-Treiber für die Evolis Avansia.",
             "Beidseitig, YMCK, 600 dpi."]
for (i, l) in lines.enumerated() {
    text(l, width / 2, height - (68 + Double(i) * 3.4) * mm, size: 5.5, centered: true)
}
let df = DateFormatter(); df.dateFormat = "dd.MM.yyyy"
text(df.string(from: Date()), width / 2, 4 * mm, size: 5, color: CGColor(gray: 0.3, alpha: 1), centered: true)
ctx.endPDFPage()
ctx.closePDF()
print("Bildkarte geschrieben: \(args[2])")
