# Spark — Branding & Design System

This document is the single source of truth for the visual identity of the Spark blind dating website. All UI changes must stay consistent with the rules defined here.

---

## Brand Identity

| Property | Value |
|---|---|
| **Product name** | Spark |
| **Tagline** | *Find your match beyond appearances* |
| **Logo mark** | `✦` (Unicode U+2726, Black Four Pointed Star) |
| **Logo format** | `✦ Spark` — mark in `--pink`, word in `--text` |
| **Voice** | Warm, modern, trustworthy — never cheesy or overly romantic |

---

## Color Palette

All colors are defined as CSS custom properties on `:root`. Always reference the variable — never hardcode the hex value directly.

### Core tokens

| Token | Hex | Usage |
|---|---|---|
| `--pink` | `#ff4d7d` | Primary accent — CTAs, highlights, stat values, logo mark |
| `--rose` | `#ff8fa3` | Secondary pink — badges, lighter accents |
| `--purple` | `#8b5cf6` | Secondary accent — gradients, Blossom algorithm |
| `--dark` | `#1a0a2e` | Page background |
| `--card` | `#2d1b4e` | Card / modal / nav background |
| `--glass` | `rgba(255,255,255,0.06)` | Subtle surface on top of `--card` (stat boxes, pair rows) |
| `--border` | `rgba(255,255,255,0.12)` | All borders and dividers |
| `--text` | `#f0e6ff` | Primary body text |
| `--muted` | `rgba(240,230,255,0.55)` | Secondary / helper text, placeholders |

### Semantic colors (not tokens — use inline where needed)

| Purpose | Value | Usage |
|---|---|---|
| Success / high score | `#6ee7b7` (text) · `rgba(16,185,129,0.1)` (bg) | Score ≥ 80, guarantee banners |
| Warning / mid score | `#fcd34d` (text) · `rgba(245,158,11,0.15)` (bg) | Score 65–79 |
| Danger / low score | `#fca5a5` (text) · `rgba(239,68,68,0.15)` (bg) | Score < 65, errors |

### Background orbs

Two fixed radial-gradient pseudo-elements create the ambient glow on `body`. Do not remove them.

- Top-left orb: `rgba(255,77,125,0.18)` — 600 × 600 px, offset `-200 -200`
- Bottom-right orb: `rgba(139,92,246,0.15)` — 500 × 500 px, offset `-150 -150` from bottom-right

### Algorithm accent colors

Each algorithm has a dedicated two-stop gradient defined as `--accent-a` / `--accent-b` on its card class:

| Algorithm | Class | `--accent-a` | `--accent-b` |
|---|---|---|---|
| Gale-Shapley | `.gs-card` | `#ff4d7d` | `#ff8fa3` |
| Hopcroft-Karp | `.hk-card` | `#f59e0b` | `#fbbf24` |
| Hungarian | `.hu-card` | `#10b981` | `#34d399` |
| Blossom | `.bl-card` | `#8b5cf6` | `#a78bfa` |

When building algorithm-related UI elements, use that algorithm's accent pair — not the global `--pink`/`--purple`.

---

## Typography

| Property | Value |
|---|---|
| **Font stack** | `'Segoe UI', system-ui, sans-serif` |
| **Base color** | `--text` |
| **Secondary color** | `--muted` |

### Scale

| Role | Size | Weight | Notes |
|---|---|---|---|
| Hero heading | `clamp(2.4rem, 5vw, 4rem)` | 800 | Line-height 1.1 |
| Section heading | `1.9rem` | 700 | |
| Card heading | `1rem` | 700 | |
| Modal title | `1.15rem` | 700 | |
| Body / description | `0.9rem` | 400 | Line-height 1.5 |
| Card body / helper | `0.83rem` | 400 | Line-height 1.5, `--muted` |
| Badge / label | `0.72–0.8rem` | 600 | Letter-spacing 0.5–1px |
| Stat value | `1.6rem` | 800 | `--pink` |
| Stat label | `0.75rem` | 400 | `--muted` |
| Footer | `0.82rem` | 400 | `--muted` |

**Accented text in headings:** use `<em>` (styled `font-style: normal; color: var(--pink)`) for the highlighted word in hero h1.

---

## Spacing & Layout

| Token | Value | Usage |
|---|---|---|
| `--radius` | `16px` | Standard card border-radius |
| Modal radius | `24px` | Slightly larger for the modal popup |
| Avatar radius | `50%` | Circular |
| Icon/badge radius | `12px` | Algo icon tiles |
| Pill radius | `20px` | All badge/pill elements |
| Page max-width | `1100px` | `.container` |
| Page horizontal padding | `24px` | `.container`, nav |
| Section vertical padding | `60px` top & bottom | `.section` |
| Card padding | `28px 24px` | Algo cards |
| Card padding (profile) | `24px 20px` | Profile cards |
| Modal body padding | `20px 28px 28px` | |
| Grid gap (algo) | `18px` | |
| Grid gap (profile) | `16px` | |
| Grid gap (stats) | `12px` | |

