---
name: Shaula
description: Screenshots for Wayland.
colors:
  signal-orange: "#f1732c"
  signal-orange-soft: "#ff9b61"
  chalk-ink: "#f5f3ee"
  night-surface: "#0d0e0d"
  night-raised: "#141613"
  night-soft: "#1a1c18"
  rule: "#2a2d27"
  muted-ink: "#969a90"
typography:
  display:
    fontFamily: "Inter, Geist, ui-sans-serif, system-ui, sans-serif"
    fontSize: "clamp(3rem, 8vw, 6rem)"
    fontWeight: 600
    lineHeight: 0.98
    letterSpacing: "-0.04em"
  body:
    fontFamily: "Inter, Geist, ui-sans-serif, system-ui, sans-serif"
    fontSize: "1rem"
    fontWeight: 400
    lineHeight: 1.75
  label:
    fontFamily: "SFMono-Regular, Consolas, Liberation Mono, monospace"
    fontSize: "0.75rem"
    fontWeight: 400
    lineHeight: 1.5
rounded:
  control: "6px"
spacing:
  xs: "8px"
  sm: "12px"
  md: "16px"
  lg: "24px"
  xl: "32px"
  section: "80px"
components:
  button-primary:
    backgroundColor: "{colors.signal-orange}"
    textColor: "{colors.night-surface}"
    rounded: "{rounded.control}"
    padding: "12px 16px"
  button-secondary:
    backgroundColor: "{colors.night-raised}"
    textColor: "{colors.chalk-ink}"
    rounded: "{rounded.control}"
    padding: "12px 16px"
---

# Design System: Shaula

## Overview

**Creative North Star: "The Quiet Utility"**

Shaula's website behaves like a well-made native utility: direct, dark, compact, and dependable. The composition is linear and left-aligned, with enough space to make four simple beats legible without turning them into a campaign.

The system rejects generic SaaS copy, feature walls, decorative technical motifs, glow effects, gradients, and glass. Product imagery and executable commands carry more weight than explanation.

**Key Characteristics:**
- Near-black, subtly tinted surfaces with one orange signal color.
- Flat structure defined by spacing and one-pixel rules.
- Large plain-language headings and short supporting copy.
- Real commands and real product imagery as proof.

## Colors

The palette is restrained: orange signals action or state; tinted near-blacks and readable neutral text carry everything else.

### Primary
- **Signal Orange** (`#f1732c`): primary actions and focus indication.
- **Soft Signal Orange** (`#ff9b61`): hover and successful state feedback.

### Neutral
- **Night Surface** (`#0d0e0d`): page background.
- **Night Raised** (`#141613`): command and media surfaces.
- **Night Soft** (`#1a1c18`): pressed and hover surfaces.
- **Rule** (`#2a2d27`): one-pixel boundaries.
- **Chalk Ink** (`#f5f3ee`): primary text.
- **Muted Ink** (`#969a90`): explanatory text.

**The Signal Rule.** Orange is reserved for action, focus, and meaningful state. It is not decoration.

## Typography

**Display Font:** Inter with Geist and system sans fallbacks
**Body Font:** Inter with Geist and system sans fallbacks
**Label/Mono Font:** SFMono-Regular with Consolas and Liberation Mono fallbacks

**Character:** One sans family keeps the voice plain and contemporary. Mono appears only where the content is executable or machine-shaped.

### Hierarchy
- **Display** (600, `clamp(3rem, 8vw, 6rem)`, 0.98): homepage statement only.
- **Headline** (600, 2.25rem-3rem, 1.05): section titles.
- **Body** (400, 1rem-1.125rem, 1.75): short explanatory copy, capped near 65ch.
- **Label** (400, 0.75rem, normal case): commands and compact metadata.

**The Plain Speech Rule.** Headings state facts. They do not perform enthusiasm.

## Elevation

The system uses no shadows. Depth comes from slightly lighter surfaces and one-pixel borders; floating glass and ambient glow are prohibited.

**The Flat-By-Default Rule.** A surface earns separation through function, not decoration.

## Components

### Buttons
- **Shape:** compact rectangle with a 6px radius.
- **Primary:** Signal Orange with dark text and 12px by 16px padding.
- **Hover / Focus:** soft-orange hover, one-pixel lift, and a visible orange focus outline.
- **Secondary:** raised near-black surface with Chalk Ink and a Rule border.

### Cards / Containers
- **Corner Style:** square for command and media surfaces.
- **Background:** Night Raised.
- **Shadow Strategy:** none.
- **Border:** one-pixel Rule boundary.
- **Internal Padding:** 16px-20px.

### Navigation
- Fixed, 64px high, one-pixel lower rule, compact text links, and one bounded GitHub action.

### Command Row
- A visible package label sits above a horizontally scrollable command with a 44px copy control.
- Copy success is announced; clipboard failure selects the command for manual copying.

## Do's and Don'ts

### Do:
- **Do** keep the page to Hero, Install, Demo, and GitHub.
- **Do** let executable commands and real product imagery provide proof.
- **Do** use Signal Orange only for action, focus, and meaningful state.
- **Do** preserve WCAG AA text contrast, visible focus, and reduced motion.

### Don't:
- **Don't** write generic SaaS copy or pitch-deck language.
- **Don't** add feature inventories, decorative metrics, or repeated section kickers.
- **Don't** use terminal cosplay as a visual theme merely because the audience is technical.
- **Don't** add gradients, glow effects, glassmorphism, or colored side stripes.
- **Don't** use mono text unless the content is genuinely technical or executable.
