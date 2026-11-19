// Modal's light-theme accents keep the printed page quiet and the keys distinct.
#set text(fill: rgb("#292e42"))
#set par(justify: false, leading: 0.7em)
#show heading.where(level: 2): set text(fill: rgb("#3760bf"))
#show heading.where(level: 3): set text(fill: rgb("#46652b"))
#show raw.where(block: false): it => box(
  fill: rgb("#edf0f7"),
  inset: (x: 3pt, y: 1pt),
  radius: 2pt,
  text(font: "JetBrains Mono NL", size: 0.85em, fill: rgb("#543780"), it),
)
// Highlighted code marks line endings with raw newlines; they must not become boxed keys.
#show raw.where(block: false, text: "\n"): linebreak()
#set table(inset: 7pt)
