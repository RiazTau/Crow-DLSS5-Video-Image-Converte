# V0.3.1 Change Log

## Win32 ComboBox visibility fix

V0.3.0 populated the NR Preset and NR Style ComboBox controls correctly, but `Layout()` resized each ComboBox to the ordinary 24-pixel row height. For a native `CBS_DROPDOWNLIST`, that height also constrains the dropped list, so only roughly one item was visible.

V0.3.1:

- preserves a dedicated drop-list height for ComboBox controls;
- requests all short option lists to be visible when opened;
- keeps the existing Feature-18 mappings unchanged;
- applies the fix to all GUI ComboBoxes to prevent the same bug elsewhere.

Expected lists:

- NR Preset: `Default`, `Preset #1`, `Preset #2`, `Preset #3`
- NR Style / Look: `Default`, `Natural`, `Cinematic`
