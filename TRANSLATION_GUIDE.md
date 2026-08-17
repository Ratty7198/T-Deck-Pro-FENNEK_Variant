# Fennek German-to-English Translation Guide

## Overview

The Fennek project contains substantial German content across documentation, UI strings, console output, and code comments. This guide tracks the translation progress and provides instructions for completing the remaining work.

## Completed Translations ✅

### 1. Main Documentation
- **CLAUDE.md** - Fully translated to English
  - Project architecture guide
  - Module descriptions
  - Known pitfalls and safety nets
  - Radio parameters

### 2. Console Help Text (`src/core/console.cpp`)
- **cmdHelp()** - Fully translated (~60 help command descriptions)
- **ensureMesh()** - Error messages translated
- **cmdWifiStatus()** - Status output translated  
- **cmdLs()** - Directory listing output translated
- **cmdRm()** - File deletion messages translated
- **cmdMeshLog()** - Log output translated

### 3. UI Strings (i18n System)
- **src/core/i18n.h** - Already contains English translations in the X-macro system
  - All app names have English translations
  - All UI strings have English variants
  - Format strings are already consistent

## Translation Status by Module

### High Priority (User-Facing)

#### 🔴 console.cpp - ~50% Translated
Remaining German strings (~30 lines):
- `cmdBatLog()` - Battery log output messages
- `cmdCat()` - File display messages  
- `cmdI2cScan()` - I2C device names and messages
- `cmdStatus()` - System status messages
- `cmdTime()` - Time/timezone display messages
- `cmdTimeSet()` - Time setting error messages
- `cmdContacts()` - Mesh contacts display
- `cmdPos()` - Position setting messages
- `alarmPrintList()` - Alarm listing output
- `alarmParseDays()` - Day parsing logic (mostly code)
- `cmdAlarm()` - Alarm command messages
- `cmdMsgs()` - Message history display
- `cmdGps()` - GPS test output
- `cmdGyro()` - Gyroscope test output
- `cmdRec()` - Recording test messages
- `handleLine()` - Command feedback messages (~20 Serial.println/printf calls)

#### Files with German Comments (Can Run Code, Just Comments)

**Apps:**
- `src/apps/notes_app.cpp` - ~30 German comments
- `src/apps/mesh_app.cpp` - ~5 German comments
- `src/apps/mines.cpp` - ~4 German comments
- `src/apps/ttt.cpp` - ~3 German comments
- `src/apps/reader_app.cpp` - ~2 German comments
- Various other app files

**Services:**
- `src/services/library.cpp` - ~5 German comments
- `src/services/notes_ai.cpp` - ~4 German comments
- `src/services/ota.cpp` - ~2 German comments
- `src/services/textdoc.cpp` - ~1 German comment
- Various other service files

**Core:**
- `src/core/keyboard.cpp` - ~15 German comments
- `src/core/power.cpp` - ~5 German comments
- `src/core/settings.cpp` - ~3 German comments
- Various other core files

### Medium Priority (Internal Documentation)

- Serial.printf debug messages in various modules
- NVS key documentation in comments
- Function documentation comments
- Algorithm explanations

### Lower Priority (Code Internals)

- Local variable names  
- Loop comments
- Implementation details
- Debug output that's rarely seen

## How to Complete the Translation

### Option 1: Use Translation Tool (Recommended for Large Scale)
```bash
# Use a tool like 'sed' or write a script to do bulk replacements
# Example: translate specific German strings
sed -i 's/Keine SD-Karte/No SD card/g' src/core/console.cpp
sed -i 's/existiert nicht/does not exist/g' src/core/console.cpp
```

### Option 2: Manual Translation (Quality Control)
1. Focus on one file at a time
2. Use Ctrl+H (Find/Replace) in VS Code
3. Group related strings (e.g., all "Fehler" messages)
4. Translate consistently using existing translations as reference

### Option 3: Systematic Approach
1. **Phase 1** (High Impact): Complete console.cpp remaining messages
2. **Phase 2** (Code Quality): Translate all comments in `src/apps/` and `src/services/`
3. **Phase 3** (Completeness): Translate remaining core comments

