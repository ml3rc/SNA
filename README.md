# Altium project template

**The <https://gitlab.spacetek.ch/stt/space/templates/altium-template> template is very similar, the only difference is that it has the `ASSY` Variant (for space)**

## Project Parameters

### General

* register CCA design w/ <https://spacetek.atlassian.net/wiki/x/8AVvKQ>
  * set `STTNumber` accordingly, valid valid `^STT[\d]{6}$`
  * set `PCBVersion` accordingly, valid `^[A-Z]$`
* set `Board` accordingly
* set `Project`

### Schematic

* set `SCHVersion`, valid `^\d+\.\d+$`
* set `SCHDrawnBy` [^1] and `SCHCheckedBy` [^1] w/ the domain username initials, valid `^[A-Z]{2,}$`
* set `SCHRelease` to release date, `dd.mm.yyyy`

### PCB

* set `PCBVersion` accordingly, valid `^[A-Z]$`
* set `PCBDrawnBy` [^1] and `PCBCheckedBy` [^1] w/ the domain username initials, valid `^[A-Z]{2,}$`
* set `PCBRelease` to release date, `dd.mm.yyyy`

### Draftsman

* set `DWFVersion`, valid `^\d+\.\d+$`
* set `DWFDrawnBy` [^1] and `DWFCheckedBy` [^1] w/ the domain username initials, valid `^[A-Z]{2,}$`
* set `DWFRelease` to release date, `dd.mm.yyyy`

## Variants

EMS[^2] variant shall be labelled _EMS_. If specific, use `^EMS::\[A-Z]{2,}`.

---

[^1]: In case of different `DrawnBy` or `CheckedBy` parameters per sheet, just delete the the parameter in project settings.

[^2]: Electronics Manufacturing Services