---

## Component Patterns

### Buttons

| Variant | Class | Style |
|---|---|---|
| Primary | `.btn.btn-primary` | `linear-gradient(135deg, --pink, --purple)`, white text, shadow `0 4px 20px rgba(255,77,125,0.35)`, lifts 2px on hover |
| Ghost | `.btn.btn-ghost` | `--glass` bg, `--border` border, lifts on hover to `rgba(255,255,255,0.1)` |
| Card run | `.btn-run` | Full-width, uses algorithm's `--accent-a`/`--accent-b` gradient, 10px radius |
| Nav CTA | `.btn-login` | Same gradient as `.btn-primary`, applied to a nav `<a>` |

All buttons use `transition: all 0.2s`, `border-radius: 12px`, `font-weight: 600`, `font-size: 0.95rem`. The `.btn-run` uses `border-radius: 10px` to stay proportional inside the card.

**Disabled state:** `opacity: 0.5; cursor: wait;`

**Loading state:** Replace button text with `<span class="spinner"></span> Running…`

### Cards

All cards share:
- `background: var(--card)`
- `border: 1px solid var(--border)`
- `border-radius: var(--radius)` (16px)
- `transition: all 0.2s`
- Hover: `transform: translateY(-3px)` and `border-color: rgba(255,77,125,0.3)`

Algorithm cards additionally have an invisible overlay (`::before`) that fades in at `opacity: 0.07` on hover using the algorithm's own accent gradient. Hover lift is `-4px` (slightly more dramatic than profile cards at `-3px`).

### Badges & Pills

- Background: `rgba(255,255,255,0.08)`, border: `1px solid var(--border)`, color: `--muted`
- For colored/semantic badges: set `background`, `border`, and `color` inline using the relevant semantic or algorithm color

### Modal

- Overlay: `rgba(0,0,0,0.7)` + `backdrop-filter: blur(6px)`, clicking overlay dismisses
- Panel: `var(--card)` bg, `24px` radius, max-width `680px`, max-height `85vh`
- Entry animation: `slideUp` — `translateY(30px) → 0` + `opacity 0 → 1` over `0.25s`
- Close button: circular, `32px`, uses `--glass` bg, turns `rgba(255,77,125,0.2)` on hover
- Escape key also dismisses

### Score Pills (pair list)

| Range | Class | Text color | Background |
|---|---|---|---|
| ≥ 80 | `.score-high` | `#6ee7b7` | `rgba(16,185,129,0.15)` |
| 65–79 | `.score-mid` | `#fcd34d` | `rgba(245,158,11,0.15)` |
| < 65 | `.score-low` | `#fca5a5` | `rgba(239,68,68,0.15)` |

### Spinner

```html
<span class="spinner"></span>
```
18×18 px circle, `border-top-color: #fff`, 0.7s linear `spin` animation.

### Guarantee banner

Green tinted row inside modal:
- `background: rgba(16,185,129,0.1)`, `border: 1px solid rgba(16,185,129,0.25)`
- `border-radius: 10px`, `color: #6ee7b7`
- Prefix with `✓`

---

## Navigation

- Sticky, `z-index: 100`, blurred background `rgba(26,10,46,0.7)` + `backdrop-filter: blur(8px)`
- Bottom border: `1px solid var(--border)`
- Logo left, nav links right
- Nav links: `--muted` color, pill hover (`--glass` bg)
- Sign In link always uses the primary gradient (`.btn-login`)

---

## Iconography

Use emoji as icons throughout — they render consistently across platforms with no extra dependency.

| Context | Icon |
|---|---|
| Logo mark | ✦ |
| Gale-Shapley | ⚖️ |
| Hopcroft-Karp | 🔗 |
| Hungarian | 🏆 |
| Blossom | 🌸 |
| Best result indicator | 🏆 |
| Heart / matched | ♥ (colored `--pink`) |
| Guarantee check | ✓ |
| Primary CTA prefix | ✦ |
| Compare CTA prefix | ⚡ |

---

## Do / Don't

**Do**
- Always derive colors from CSS tokens — change the token, not every usage site
- Use `linear-gradient(135deg, ...)` for all gradient fills
- Maintain the dark background — the UI is dark-only, no light mode
- Keep `transition: all 0.2s` on interactive elements for consistent feel
- Use `transform: translateY(-Npx)` on card hover — never `scale`

**Don't**
- Don't introduce new background colors — extend the token list if truly needed
- Don't use font weights below 400 or above 800
- Don't add drop shadows except on `.btn-primary` (glow shadow, pink-tinted)
- Don't use border-radius values outside the defined set (10, 12, 16, 20, 24, 50%)
- Don't add new animation keyframes without documenting them here
