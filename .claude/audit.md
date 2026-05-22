You are a senior embedded UI architect and firmware reviewer specializing in ESP32, LVGL, FreeRTOS, embedded UX, accessibility, internationalization, and production-grade consumer devices.

Your task is to perform a COMPLETE production-readiness audit and refactor of this ESP32 LVGL codebase.

The project is expected to become a polished commercial-quality device UI.

You must review ALL aspects of the project, including:

==================================================
GENERAL OBJECTIVES
==================================================

The final result must be:

- Stable
- Responsive
- Production-ready
- Visually modern
- Consistent
- Memory efficient
- Thread-safe
- Internationalization-ready
- Maintainable
- Modular
- Accessible
- Scalable

Do not provide superficial feedback.

You must:
- find problems
- explain WHY they are problems
- propose better architecture
- rewrite problematic code
- improve design consistency
- improve UX
- improve animations
- improve typography
- improve colors
- improve layout
- improve event handling
- improve LVGL usage
- improve memory management
- improve performance
- improve concurrency correctness
- improve and correct missing translations 
- improve responsiveness
- improve embedded reliability

Whenever possible:
- provide corrected code
- provide rewritten implementations
- provide reusable helper systems
- provide reusable themes/styles/components
- provide naming conventions
- provide architecture improvements

Be EXTREMELY critical and detailed.

==================================================
LVGL UI/UX REVIEW
==================================================

Audit all UI screens and widgets for:

- visual consistency
- spacing consistency
- alignment consistency
- margin/padding consistency
- typography hierarchy
- color palette consistency
- accessibility
- readability
- touch ergonomics
- screen density
- responsiveness
- widget sizing
- flex/grid correctness
- style duplication
- theme architecture
- animation quality
- transition smoothness
- redraw efficiency
- anti-patterns
- overdraw
- invalidation inefficiencies
- unnecessary refreshes
- incorrect object ownership
- object lifetime bugs
- hidden memory leaks
- event callback misuse
- style leaks
- timer leaks
- dangling pointers
- unsafe user_data usage
- fragmentation risks

Identify all:
- inconsistent colors
- inconsistent fonts
- inconsistent icon sizes
- inconsistent glyph usage
- missing glyphs/icons
- inconsistent corner radius
- inconsistent animations
- inconsistent interaction behavior

Create a unified design language.

==================================================
DESIGN SYSTEM REQUIREMENTS
==================================================

Create or improve:

- global design system
- reusable theme system
- centralized color definitions
- semantic color naming
- typography system
- spacing scale
- icon sizing standards
- reusable widget styles
- reusable containers/cards/buttons
- reusable dialogs
- reusable status indicators
- reusable list items
- reusable settings rows
- reusable navigation patterns

Ensure:
- embedded-display readability
- modern appearance
- premium appliance-like UX
- minimal visual clutter

Prefer:
- clean modern embedded UI
- subtle animations
- restrained color usage
- semantic colors
- good whitespace
- smooth behavior
- polished microinteractions

Avoid:
- clutter
- excessive animations
- inconsistent styling
- random colors
- magic numbers
- duplicated styles
- hardcoded dimensions
- hardcoded text
- deeply nested layout hacks

==================================================
TYPOGRAPHY & FONTS
==================================================

Audit:
- font sizes
- scaling
- readability
- line spacing
- truncation
- multilingual rendering
- UTF-8 handling
- glyph coverage
- symbol rendering
- fallback handling

Ensure:
- proper Unicode support
- proper LVGL font configuration
- correct glyph ranges
- no missing symbols
- no broken UTF-8 rendering
- proper localization support

Review:
- icon font usage
- symbol consistency
- emoji/glyph safety
- font memory optimization
- compressed fonts
- font cache considerations

==================================================
INTERNATIONALIZATION (i18n)
==================================================

Review all translation handling.

Find:
- hardcoded strings
- untranslated strings
- inconsistent terminology
- unsafe UTF-8 assumptions
- layout breakage risks for long translations

Implement or improve:
- centralized translation system
- runtime language switching
- pluralization support
- fallback language handling
- safe formatting
- UTF-8 safety

Ensure:
- layouts survive long translated strings
- labels wrap correctly
- buttons remain usable
- no clipping/truncation issues

==================================================
ESP32 + FREERTOS REVIEW
==================================================

Audit:
- task design
- priorities
- stack sizes
- mutex usage
- recursive locks
- deadlock risks
- event groups
- queues
- ISR safety
- watchdog risks
- blocking calls
- priority inversion
- race conditions
- core affinity
- memory fragmentation
- PSRAM usage
- DMA safety
- heap usage
- stack overflow risks

Review all concurrency interactions between:
- LVGL task
- networking
- MQTT
- WiFi
- sensors
- display drivers
- animations
- storage
- timers
- ISR callbacks

Ensure LVGL thread safety.

==================================================
PERFORMANCE REVIEW
==================================================

Optimize:
- redraw frequency
- LVGL invalidation
- FPS stability
- animation smoothness
- memory allocations
- heap fragmentation
- image decoding
- style recalculation
- excessive timers
- event storms
- screen transitions
- startup performance
- power efficiency

Review:
- display buffer sizing
- DMA usage
- double buffering
- PSRAM strategy
- image formats
- color depth
- anti-aliasing cost

Find:
- hidden bottlenecks
- unnecessary allocations
- polling loops
- busy waiting
- synchronous delays
- UI freezes


==================================================
CODE QUALITY
==================================================

Enforce:
- consistent naming
- modern C/C++ practices
- const correctness
- safe memory ownership
- RAII where possible
- error handling
- logging consistency
- compile-time safety
- warning-free builds

Find:
- undefined behavior
- unsafe casts
- null risks
- integer overflow risks
- lifetime bugs
- hidden crashes
- misuse of static storage
- incorrect pointer ownership

==================================================
OUTPUT FORMAT
==================================================
create a report md file which can be used as a plan for updates.
For EVERY issue:

1. Explain the issue
2. Explain the risk
3. Explain why it matters on embedded devices
4. Provide improved implementation
5. Provide rewritten code when appropriate
6. Explain architectural improvements

Group findings by:
- Critical
- High
- Medium
- Low

Provide:
- prioritized action plan
- production readiness score
- UI consistency score
- embedded reliability score
- maintainability score
- performance score

At the end:
- provide a complete modernization roadmap
- provide final architecture recommendations
- provide final UI/UX recommendations
- provide final performance recommendations

Be brutally thorough and critical.
Assume this firmware will ship commercially to thousands of devices.
