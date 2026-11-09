// testcard.swift - erzeugt eine zweiseitige CR80-Testkarte als PDF.
//
//   swift tools/testcard.swift testkarte.pdf
//
// Vorderseite: Farbfelder, Graustufenkeil, feiner schwarzer Text, Pfeil "OBEN",
// Markierung oben links. Rueckseite: Pfeil "OBEN" und Text - damit laesst
// sich die Ausrichtung beim beidseitigen Druck pruefen.
//
// SPDX-License-Identifier: MIT
import CoreGraphics
import CoreText
import Foundation

let mm = 72.0 / 25.4
let width = 85.6 * mm, height = 53.98 * mm
guard CommandLine.arguments.count == 2 else {
    FileHandle.standardError.write("Aufruf: swift testcard.swift ausgabe.pdf\n".data(using: .utf8)!)
    exit(2)
}
let url = URL(fileURLWithPath: CommandLine.arguments[1]) as CFURL
var box = CGRect(x: 0, y: 0, width: width, height: height)
guard let ctx = CGContext(url, mediaBox: &box, nil) else { exit(1) }

func text(_ s: String, _ x: Double, _ y: Double, size: Double, bold: Bool = false, color: CGColor = CGColor(gray: 0, alpha: 1)) {
    let font = CTFontCreateWithName((bold ? "Helvetica-Bold" : "Helvetica") as CFString, size, nil)
    let attr = NSAttributedString(string: s, attributes: [
        NSAttributedString.Key(kCTFontAttributeName as String): font,
        NSAttributedString.Key(kCTForegroundColorAttributeName as String): color,
    ])
    let line = CTLineCreateWithAttributedString(attr)
    ctx.textPosition = CGPoint(x: x, y: y)
    CTLineDraw(line, ctx)
}

func arrowUp(_ cx: Double, _ cy: Double) {
    ctx.setFillColor(CGColor(gray: 0, alpha: 1))
    ctx.beginPath()
    ctx.move(to: CGPoint(x: cx, y: cy + 10 * mm))
    ctx.addLine(to: CGPoint(x: cx - 5 * mm, y: cy + 3 * mm))
    ctx.addLine(to: CGPoint(x: cx - 1.5 * mm, y: cy + 3 * mm))
    ctx.addLine(to: CGPoint(x: cx - 1.5 * mm, y: cy - 6 * mm))
    ctx.addLine(to: CGPoint(x: cx + 1.5 * mm, y: cy - 6 * mm))
    ctx.addLine(to: CGPoint(x: cx + 1.5 * mm, y: cy + 3 * mm))
    ctx.addLine(to: CGPoint(x: cx + 5 * mm, y: cy + 3 * mm))
    ctx.closePath()
    ctx.fillPath()
    text("OBEN", cx - 4.2 * mm, cy - 10 * mm, size: 7, bold: true)
}

// ---- Vorderseite ----
ctx.beginPDFPage(nil)
// Hintergrund-Verlauf bis zum Rand (randloser Druck)
let colors = [CGColor(red: 0.85, green: 0.93, blue: 1, alpha: 1), CGColor(red: 1, green: 1, blue: 1, alpha: 1)] as CFArray
let gradient = CGGradient(colorsSpace: CGColorSpace(name: CGColorSpace.sRGB), colors: colors, locations: [0, 1])!
ctx.drawLinearGradient(gradient, start: .zero, end: CGPoint(x: width, y: height), options: [])
// Farbfelder Y M C R G B K
let swatches: [(Double, Double, Double)] = [(1, 1, 0), (1, 0, 1), (0, 1, 1), (1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, 0)]
for (i, c) in swatches.enumerated() {
    ctx.setFillColor(CGColor(red: c.0, green: c.1, blue: c.2, alpha: 1))
    ctx.fill(CGRect(x: (4 + Double(i) * 7) * mm, y: 4 * mm, width: 6 * mm, height: 6 * mm))
}
// Graustufenkeil
for i in 0..<11 {
    ctx.setFillColor(CGColor(gray: Double(i) / 10, alpha: 1))
    ctx.fill(CGRect(x: (4 + Double(i) * 4.4) * mm, y: 12 * mm, width: 4.4 * mm, height: 4 * mm))
}
// Markierung oben links
ctx.setFillColor(CGColor(red: 1, green: 0, blue: 0, alpha: 1))
ctx.fill(CGRect(x: 2 * mm, y: height - 7 * mm, width: 5 * mm, height: 5 * mm))
text("Evolis Avansia", 9 * mm, height - 9 * mm, size: 13, bold: true)
text("Testkarte – Vorderseite", 9 * mm, height - 14.5 * mm, size: 9)
text("Feiner Text 6 pt: The quick brown fox jumps over the lazy dog 0123456789", 4 * mm, 20 * mm, size: 6)
text("Feiner Text 4 pt: Treiber für macOS – K-Panel-Schärfetest", 4 * mm, 23 * mm, size: 4)
// Rahmen 1 mm vom Kartenrand
ctx.setStrokeColor(CGColor(gray: 0, alpha: 1))
ctx.setLineWidth(0.3)
ctx.stroke(CGRect(x: 1 * mm, y: 1 * mm, width: width - 2 * mm, height: height - 2 * mm))
arrowUp(width - 12 * mm, height - 22 * mm)
ctx.endPDFPage()

// ---- Rueckseite ----
ctx.beginPDFPage(nil)
ctx.setFillColor(CGColor(red: 1, green: 0.95, blue: 0.85, alpha: 1))
ctx.fill(box)
ctx.setFillColor(CGColor(red: 0, green: 0, blue: 1, alpha: 1))
ctx.fill(CGRect(x: 2 * mm, y: height - 7 * mm, width: 5 * mm, height: 5 * mm))
text("Rückseite", 9 * mm, height - 9 * mm, size: 13, bold: true)
text("Der Pfeil muss in dieselbe Richtung zeigen wie vorne, wenn man", 4 * mm, 8 * mm, size: 6)
text("die Karte seitlich umdreht (Druck mit \"Kurze Kante\").", 4 * mm, 4.5 * mm, size: 6)
ctx.setStrokeColor(CGColor(gray: 0, alpha: 1))
ctx.setLineWidth(0.3)
ctx.stroke(CGRect(x: 1 * mm, y: 1 * mm, width: width - 2 * mm, height: height - 2 * mm))
arrowUp(width / 2, height / 2)
ctx.endPDFPage()
ctx.closePDF()
print("Testkarte geschrieben: \(CommandLine.arguments[1])")
