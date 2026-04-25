export interface AccentRgb { r: number; g: number; b: number }

export function applyAccentVars({ r, g, b }: AccentRgb): void {
  const style = document.documentElement.style
  // Raw accent — used only for rgba() transparent backgrounds
  style.setProperty('--accent-rgb', `${r}, ${g}, ${b}`)
  // Soft tint (20% toward white) — used for solid fills on dark backgrounds, matching Windows UI style
  const [sr, sg, sb] = lightenRgb(r, g, b, 0.20)
  style.setProperty('--accent-soft-rgb', `${sr}, ${sg}, ${sb}`)
  // Light tint (40% toward white) — used for SVG icon strokes inside colored circles
  style.setProperty('--accent-light', `#${hex(Math.round(r + (255 - r) * 0.40))}${hex(Math.round(g + (255 - g) * 0.40))}${hex(Math.round(b + (255 - b) * 0.40))}`)
}

function lightenRgb(r: number, g: number, b: number, t: number): [number, number, number] {
  return [Math.round(r + (255 - r) * t), Math.round(g + (255 - g) * t), Math.round(b + (255 - b) * t)]
}

function hex(n: number): string { return n.toString(16).padStart(2, '0') }
