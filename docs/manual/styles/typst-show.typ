// A compact reading edition: shared Quarto content, with a local cover and chapter treatment.
// No remote templates or fonts are fetched during publication.
#let appendices(title, hide-parent: false, body) = {
  state("appendix-state", none).update(true)
  counter(heading).update(0)
  set heading(numbering: "A.1", supplement: [Appendix])
  body
}

#show: body => {
  set text(font: "Libertinus Serif", size: 11pt, fill: rgb("#292e42"), lang: "en", region: "GB")
  set par(justify: false, first-line-indent: 0pt, leading: 0.6em, spacing: 0.7em)
  set page(
    paper: "a4",
    margin: (x: 22mm, y: 23mm),
    header: context {
      if counter(page).get().first() > 2 {
        text(size: 8pt, fill: rgb("#565f89"))[Modal · User manual]
        line(length: 100%, stroke: 0.4pt + rgb("#c4c8da"))
      }
    },
    footer: context align(right, text(size: 9pt, counter(page).display("1"))),
  )
  set heading(numbering: "1.1", supplement: [Chapter])
  let figure-number = n => numbering(
    if state("appendix-state", none).get() != none { "A.1" } else { "1.1" },
    counter(heading).get().first(), n,
  )
  show figure.where(kind: "quarto-float-fig"): set figure(
    numbering: figure-number,
  )
  show figure.where(kind: "quarto-float-tbl"): set figure(
    numbering: figure-number,
  )
  show figure.where(kind: "quarto-float-tbl"): set block(breakable: true)
  set table(align: left)
  show figure.caption: set text(size: 9pt)
  show heading.where(level: 1): it => {
    pagebreak(weak: true)
    block(width: 100%, above: 0pt, below: 18pt, inset: (top: 12pt),
      stroke: (top: 3pt + brand-color.primary))[
      #text(size: 25pt, weight: "bold")[
        #if it.numbering != none { counter(heading).display(); h(8pt) }
        #it.body
      ]
    ]
  }
  // The title page uses Modal's dark panels, blue mode accent and amber cursor colour.
  page(header: none, footer: none)[
    #v(28mm)
    #block(width: 100%, fill: rgb("#1a1b26"), inset: 24pt)[
      #text(font: "JetBrains Mono NL", size: 11pt, fill: rgb("#7aa2f7"))[MIDI SEQUENCING FROM THE KEYBOARD]
      #v(18mm)
      #text(font: "JetBrains Mono NL", size: 48pt, weight: "bold", fill: rgb("#c0caf5"))[$title$]
      #v(6mm)
      #text(size: 21pt, fill: rgb("#e0af68"))[User manual]
      #v(15mm)
      #line(length: 24%, stroke: 4pt + rgb("#9ece6a"))
      #h(3mm)
      #line(length: 14%, stroke: 4pt + rgb("#bb9af7"))
      #h(3mm)
      #line(length: 34%, stroke: 4pt + rgb("#7aa2f7"))
    ]
    #v(1fr)
    #text(size: 15pt)[Initial draft]
    #par[16 November 2026]
    #text(size: 10pt, fill: rgb("#565f89"))[The Modal project]
  ]
  outline(title: [Contents], depth: 2)
  body
}