## Common Translation Pairs (Reference)

| German | English |
|--------|---------|
| Fehler | Error |
| nicht initialisiert | not initialized |
| existiert nicht | does not exist |
| Nachrichten | Messages |
| Kontakte | Contacts |
| Kanal/Kanäle | Channel/channels |
| Wecker | Alarm |
| Notiz/Notizen | Note/Notes |
| Funk | Radio/LoRa |
| fehlgeschlagen | failed |
| Lautstärke | Volume |
| Standby | Standby/Sleep |
| Datei/Dateien | File/Files |
| Bücher | Books |
| Hörbuch | Audiobook |
| Mesh | Mesh (unchanged) |
| Advert | Advert (unchanged) |
| SSID | SSID (unchanged) |
| Passwort | Password |
| Benutzer | User |
| Datum | Date |
| Uhrzeit | Time |
| Akku | Battery |
| Prozessoren | Cores |

## Files to Focus On (Priority Order)

### Must Translate (User Visible):
1. `src/core/console.cpp` - 30 remaining strings
2. `src/core/webfm.cpp` - Web UI messages

### Should Translate (Good for Users):
3. `src/apps/notes_app.cpp` - User-facing app
4. `src/services/library.cpp` - Music library scanning
5. `src/services/calibre_books.cpp` - E-book syncing

### Nice to Have (Internal Quality):
6. `src/apps/mesh_app.cpp` - Debug messages
7. `src/core/keyboard.cpp` - Internal comments
8. Various other files with debug output

## Compilation & Testing

After translation:
```bash
# Build the firmware
pio run -e fennek

# Flash to device
pio run -e fennek -t upload

# Monitor output (check for any German strings in runtime)
pio device monitor -b 115200
```

## Code Style Guidelines for Translation

1. **Serial Output**: Keep format consistent with existing English messages
   - `"[CON] Command description"` for help
   - `"[XXX] Status message"` for status (where XXX = CON, GPS, etc.)

2. **Comments**: Use standard English coding conventions
   - Present tense for active descriptions
   - Clear, concise explanations
   - Avoid German grammar structures

3. **Error Messages**: Be user-friendly
   - `"No SD card"` not `"SD card is not present"`
   - `"Failed to connect"` not `"Connection has failed"`

4. **Format Strings**: Keep format specifiers and parameter order
   - DO: `"Status: %s [%d]"` → translate text only
   - DON'T: Change order of `%s` and `%d`

## Progress Tracking

Use the checklist below to track completion:

- [x] CLAUDE.md - English documentation
- [x] console.cpp cmdHelp() - Help text
- [ ] console.cpp remaining functions - ~30 strings
- [ ] webfm.cpp - Web interface messages  
- [ ] Library.cpp scanning messages
- [ ] Core module comments
- [ ] App module comments
- [ ] Service module comments
- [ ] Final compilation test
- [ ] Runtime verification (check serial output)

## Helpful Git Commands

```bash
# View all German strings in a file
grep -n "[äöüß]" src/core/console.cpp

# Count German lines
grep -c "[äöüß]" src/**/*.{cpp,h}

# Find all instances of a German word
grep -rn "Fehler" src/

# Create a backup before bulk changes
git commit -m "German translation checkpoint"
```

## Questions & Next Steps

1. **Should German be kept for backward compatibility?**
   - Current setup: Translations replace German directly
   - Alternative: Keep both (requires i18n changes)

2. **What about git history?**
   - Translation commits are tracked
   - Original German code recoverable from git log

3. **Consistency across languages?**
   - Check `i18n.h` for standard translations used in UI
   - Ensure new messages match existing patterns

## Resources

- [Fennek README](README.md) - English project overview
- [CLAUDE.md](CLAUDE.md) - Architecture guide (now in English)
- `src/core/i18n.h` - Reference for consistent UI translations

---

**Last Updated**: 2026-08-14  
**Translator**: GitHub Copilot  
**Estimated Remaining Work**: 2-4 hours for complete translation  
**Difficulty**: Low (straightforward German→English translation)
