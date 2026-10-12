---
id: 9
status: done
tags:
- engine
- stretch
created_at: 2026-10-12T15:04:48Z
updated_at: 2026-10-12T22:29:00Z
---
## Why
The Machinedrum's output passes through a stereo master section on its mixer DSP (DSP1): rhythm echo (delay), gate box
(reverb), master EQ and dynamix (compressor). The MPC port dropped it for CPU. Our plugin puts the reverb and delay sends
on their own buses instead, and the four master-effect blocks of a kit (`md::library::Kit::masterFx`, kept raw) do
nothing. The user wants the real master effects. On this Mac the voice DSP costs ~7 % of a core on the JIT, so CPU is no
longer the constraint.

## Result of the research spike (2026-10-12): recommendation first

**Run DSP1's own program, master section only, on a second emulated DSP (approach A', below), fed by the native
`Mixer`'s outputs. Keep `TrackFx` and `Mixer` native. Translate the master section to C++ later only if RAM or CPU ever
asks for it, with A' as the oracle.** Reasons, all measured here (OS 1.63, M-series Mac, JIT build, `MD_USE_JIT=ON`):

- It works today. I booted DSP1, drove it block by block from a host harness whose only patches are outside the master
  code (an end-of-block stub; the whole-loop variant patches four more words), and ran the golden pattern through it: the master pass is **identical** to the same pass inside the whole
  DSP1 loop (6000 blocks, 1.8 M words incl. the whole master Y state, 0 differences; three kits), and the **JIT equals
  the interpreter** on the master section over ~11 000 blocks of random kits (extreme values included) and random
  signals (57 M words compared, 0 differences).
- Cost: **10 295 DSP instructions per 32-sample block** (14.2 M/s), **12.4 us/block on the JIT = 1.7 % of one core**
  (0.18-0.2 s per 12 s of audio; today's 12 s golden render is 0.52 s, of which the voice DSP is 0.37 s). Interpreter:
  143 us/block (20 %).
- Exact by construction (the original program and the original OS parameter code run unchanged), about a day or two of
  work, versus a week or more for a bit-exact translation of 1255 instructions of 48-bit DSP code.
- The dry mix is untouched: the master is an extra stage after `Mixer`. `md-hash` stays `04ef1db4fe767721` (the spike ran
  the native chain with the master pass behind it and the golden hash still printed it), kit 0 `f5c618b7df5550fa` and the
  other engine-level hashes are unaffected.
  The plugin's main bus is what changes when the master is on, so the plugin needs a master bypass (see "Hashes").
- Not recommended: retiring `TrackFx`/`Mixer` in favour of the whole DSP1. It is bit-exact too (2000 blocks incl.
  non-main routes, 0 differences) but costs 48-52 us/block (6.7-7.2 % of a core) against 5 us/block (82+4 ms per 12 s) for the
  native chain.
- Price of A': **RAM**. A second emulated DSP is ~275 MB in `ru_maxrss`/`phys_footprint` before the program is even
  loaded, ~375-540 MB once DSP1 has booted (the project already sits at 420-500 MB per instance). That is the one real
  argument for B; see "Risks" for how to look at trimming it.

### What was known before and what this spike established
| | Known (HANDOFF.md / PROTOCOL.md) | Established here |
|---|---|---|
| Where | master section "P:$344-$970" (`HANDOFF.md:197`), `MixerRef::runMaster` $342->$971 (`tools/mdmix/MixerRef.cpp:135-144`) | starts at $342, ends at $9a7: 1255 instructions in 1638 words; $971-$983 adds the result to the output frame, $984-$9a7 are input-level meters (they read the codec ADC buffer, not the master output) |
| Inputs | dry L/R `X:$180`, sends `X:$1c0` / `X:$600` (`PROTOCOL.md:397-398`) | confirmed by impulses; layout of all buffers, what is left where (below) |
| Parameters | `Y:$150-$18c`, "not decoded" (`PROTOCOL.md:111,328`); 32 kit bytes at `$1000d7c+16` | all 38 words decoded: four groups, who computes them (the OS tick, `$20b4a2-$20ba4c`), from which kit byte, through which OS table |
| Cost | ~9740 instr/block, ~13.4 M/s (`HANDOFF.md:1429-1432`) | 10 295/block incl. output stage and meters; per-stage table; JIT wall clock |
| Delay memory | "external memory at `$1439xx`" (`HANDOFF.md:1430`) | that is the reverb/dynamix state; the echo's rings are X:$10011a-$12f711 and the reverb's lines X:$130000-$137fff (map below) |
| Boot | `MixerRef` runs init and stops at $2d | free-running DSP1 halts itself (watchdog); what it waits for; a harness that runs its loop |
| Parameter order | unverified (`MdSysex.h:75-84`) | confirmed from the OS and the manual; the editor's page order differs from the storage order |

## 1. What DSP1's master section does

### 1.1 Signal flow
```
 16 tracks -> TrackFx (native) -> Mixer (native: pan, VOL, REV/DEL sends)
                                        |
   dry main L/R  X:$180 ----------------+-----------------------------------------------+
   delay send    X:$600 --> RHYTHM ECHO (stereo ring, modulated tap, FB loop with HP+LP, |
                              MONO cross-feed, LEV) --> echo return (X:$600)------------+
                                |  DVOL x echo return                                    |
   reverb send   X:$1c0 --------+--> GATE BOX REVERB (pre-delay, mono sum, 5 diffusers,   |
                                     4 damped lines, modulated taps, gate, HP/LP, LEV)---+
                                     reverb return (Y:$0..$3f)                           |
                                                                                         v
                           X:$180 = dry + echo return + reverb return  ($66c-$67b)
                                  -> MASTER EQ  (low shelf, high shelf, parametric, GAIN)
                                  -> DYNAMIX    (look-ahead compressor, side-chain HP, OUTG, MIX with the uncompressed)
                                  -> X:$180 and Y:$0 (final stereo block)
                                  -> added to channels 2 (L) and 5 (R) of the 6-channel output frame (X:[X:$640], $971)
```
Facts behind the diagram (each from the disassembly, and where it says "test" from an experiment on the emulated DSP1):
- The echo is fed by the delay send only; the reverb by the reverb send plus `DVOL` x the echo return (`$44e-$464`: it reads
  `X:$600` after the echo rewrote it). Test: an impulse on the delay send reappears at TIME = 8 at 120 BPM exactly 5512
  samples later (8 x 82 688 / 120 = 5512.5), once per FB pass; `DVOL` 0 -> 127 adds reverb to the echo.
- The dry bus goes through EQ, dynamix and GAIN only. Test: a dry impulse comes out 18 samples later (constant over every
  setting tried: default, EQ GAIN, dynamix OUTG/MIX/TRHD, echo TIME 0, reverb LEV 0); stopping the pass at stage ends shows the
  impulse still at frame 0 in `X:$180` up to `$953` and at frame 18 after the dynamix's final stage (`$953-$970`, which reads
  the block back from its 96-frame look-ahead ring at `X:$143d00`). L stays L (R gets 0).
- The reverb input is a **mono sum** (L+R) of the pre-delayed send; its output is stereo and decorrelated. Test: an impulse
  on the L or on the R of the reverb send gives the identical output (3840 of 3840 frames).
- Master effects act on the main outputs only. Tracks routed to individual outputs, and routes 0/1 (which the mixer
  adds to the main pair of the frame without effects), bypass them (manual p. 28; `Mixer.h:25-26`). The mixer's
  `frame` is therefore an input of the master pass too (it is added to).
- Level: EQ GAIN 127 is +5.8 dB (an impulse of 0x400000 comes out as 8 205 991 with dynamix bypassed), GAIN 64 is -6.1 dB.
  With the kit defaults (EQ flat, GAIN 127, dynamix 127/127/127/127/127/127/0/0: a 0 dB limiter, fully wet, OUTG 0) a 1 kHz
  sine at 0.02-0.1 FS comes out at -2.6 dB and at 0.7 FS at -6.4 dB. **Master on is not level-neutral against the dry bus.**

### 1.2 Buffers (DSP1 memory, as `Mixer::Output` fills them)
| Address | Content |
|---|---|
| `X:$180..$1bf` | dry main mix, 32 frames interleaved L,R (`Mixer::Output::main`); also the master's output block |
| `X:$1c0..$1ff` | reverb send, same layout (`rev`) |
| `X:$600..$63f` | delay send (`del`); holds the echo return during the pass |
| `X:$400..$4bf` | 6-channel output frame, 6 x 32 words (`frame[f][ch]`, index 6f+ch); `X:$640` = its base, `X:$641` = the codec ADC buffer ($100) the meters read. Result: ch 2 = main L, ch 5 = main R |
| `Y:$150..$1c1` | the master section's whole Y state and parameter words (map below) |
| `X:$000..$0ff`, `Y:$000..$0ff` | scratch (the final stereo block ends in `Y:$0..$3f` too) |
| `X:$1000da..$137fff` | external RAM cleared by init (`0x37f26` words): echo rings (L `X:$10011a-$117c15`, R the same +$17afc; ring length `$17adc` = 96 988 frames = 2.2 s), reverb pre-delay ring `$134000-$137fff` (16 384 words), the reverb's diffuser/delay lines `$130000-$133fff` |
| `X:$143d00..$143dbf` | dynamix look-ahead ring (192 words) |
| `P:$140000..$143e7f` (also X/Y through the bridged SRAM) | tables from the OS file: filter/EQ/dynamix curves, the reverb descriptors at `$14398d` (gains, line pointers and lengths 47/67/101/149/223 and 2695/2479/1843/1653/2048/1024 words) |

### 1.3 The parameter words and who writes them
The ColdFire writes **38 words in four groups** into DSP1's Y memory with the host command `P:$12` (vector `jsr $9aa`: the
handler reads address and count from HRX and programs DMA channel 5 to copy the following words into `Y:addr`). The
caller is the tail of the OS tick routine, `$20ad9a`, and the sender `$20aa66-$20ab1a` (block-write function
`$100074c`; flags `$29f448 $29ee50 $29f39a $29ea40`). The tail `$20b4a2-$20ba4c` runs **one group per tick**, selected by
`$265404 & 3`, so every group is refreshed every fourth tick (~33 ms at the OS's ~120 Hz). It is straight-line C that
reads only the **smoothed kit values** (32 halfwords at `$1000d9c`, `value << 7`, produced each tick by `$100029e` from the
kit bytes at `$1000f6c`), the tempo word `$100150c` (BPM x 24) and OS tables, and writes four 68k buffers.

I ran it unchanged in Musashi (`MachineRunner`): patch an `rts` over `$20ba4c`, poke the smoothed values, poke
`$265404 = group`, `call($20b4a2)`, read the buffer. Nothing else in the tick is involved.

| Group (`$265404 & 3`) | Effect | 68k buffer | DSP1 words | Smoothed values read |
|---|---|---|---|---|
| 0 | rhythm echo | `$1001af4` | `Y:$150..$158` (9) | `$1000dac..$1000dba` |
| 1 | dynamix | `$1001b18` | `Y:$17a..$184` (11) | `$1000dcc..$1000dda` |
| 2 | master EQ | `$1001b44` | `Y:$170..$179` (10) | `$1000dbc..$1000dca` |
| 3 | gate box reverb | `$1001b74` | `Y:$185..$18c` (8) | `$1000d9c..$1000daa` |

The 32 kit bytes (kit dump offset 0x487, `MdSysex.h:21`) are stored in the OS as **reverb, echo, EQ, dynamix**
(kit RAM `$70042a/$700432/$70043a/$700442`, copied to the smoothing targets `$1000f6c/f74/f7c/f84` by `$209fe0-$20a03c`
and by the per-parameter setters `$205730-$2057b8`). Word by word:

| Y word | Kit parameter (block, index) | What the OS computes (`v` = parameter, `w = v << 7`, T[] = OS table in the image) | Role in the DSP |
|---|---|---|---|
| `$150` | echo TIME (1,0) | `$17adc - n`, n = TIME x 82 688 / BPM samples (TIME in 128th notes; 127 is stretched to 128); `$20` if n > `$17adb` | ring offset = ring length - delay |
| `$151` | TIME | `$4001`, or `$5ebf0000 / n` for delays longer than the ring | slew target of the read rate (1.0 = `$4001`; role inferred from `$346-$35e` and `$361-$381`) |
| `$152` | MOD (1,1) | `(2w) & $1fffe` | delay modulation depth |
| `$153` | MFRQ (1,2) | `(w << 4) + $200` | modulation phase increment |
| `$154` | FB (1,3) | `(w << 9) & $1fffe00` | feedback gain (64 -> $400000, doubled by the loop = unity) |
| `$155` | FILTF (1,4) | `$7fffff - T[w >> 6]` (`$24da14`) | loop high-pass coefficient |
| `$156` | FILTW (1,5), FILTF | `$7fffff - T[min((FILTF + FILTW) x 2, 255)]` | loop low-pass coefficient (FILTF+FILTW) |
| `$157` | MONO (1,6) | `($3fff - w) << 9` | feedback cross-feed (ping-pong <-> stereo) |
| `$158` | LEV (1,7) | `(w << 8) & $ffff00` | echo output level |
| `$170-$172` | EQ LF, LG (2,0-1) | shelf coefficients from T[`$24e614`][w>>7], T[`$24e814`][w>>7] (two branches around LG = 64, 3 divisions) | low shelf (1st order, 48-bit state `Y:$192-$197`) |
| `$173-$175` | EQ HF, HG (2,2-3) | same with T[`$24e414`] | high shelf (state `Y:$198-$19f`) |
| `$176` | EQ PQ (2,6) | T[`$24ec14`][w>>7] | parametric Q |
| `$177` | EQ PF (2,4) | T[`$24ea14`][w>>7] | parametric frequency |
| `$178` | EQ PG (2,5) | +/- T[`$24ee14`] (sign flips at w = `$2000`) | parametric gain; the DSP derives the biquad `Y:$18d-$191` per block (24-step `div` loops) |
| `$179` | EQ GAIN (2,7) | `(w x w) >> 5` | output gain after the EQ |
| `$17a` | DYN ATCK (3,0) | T[`$24e014`][w>>7] | envelope attack |
| `$17b` | DYN REL (3,1) | T[`$24e214`][w>>7] | envelope release |
| `$17c` | DYN OUTG (3,6) | T[`$24de14`][w>>7] | make-up gain |
| `$17d` | DYN HP (3,5) | T[`$24da14`][255 - (w>>6)] | side-chain high-pass |
| `$17e` | DYN MIX (3,7) | `(w << 9) & $1fffe00` | compressed/original cross-mix |
| `$17f-$184` | DYN TRHD, RTIO, KNEE (3,2-4) | threshold (T[`$24de14`][TRHD>>1]), ratio (`w^2>>21 + 1`) and knee (`(w>>7) + $2a`) turned into the curve segments with 4 divisions | gain computer |
| `$185` | REV HP (0,4) | `$7fffff - T[w >> 6]` | output high-pass |
| `$186` | REV LP (0,5) | `$7fffff - T[w >> 6]` | output low-pass |
| `$187` | REV DEC (0,2) | `w >> 8` (0-63) | diffuser/line gains |
| `$188` | REV DAMP (0,3) | `w >> 8` (0-63) | damping |
| `$189` | REV LEV (0,7) | T[`$24bb90`][w>>7] << 7 | output level |
| `$18a` | REV GATE (0,6) | `$7fffff` if w > `$3f70` (GATE 127 = off), else from T[`$24bd94`] | gate time |
| `$18b` | REV DVOL (0,0) | `(w << 9) & $1fffe00` | echo -> reverb level |
| `$18c` | REV PRED (0,1) | `w & $fffe` | pre-delay in frames |

Test vector (kit 1 "EFM" of `MD_presets_1_50.syx`, 120 BPM, smoothed values set to exactly `v << 7`):
`Y:$150..$158 = 017018 004001 000000 010200 220000 0078b4 071408 7ffe00 338000`,
`Y:$170..$179 = 200000 e05200 7eb900 200000 fea900 056000 018abe 008dad 080000 7e0200`,
`Y:$17a..$184 = 00b0ee 000b7b 023b0d 7e72df 000000 020c07 1a2779 012967 38ae80 0403fa 2aaaaa`,
`Y:$185..$18c = 0085f9 093100 000022 000000 23bf00 7fffff 690000 000a80`.

DSP1 itself writes `Y:$159..$16f` (echo write phase, delay offset, rate, filter states), `$18d-$1bd` (EQ coefficients,
filter and dynamix state), `$1be-$1c1` (codec-input peak meters the ColdFire peeks) and the loop variables `$1c2-$1c9`.
Init (`P:$100000`, `$143e4a`, `$143de4`, `$143dc0`) zeroes `Y:$150-$1bd` and sets defaults, among them **`Y:$15a = 40000`**:
the echo's delay offset starts there and moves toward `Y:$150` by one sample per sample, so after boot the echo glides (a
pitch-shifted sweep, ~1.2 s for TIME = 8). That is how the hardware behaves too; the harness should set `Y:$15a = Y:$150` once
after the first words so that an engine does not glide at start-up.

### 1.4 Where the code lives (`OS section 2`, disassembled with the repo's `Disassembler`; stage costs measured by stopping the pass at each address)
| P range | Instr/block | Role |
|---|---|---|
| `$342-$3c7` | 559 | echo: delay-offset slew (`Y:$150/151/15a/15b`), modulation phase (`Y:$159`), read addresses `X:$80-$bf`, two taps per channel for interpolation |
| `$3c8-$3e4` | 273 | echo: input + FB x delayed |
| `$3e5-$422` | 487 | echo: in-loop band filter (FILTF/FILTW), MONO cross-feed matrix, LEV |
| `$423-$43b` | 202 | echo: write back into both rings |
| `$43c-$475` | 346 | reverb: input = reverb send + DVOL x echo return, pre-delay ring, readout at PRED, mono sum |
| `$476-$4a0` | 768 | reverb: five series diffusers (descriptor table `P:$14398d`), gains scaled by DEC |
| `$4a1-$4f2` | 297 | reverb: four damped delay lines (DAMP) |
| `$4f3-$5b5` | 1394 | reverb: modulated fractional-delay taps and the mixing/decorrelation network that makes the stereo return |
| `$5b6-$613` | 351 | reverb: gate (peak follower, fade) |
| `$614-$66b` | 491 | reverb: HP/LP and LEV, return to `Y:$0..$3f` |
| `$66c-$67b` | 198 | sum: dry + echo return + reverb return -> `X:$180` |
| `$67c-$6f7` | 70 | EQ: parametric coefficients from `Y:$176-$178` (24-step `div`) |
| `$6f8-$7a6` | 1106 | EQ: low and high shelf (48-bit `mpysu/macsu/dmac` state) |
| `$7a7-$86a` | 1113 | EQ: parametric biquad (scaling-down mode via `bset #$b,sr`) |
| `$86b-$880` | 329 | EQ: GAIN |
| `$881-$8c3` | 414 | dynamix: look-ahead ring, side-chain HP |
| `$8c4-$907` | 481 | dynamix: level detection and attack/release envelope |
| `$908-$952` | 457 | dynamix: gain computer (threshold/ratio/knee) and gain smoothing |
| `$953-$970` | 459 | dynamix: apply gain, OUTG, MIX, write the final block |
| `$971-$983` | 140 | add into frame channels 2 and 5 (`$971-$980`; the meter setup starts at `$981`) |
| `$984-$9a7` | 334 | codec-input peak meters (`Y:$1be-$1c1`, reading the ADC buffer `X:[X:$641]`) |
| `$9a8` | | `bra $3c`: end of the block |

Instruction mix of `$342-$9a7`: 772 moves, 137 mac, 61 mpy, 57 `do`, 41 add, 27 asl, 26 macsu, 16 mpysu, 14 dmac, 3 maxm,
2 div, 3 tge, 1 `bset`/`bclr sr`. No self-modifying code in the master section (the mixer's generated MAC pairs at
`P:$9e2` are used by the mix, not here). Labels are read from the code; the three behaviours I did not test with
signals (shelves, parametric band, dynamix curve) follow from the parameter pipeline above, not from measurement.

## 2. Parameter order of `MdSysex.h` (deliverable 5): confirmed, one thing to add
`MasterFx` lists `reverb: DVOL PRED DEC DAMP HP LP GATE LEV`, `delay: TIME MOD MFRQ FB FILTF FILTW MONO LEV`,
`eq: LF LG HF HG PF PG PQ GAIN`, `dynamics: ATCK REL TRHD RTIO KNEE HP OUTG MIX`, in the dump order reverb, echo, EQ, dynamix.
All of it is right. Evidence:
1. The OS's array is that order (table in 1.3: each index feeds the DSP role its name says, e.g. index 6 of the reverb
   block is the gate and 127 turns it off, `w > $3f70 -> $7fffff`; index 4/5 of the echo are the loop filter; index 6 of
   the EQ is Q, 4 and 5 frequency and gain).
2. The kit dump block order is the kit RAM order (`$70042a` reverb, `$700432` echo, `$70043a` EQ, `$700442` dynamix) and
   the 16 preset kits' values fit the names: reverb LEV 96-111 in every kit, LP 127 with HP 0 and GATE 127 (open / off) in
   the init-like kits, echo FILTW 127 and MONO 0 in most with LEV 71-127, EQ gains 64 = flat and GAIN 127 in 14 of 16, dynamix
   either the default 127/127/127/127/127/127/0/0 or a used compressor with OUTG 4-30 and MIX 0 (`MD_presets_1_50.syx`
   kits 0-15; `kitfx` in the spike prints them).
3. The manual (p. 29-31) lists the parameters of each page in this order.

What differs is the **page order**: the manual describes the master effects as RHYTHM ECHO, GATE BOX, MASTER EQ, DYNAMIX, names
the control machines `CTR-RE`, `CTR-GB`, `CTR-EQ`, `CTR-DX` and the menu tabs ECHO, REV, EQ (p. 28, A-13/14), and the OS code that
picks a master block by a 0-3 selector (`$21bff0-$21c03a`) takes 0 -> `$700432` (echo), 1 -> `$70042a` (reverb), 2 -> `$70043a`
(EQ), 3 -> `$700442` (dynamix). So the editor's pages should run ECHO, REV, EQ, DYN while the storage order (kit dump, `MasterFx`)
stays reverb, echo, EQ, dynamix. (The tab order itself is not spelled out in the manual text; check it on the LCD artwork
or a capture if it matters.) The comment in `MdSysex.h:75-77` ("not used by the engine yet") and the "unverified" note in
`docs/DEVELOPMENT.md:141` should be updated when the master lands.

## 3. DSP1 boot experiment (deliverable 4, first item)
Loaded section 2 (`fw.dspB`) exactly as `VoiceEngine::loadImage` loads section 1 (P and anything >= `$20000` through
`memWriteP`), AAR0-3 = `$100539 $140639 $180539 $1c0639`, OMR `$00490d` (`MixerRef`'s DSP1 value; DSP2's is `$00498d`),
silent ESSI frames in and out, PC = `$24`, JIT with `interruptRegionIsCode` (the program has ordinary code in the vector
area: the whole main loop is at `$24-$a3`).
- **Init completes**: `P:$24` -> `jsr $100000` -> back at `$2d` after **820 565 instructions** (22 ms with loading and the
  JIT): sine table (32 767 steps), 229 k-word RAM clear, per-track state, Y:$150-$1bd zeroed and the three master init
  routines, ESSI0/1 role detection (4000-iteration loop on Port D), DMA0 (codec ADC -> `X:$100`, 128 words) and DMA1.
- **It does not reach a steady main loop in free-running emulation.** It parks at `$36` polling `x:<<$ffffee` (DMA0's
  destination pointer) for `$139`; the DMA0 block-complete interrupt (`P:$18` -> `$9c2`) fires three times first, and the third
  runs `movep #0,x:<<$ffffe8` and `jmp *` at `$9da`: a **deadline watchdog** that halts the DSP if the block loop is late
  (observed: 2 M instructions per chunk at `$9da` forever).
- **What the real loop waits for** (all replaced by the harness): (1) the codec DMA position (`$36`, `$3c`: `$139`/`$13f`/`$17f`
  in `x:$ffffee`); (2) each track's 32 voice words from DMA4 (`$73-$79`: `x:$ffffde - y:$1c4 >= $20`); (3) the ColdFire's
  Y writes by host command and DMA5 (`$12 -> $9aa`, `$10 -> $9b6`, `$22 -> $9bf`).
- **Harness (works, bit-exact):** after init, switch DMA0 off (`x:$ffffec = 0`); mask interrupts and set `m0..m7 = $ffffff`
  for each pass; for the whole loop patch `$3c -> jmp $44`, `$79 -> nop`, `$7d -> nop` (a `jseq $270`), `$25e -> jmp $6f` (the
  track function's timing pad: a 32 x 15 nop loop, ~480 instructions per track) and make `$9a8` jump to a stub that sends one host word and waits for
  another (as `VoiceEngine` ends its block), stub at `P:$c00` (free internal P). Per block the host pokes the 16 voice blocks
  at `Y:$600 + $20k`, the track words `Y:$200 + $40k` and mix words `Y:$100 + 5k`, and starts at `$3c` (the first pass starts
  at `$294`). The master-only harness needs just the stub at `$9a8`, `PC = $342`, `X:$180/$1c0/$600/$400` and
  `X:$640 = $400` poked.
- **Results** (spike tools, scratch space, not in the repo): the whole DSP1 loop on the golden pattern reproduces
  `Mixer::Output` main, rev, del and the 6-channel frame word for word (2000 blocks, 768 000 words, JIT and interpreter,
  0 mismatches) - this also re-verifies `TrackFx`/`Mixer` against DSP1 through the real loop. The master-only harness
  equals the whole loop (6000 + 4000 + 4000 blocks, kits EFM, ALIENS, BOOGIE: the 192-word frame and 114 Y words every
  block, 0 mismatches).

## 4. Cost (deliverable 2)
| | Instr/block | JIT us/block | share of one core | per 12 s of audio |
|---|---|---|---|---|
| master section alone (`$342-$9a7`) | 10 295 (14.2 M/s) | 12.4 | 1.7 % | 0.205 s (0.176 s measured inside the integrated run) |
| whole DSP1 loop (without the timing pads; 51 581 with them) | 43 885 (60.5 M/s) | 48-52 | 6.7-7.2 % | 0.8-0.86 s |
| native `TrackFx` + `Mixer` (what we run today) | - | ~5.2 | 0.7 % | 82 + 4 ms (`MD_TIMING=1 md-hash`) |
| voice DSP + OS tick (today) | - | - | - | 368 + 59 ms; the golden render is 523 ms in all (4.4 % of real time) |
| master section on the interpreter | 10 295 | 143 | 19.7 % | 2.4 s |

- Adding A' to the engine turns today's 523 ms into about 700-730 ms (**4.4 % -> ~6 %** of real time); the plugin bench
  (7.2 % at 44.1 kHz) should land near 9 %. The master runs every block whether or not anything sounds (it has feedback
  loops and a free-running modulation phase; see Risks on skipping).
- Pokes of the 384 input words cost 0.5 us per block (`Memory::set` 0.1 us); the stub-based block end matters: stopping each
  pass by patching memory (as `MixerRef` does) invalidates JIT blocks and made the same pass cost 127 us.
- The first master block compiles ~1200 instructions in the JIT: **13.6 ms** once. It must happen in the processor's
  warm-up, not on a live block (`kWarmUpBlocks = 16`, `OneProcessor.h:80`).
- DSP1 boot: 22 ms. RAM: see Risks.
- Hardware load for scale: DSP1 ran ~79 M instr/s on the MD (57 k instructions per block); the timing pads are ~7.7 k of them.

## 5. The two approaches (deliverable 3)

### A. Run DSP1's program
**A' (recommended): master only.** `MasterFx` owns a second `dsp56k::DSP` with DSP1's program (section 2), the stub harness
above, DMA0 off, and the 38 words. Per engine block: poke `Mixer::Output` main/rev/del/frame, run `PC = $342` to the stub,
read frame channels 2 and 5.
**A (whole DSP1):** also run the track function and the mix on the DSP, feeding it the 16 voice blocks; `TrackFx`/`Mixer`
could be deleted. Works, bit-exact, but 9x the CPU of the native chain and no new capability; keep it as the test oracle.

- *Verification:* by construction the same code; against the references above: (1) master-only vs whole-DSP1 loop on the golden
  pattern; (2) JIT vs interpreter lockstep on random kits and signals (extend `VoiceEngine::lockstep`'s approach, or a test
  that runs two `MasterFx` instances, one with `setInterpreterEnabled`); (3) the dry/sends/frame entering the master are
  `Mixer`'s, already verified against DSP1.
- *Risk:* low. The JIT executes DSP1's instruction mix correctly in everything tried (the three JIT bugs of increment 0011
  were found on DSP2's program; DSP1's master adds `mpysu/macsu/dmac/maxm/div/tge` and the scaling bit, all exercised by
  the 57 M words with 0 differences); the x64 JIT has not been run (same caveat as increment 0011). RAM is the real risk.
- *Effort:* 1-2 days for the engine stage (`MasterFx`, `HostModel` master pipeline, tests), 1-2 days for plugin wiring and the editor.
- *CPU:* +1.7 % of a core (JIT), +20 % without a JIT. *Hashes:* nothing existing changes.

### B. Translate the master section to C++
As `TrackFx` was done: stage by stage (the table in 1.4 is the stage list; the stop-address method of the spike gives the
intermediate values to compare), each stage checked against A' with random states, parameters (0/127 extremes) and
signals, then the whole chain. `Dsp56.h` would need `maxm`, a stage model of the scaling-down mode (`bset #$b,sr`), 48-bit
`l:` moves for the shelf and biquad states (partly there), and the init state (tables read from section 2's records).
- *Verification:* A' is the oracle (and stays in `tests/` as `Dsp1Reference`, like `tools/mdmix`): per-stage and whole-chain
  word-for-word comparison, the same way `mdfxtest`/`mdmixtest` verified the chain (3.2 M samples there).
- *Risk:* medium. 1255 instructions in 1638 words is ~3.6x the per-track function, which became 551 lines
  (`TrackFx.cpp`); expect 1800-2200 lines. The long tail is exactly what makes it fragile: look-ahead ring and gate edge
  cases, 24-step `div` loops reproduced with `d56::div`, the free-running modulation phases and the stereo diffusion network,
  all of which must match to the bit including their state after any input history.
- *Effort:* about a week of focused work, plus the same 1-2 days of wiring.
- *CPU:* roughly 3-4 us/block (estimated from `TrackFx`: ~0.3-0.5 ns per DSP instruction) = ~0.5 % of a core, saving ~1.2 % over A'.
  *RAM:* no second DSP (the delay memory is ~1 MB).

### Comparison
| | A' master on DSP1 | A whole DSP1 | B translation |
|---|---|---|---|
| exactness | by construction | by construction | proven per stage against A' |
| effort | 1-2 days + wiring | A' + 1 day, deletes code | ~1 week + wiring |
| CPU, this Mac (JIT) | +1.7 % | +6.0 % net | +0.5 % |
| CPU without JIT | +20 % | +85 % | +0.5 % |
| RAM per instance | +~375-540 MB | same | ~+1 MB |
| existing hashes | unchanged | unchanged | unchanged |
| verification risk | low | low | medium |

**Recommendation: A' now; B only as an optimisation, and only after the RAM question below is answered** (if the second DSP
can be trimmed to something like 100 MB, B never pays for itself). Do not retire `TrackFx`/`Mixer`.

### Hashes (deliverable 3)
- `Mixer::Output` (main, rev, del, frame) is what `md-hash` and the kits test hash. The master stage adds a new output field
  (`Engine::Output::master`), so `04ef1db4fe767721` (plain, `MD_GROUPS=2/4`, JIT and interpreter) and kit 0 `f5c618b7df5550fa`
  do not move. Checked: the spike's integrated run (native chain + master on the second DSP) prints `04ef1db4fe767721` for the
  golden pattern.
- The master pass is **not** "dry + returns on top": the dry bus goes through EQ, dynamix and GAIN too (1.1). The plugin
  hashes `da62047690565e38`, `a28b9dc178776939`, `14291ff9302212d4` are of the main bus; with the master on they change by
  definition. Therefore: a master on/off host parameter, the regression renders and `plugin-state` run with it **off** (old
  hashes hold) and a new pair of `master` hashes is recorded with it on.
- Candidate values, only to show that the pipeline is deterministic, not acceptance values (static words computed from exact
  `v << 7`, 125 BPM, 12 s golden pattern; the real path goes through the OS slew and differs by its 3-unit offset):
  master-out hash JIT = interpreter for kit EFM `91b5d84165c0c467`; JIT only: TRX `76c6ee93aebc899e`, BOOGIE
  `a50da05873e33d4a`, ALIENS `b8fab0069157a4c9`.

## 6. Design decisions for the implementation (do not re-litigate without a reason)
1. **A'**: `src/core/md/MasterFx.{h,cpp}`: owns the second DSP (JIT config as `VoiceEngine::configureJit()`:
   `interruptRegionIsCode`, `dynamicFastInterrupts`, `aguSupportBitreverse`, `linkJitBlocks = false`; interpreter cache when
   `!g_useJIT`), `reset()` loads section 2, installs the stub, runs init, switches DMA0 off, primes `Y:$15a`. The
   `Firmware` reference must outlive it (as `VoiceEngine`'s).
2. **Parameters through the OS's own code**, not by re-deriving the arithmetic. `HostModel` gets `setMasterFx(i, v)` for the
   32 bytes (pokes the targets `$1000f6c + i`; the 48-word slew `kLevelSmooth` already smooths them each tick, `HostModel.cpp:258`)
   and, in `tick()`, runs the group `tickCount & 3` (`$265404`, `rts` patched over `$20ba4c`, call `$20b4a2`) and hands the
   words to the engine, which writes them into DSP1's Y memory at the next block boundary - the hardware's cadence.
   `MachineRunner` gets a small `callUntil`/patch helper for this. Initial state: prime the smoothing (run the slew to a fixed
   point and all four groups once) when the engine is created or the OS changes, so the first block is already at the kit's
   master settings; later changes glide as on the hardware.
3. **Authentic quirks are kept**: the OS slew ends 3 units below the target when approached from below (`(3 old + t) >> 2`,
   measured: 8189 for 8192), so a parameter's table index can be one lower than the knob suggests; a TIME change glides
   (pitch sweep), a tempo change too (TIME is tempo-relative: `HostModel::setTempo` already pokes `$100150c`).
4. **Master output**: frame channels 2 and 5 after the pass (so routes 0/1 are added exactly as on the hardware) become the
   plugin's main bus when the master is on; the reverb and delay send buses stay as they are (dry sends, before the master).
   `dryMute` tracks still feed the sends and so the master's echo and reverb.
5. **Latency**: the dry path through the master is 18 frames (0.4 ms at 44.1 kHz) later than the track buses and sends. Align
   them (delay the other streams by 18 engine frames when the master is on) and add the 18 frames to the reported latency
   rather than letting the buses drift; `getTailLengthSeconds` should become ~6 s when the master is on (echo ring 2.2 s with
   feedback, reverb decay up to 3 s).
6. **Bypass**: host parameter `master_fx` on/off (default on), when off the old dry main bus and the old hashes.
7. **Host parameters**: 32 integers 0-127 with the hardware's names, in kit order (ids appended, never reordered):
   `fx_rev_dvol` ... `fx_dyn_mix`, grouped "Master FX" in four sub-groups. `kitToParameters`/`parametersToKit` carry them
   (the kit already stores the bytes; the round trip of all 64 preset kits stays byte-exact), `applyKit` sets them, state
   schema 3 with defaults from the init kit for older states (`OneProcessor.cpp:45-49` is the init kit's block).
8. **Editor**: four `KnobPage`s of 8 cells (they fit exactly), in the 2x2 of the track pages: ECHO | REV over EQ | DYN, in a
   MASTER tab beside GLOBAL; labels in the LCD font like the track pages; master on/off in GLOBAL.
9. **The whole-DSP1 loop and the stage-stopping tools live in `tests/` (or `tools/`) as the oracle**, not in the engine.

## 7. Step plan with acceptance criteria
Order matters: steps 1-3 are engine-only and independent of the plugin; steps 4-6 touch `OneProcessor`/`OneParams`/`OneEditor`
and should wait for the sessions currently editing the plugin to merge. Suggested clew split: engine (1-3), plugin (4-5),
editor (6), then optional RAM/idle (7) and B (8).

1. **`MasterFx` engine stage.** `MasterFx(const fw::Firmware&)`, `setWords(const uint32_t* words38)` /
   `writeGroup`, `process(const Mixer::Output&, std::array<std::array<int32_t,2>,32>& out)` (+ the frame in/out),
   `faultReason()`, `reset()`. Direct memory pointers for the 384 pokes.
   *Acceptance:* (a) ctest `master` (skipped without `MD_OS_SYX`): JIT vs interpreter on 2 `MasterFx` instances, >= 3000
   blocks of random kits and signals including 0/127 and full-scale, 0 differences; (b) master-only output equals the whole-DSP1
   reference loop on the golden pattern for 4000 blocks and 3 kits (the spike's `mfx12` logic, kept as `tests/Dsp1Reference`);
   (c) a block after warm-up takes < 25 us (JIT), and no allocation or JIT compile happens on a live block; (d) `md-hash`
   `04ef1db4fe767721` unchanged.
2. **Master parameters in `HostModel`.** `setMasterFx`, group-per-tick pipeline, priming, `MachineRunner::callUntil`.
   *Acceptance:* the 38 words of the test vector in 1.3 reproduce when the smoothed values are set exactly; a table test
   of all 16 preset kits (words after convergence, recorded as a checksum); the slew test (targets from below end 3 below,
   from above exact); a tempo change moves `Y:$150/$151` only; the group cadence (one group per tick) is tested.
3. **Engine integration.** `EngineT` owns an optional `MasterFx`; `Output::master` (L/R + final frame); works for
   `ParallelEngine`. *Acceptance:* `md-hash` `04ef1db4fe767721` (JIT, `MD_USE_JIT=OFF`, `MD_GROUPS=2/4`), kit 0
   `f5c618b7df5550fa`, `MD_SWEEP=1` unchanged; a new ctest `golden-master` records the master-output hash of the golden
   pattern with fixed synthetic master bytes (dry-ish and wet sets) and kit master bytes when `MD_KITS_SYX` is set, identical on
   JIT and interpreter; `md-hash` prints it with `MD_MASTER=1`.
4. **Kits and parameters.** `applyKit` applies the master blocks; 32 host params + `master_fx`; kit <-> parameters round trip;
   state schema 3. *Acceptance:* `ctest` kits: all 64 preset kits still round-trip byte for byte, a kit's master bytes survive
   load -> parameters -> `parametersToKit`; `plugin-state` with an old (schema 2) state restores the init kit's master
   bytes; the hash `da62047690565e38` holds with the master off.
5. **Plugin audio path.** Master out replaces the main bus when on; stream alignment; latency; tail; `master_fx` off = the old
   path. *Acceptance:* `plugin-render` `a28b9dc178776939` and `14291ff9302212d4` hold with the master off, new recorded hashes with it
   on (both note maps); an impulse arrives at the same engine frame on the main bus, a track bus and the send buses; the bench
   (`md-plugin-check --bench 20`) stays <= 10 % of a core at 44.1 kHz with no callback over budget; the 16 warm-up blocks
   include the master (no 13 ms stall on the first live block).
6. **Editor pages.** MASTER tab with ECHO/REV/EQ/DYN `KnobPage`s on the new parameters, `master_fx` switch in GLOBAL.
   *Acceptance:* `--check-editor` covers the new cells (every cell turns its parameter); `--snapshot` PNGs reviewed by the
   owner; parameter display names are the hardware's.
7. **Optional: RAM and idle.** Break down the second DSP's footprint (below) and try to cut it; a master idle skip needs an
   analysis of which state words are time-shift invariant (the echo's write phase `Y:$159` always advances), to be done with 0013.
8. **Optional: translation (B)** with `Dsp1Reference` as the oracle, if step 7 cannot get the RAM down or many instances matter.
9. **Docs.** `docs/DEVELOPMENT.md` (decisions, the numbers above, open work), `MdSysex.h:75-77` and `KitApply.h:13` comments, the
   README's feature list.

## 8. Risks and open questions
- **RAM.** Measured with `task_info`: a bare `Memory` + `DSP` is 193 + 81 = 274 MB `phys_footprint` regardless of the configured
  P/XY sizes (checked 0x150000/0x140000 against 0x200000/0x800000); a booted DSP1 is 376 MB (JIT) / 402 MB (interpreter); the
  spike process with the OS image and everything else 541 MB. The cause is not found; the suspect is
  `MemoryBuffer` (`ext/dsp56300/.../memorybuffer.cpp`), which maps the whole 16 M-word DSP address range three times (X, Y, P)
  onto a ~38 MB shared backing store, so the 193 MB after `Memory` is aliased pages counted per mapping or pre-faulted. Confirm
  with `vmmap`/`footprint` (denied in this sandbox) and, if it is that, fix it in `ext/patches` for both DSPs (the voice DSP
  would gain too: DEVELOPMENT.md quotes 420-500 MB per instance).
- **No end-to-end oracle.** There is no flash image, so gearmulator-md-mm cannot run and the master has not been compared
  with the full-system emulation or hardware. Fidelity rests on running the unchanged DSP1 program and the unchanged OS
  parameter code; the open differences are timing only (tick cadence, the order in which groups reach the DSP, the initial
  smoothed state).
- **Level and defaults.** Master on changes the plugin's level (+5.8 dB at EQ GAIN 127 with dynamix open, but -2.6 dB with the
  kit defaults of the dynamix, whose OUTG is 0): decide the default and the init kit's dynamix (`OneProcessor.cpp:45-49`).
- **Control machines** (CTR-RE/GB/EQ/DX drive the master parameters from a track) are not modelled; the plugin offers neither.
- **Idle cost.** The master runs always (1.7 %); increment 0013's idle target (< 0.5 % of a core) needs a skip, which is not
  exact-trivial (see step 7).
- **x64 JIT and no-JIT builds.** DSP1 has been run on the aarch64 JIT and the interpreter only.
- **First block / tail.** Tail length is now audible (echo + reverb): the host's bounce needs `getTailLengthSeconds`.

## 9. Reproducing the spike
All tools are in the scratch space (`.../scratchpad/mfx-spike`, outside the repository, built against the repo's `mdcore` and
the main checkout's `ext/dsp56300` and `ext/mc68k` with `-DMD_DSP56300_DIR/-DMD_MC68K_DIR`; the OS file is never copied):
`dis2` (disassembles section 1/2 P and dumps X/Y records), `dis68` (the ColdFire OS through Musashi's disassembler, 68040 mode;
`find32` finds constants), `dsp1boot` (boot experiment), `mfx.h` (the harness: `Dsp1`, stub, `masterWords`), `mfx1` (OS words and
cost), `mfx2` (impulses, latency, gain, DVOL, reverb mono), `mfx5` (whole DSP1 vs native chain), `mfx6` (per-stage counts),
`mfx7` (JIT vs interpreter), `mfx8` (stub timing), `mfx11` (integrated golden pattern with master), `mfx12` (A' vs A), `mfx10`
(the OS slew), `memtest` (RAM). Key facts to rebuild any of them: the patch list in section 3, the stub
(`movep #>1,x:<<$ffffc7`; `jclr #0,x:<<$ffffc3,*`; `movep x:<<$ffffc6,a`; `jmp $3c`; `$9a8` -> `jmp $c00`), and in the 68k
runner `poke16(0x20ba4c, 0x4e75)`, `poke32(0x100150c, BPM*24)`, `poke16(0x1000d9c + 2i, v << 7)`, `poke32(0x265404, group)`,
`call(0x20b4a2)`, then read longs at `$1001af4`, `$1001b18`, `$1001b44`, `$1001b74`.

## 10. Engine side, implemented (steps 1-3 and the RAM half of step 7; 2026-10-12, branch `worktree-agent-acdce1a701849f20b`)
Everything of sections 6 and 7 steps 1-3 was done as specified; what is below is what exists, what was recorded, and what the
implementation found that the spike did not know.

### What exists
- `src/core/md/MasterFx.{h,cpp}`: `MasterFx(const Firmware&, bool interpreter = false)`, `available(fw)`, `reset()`,
  `setWords(38)` / `writeGroup(group, words)` (the 38 words in `master::kGroup` order: ascending Y address, OS group g = 0 echo,
  1 dynamix, 2 EQ, 3 reverb), `snapEcho()`, `process(const Mixer::Output&, Mixer::Stereo&)`, `warmUp(wordSets, n)`,
  `compiledBlocks()`, `readX/readY/dsp()` for tools, `faultReason()` (a sticky JIT failure or an overrun block).
  `namespace master`: `kParams` 32, `kWords` 38, `kGroups` 4, `kGroup[]`, `kDefaultParams` (the cleared kit), `kDryLatency` 18.
- `HostModel`: `setMasterFx(i, v)` / `masterFx(i)`, `masterWords()` + `takeMasterGroups()` (a bit per group worked out since the
  last call), `primeMasterFx()`, `computeMasterWords(bytes, bpm, out)` (exact values, the host left as it was),
  `masterWarmUpWords(n, out)`. The tick runs group `tickCount & 3` after the OS's slew (`$100029e`) through
  `MachineRunner::callUntil($20b4a2, $20ba4c)` (nothing patched into the OS image; D2-D7/A2-A6 and the tick counter `$265404` put
  back). A new `HostModel` starts on `master::kDefaultParams`, settled by the OS's slew, all four groups due.
- `EngineT` (`MdEngine.h`): `EngineOptions{bool master = true}` as the last constructor argument of both constructors,
  `Output::master` (`Mixer::Stereo`, 24 bit), `hasMaster()`, `master()`, `primeMaster()`, `masterBypass`, `voiceOutput()`,
  `masterUs`, `kMasterWarmUpSets` = 24; `fault()` is the voice DSP's else the master's; `render()` is false on either's fault.
  Order inside `render()`: voices, tick (words worked out), track fx, mixer, then the pending word groups to the master DSP,
  then the pass. `ParallelEngine` is the same code.
- `library::MasterFx::bytes()` / `setBytes()` (the kit's four blocks <-> the 32 bytes); `MdSysex.h` / `KitApply.h` comments.
- Tests: `tests/Dsp1Reference.{h,cpp}` (the oracle: DSP1's whole main loop), `tests/test_master.cpp`; ctest `master` (`mfx_*`),
  `master-host` (`mhost_*`), `golden-master` (`gmaster_*`), `master-ram` (`ram_*`). `md-hash` `MD_MASTER=1|dry|wet|kit:N`.
  `GoldenPattern.h`: `kMasterDry`, `kMasterWet`, `Pattern::hashMaster`, `render(..., &masterHash)`.
- RAM: `ext/patches/0002-dsp56300-md-memory.patch`, `DSP56300_AUDIO_RINGBUFFER_SIZE` in `src/core/CMakeLists.txt`.

### Acceptance, as recorded (JIT build; the interpreter build gives the same numbers where numbers are not timings)
Step 1 (`ctest master`, OS file only):
- (a) `mfx_jit_vs_interpreter`: 3200 blocks, 80 random kits (bytes 0 or 127 one time in four each, tempi 60-240, signals per bus:
  silence, noise, full-scale square, impulses), **16 764 800 words compared, 0 differences** (outputs, X:$000-$1ff, the frame, the
  buffers, Y:$000-$1c9, the dynamix ring, a stride sample of the echo and reverb rings). Skipped on `MD_USE_JIT=OFF`.
- (b) `mfx_vs_whole_dsp1_loop`: EFM, BOOGIE, ALIENS x 4000 blocks of the golden pattern (with master byte edits at 1/3 and 2/3 of
  the run: echo TIME up and down, FB, MONO, gate on, EQ PG, compressor threshold), frame channels 2 and 5, X:$180-$1bf and
  Y:$150-$1c1 after every block: **2 904 000 words compared, 0 differences** (968 000 per kit).
- (c) `mfx_block_cost_and_no_compile`: warm-up 40-46 ms, the cold first block 7.3 ms; 4000 live blocks over 100 random kits:
  **median 9.6-10.9 us, mean 10.0-11.0 us, p99 12-14 us, max 30-84 us (a scheduling outlier); 0 mallocs in all 4000**
  (macOS `malloc_logger`; a JIT compile allocates). On the interpreter build (LTO): median 74 us, p99 90 us, 0 mallocs (the spike measured 143 us without LTO); `md-hash` 81 us a block.
  `gmaster_no_compile_while_playing`: `compiledBlocks()` (compiled entry points) = 672 after construction and still 672 after the golden pattern and 6000
  blocks of changing kits and tempi.
- (d) `md-hash` `04ef1db4fe767721` on the JIT, the interpreter, `MD_GROUPS=2` and `4`, with the master stage built or not.
Step 2 (`ctest master-host`): the 38 words of the 1.3 test vector reproduce exactly (EFM, 120 BPM, smoothed values `v << 7`;
note EFM's DVOL is 105, the spike's transcribed byte list had 0 there); the 16 preset kits' converged words (from the cleared
kit's settled state, 125 BPM) hash to `3f50575c608c9c35` (FNV-1a over 16 x 38 words); the slew: 96 -> 127 ends at 127 x 128 - 3,
127 -> 10 at exactly 10 x 128, 10 -> 11 at 11 x 128 - 3; a tempo change 125 -> 90 moves only Y:$150, 90 -> 30 (TIME 100: the
delay no longer fits the ring) moves only Y:$150 (= $20) and Y:$151, back to 125 restores both; the cadence: groups 0, 1, 2, 3, 0 ... one per tick, an EQ GAIN edit shows
in Y:$179 on the third tick; a new engine's first block has Y:$150 = the primed word and Y:$15a = Y:$150.
Step 3 (`ctest golden-master`, `md-hash`): master-output hashes of the golden pattern, **identical on the JIT and the interpreter
build** (the same constants in both):

| master bytes | `MD_MASTER=` | master hash |
|---|---|---|
| cleared kit (`master::kDefaultParams`) | `1` | `3c37e206683bf50e` |
| dry-ish synthetic (`kMasterDry`: reverb/echo levels 0, EQ flat, dynamix open) | `dry` | `fe784d9f02fa30b2` |
| wet synthetic (`kMasterWet`) | `wet` | `9b8a53ca6fae15fe` |
| kit 0 TRX | `kit:0` | `5f4b45659f477ab7` |
| kit 1 EFM | `kit:1` | `1222389a11b38fe6` |
| kit 9 BOOGIE | `kit:9` | `2abcba9c5e9bf547` |
| kit 13 ALIENS | `kit:13` | `b892b2c5f5a1e43b` |

Next to each the dry hash printed is `04ef1db4fe767721`; kit 0 on a fresh engine `f5c618b7df5550fa` (ctest `kits`); `MD_SWEEP=1`
identical to the run before the change, per machine, on both builds. `gmaster_engine_is_the_chain_by_hand`: the engine's master
hash equals the native chain + a fresh MasterFx (no warm-up) fed the host's groups by hand; `gmaster_parallel_engine`: the same
hash with `ParallelEngine` (2 voice groups); `gmaster_bypass`; `gmaster_disabled_is_silent`. `mfx_dry_latency`: an impulse comes
out 18 frames later, on the same channel, for the cleared kit, dry-ish and wet.
The candidate values of section 5 (static words, no slew) are reproduced exactly by `HostModel::computeMasterWords` +
`MasterFx` on the golden pattern: EFM `91b5d84165c0c467` (JIT and interpreter), TRX `76c6ee93aebc899e`.

### Cost and RAM, as measured here (M-series, JIT)
- `md-hash` render of the 12 s pattern: 590 ms without, 800 ms with the master stage (master 195 ms = 11.8 us a block); OS tick
  71 ms (the master group calls are in it). Interpreter: master 1334 ms of 3771.
- `Engine` construction: 7 ms without the master section, 60 ms with (boot 6 ms, warm-up ~50 ms).
- phys_footprint (`task_info`), one process per configuration:

| | before | after |
|---|---|---|
| bare `Memory` (P 2 M, XY 8 M, bridge 128 k) | 192.1 MB | 0.2 MB |
| `Peripherals56303` (two ESSI ports) | 80.7 MB | 0.0 MB |
| booted `VoiceEngine` | 357.8 MB | 86.5 MB |
| booted `MasterFx` | 361.8 MB | 90.5 MB |
| `Engine`, no master | 358.6 MB | 87.3 MB |
| `Engine` with master (after 3000 blocks) | 837.3 MB | 303.4 MB |

Interpreter build after the change: master DSP 118 MB, voice DSP 108 MB (its opcode cache, 48 bytes per P word, is 100 MB).
Causes (the spike's suspicion, `MemoryBuffer`'s three aliased 16 M-word mappings, was right and the mechanism is not the mappings
but the `mlock()` that `MmuHelper::mapRegion` puts on each: wiring faults in every page of every alias): (1) the `mlock`, 192 MB
per DSP; (2) the ESSI audio ring buffers, 2 x 2 x 32768 frames = 80 MB per DSP (dsp56300 has `DSP56300_AUDIO_RINGBUFFER_SIZE`
for hosts that do not stream audio through them). Both removed; the audio is bit-identical (all hashes above).
Best lead for the rest: the JIT keeps one block chain per DSP mode (`JitDspMode`: the AGU registers' addressing modes and the
scaling/saturation bits), each with an array of 8 bytes per P address up to the highest P word ever written (`Jit`'s
`m_maxUsedPAddress`: 1 326 698 for DSP1 because its init code sits at $143e4a: 10.6 MB per chain). The master pass visits 17 modes
(its `move #$1f,m4/m5` and the like): ~130 MB of the 303. `DSP::execJit` indexes the array unchecked (`m_jitEntries[pc]`), so
growing it on demand needs a bounds check in the dispatch; on macOS the `MmuArray` lazy path is disabled (EXC_GUARD). A
change to the program's modes is not possible (it is the OS's code). Not done.

### What the implementation found (for whoever touches this)
- **Warm-up and a clean state.** `reset()` after a warm-up is not a fresh instance: DSP1's init does not clear Y:$000-$03f (the
  reverb return scratch) and the reverb gate's state words in the table area (X/Y:$14397b-$14398a), 269 Y and 15 X words differed.
  `warmUp()` therefore snapshots DSP memory (X view $200000 words, Y and P internal $20000) and puts it back by the DSP's own writes;
  a reset would also invalidate the JIT blocks at the end of the pass ($9a8/$9a9 are patched). `mfx_warmup_leaves_a_fresh_state`:
  600 blocks, 3 105 000 words, 0 differences between a fresh and a warmed instance.
- **JIT coverage needs sweeps.** The data-dependent branches compile lazily; the compressor's knee region (P:$927-$92b) is only
  reached when the dynamix's envelope moves between its two thresholds, e.g. on the returns of the sends while the dry bus is silent,
  and a signal that only switches between a few levels never does. The warm-up draws a kind of signal per bus and per word set
  (noise rising from -102 dB or falling, steady, squares, impulses, silence); 24 sets x 140 blocks compile all 672 entries
  (a scratch coverage run over 6 seeds x 4000 random live blocks, and the tests, found no block left).
- The tail of the OS's tick routine ($20b4a2-$20ba4c) reads absolute addresses only and sets D5, D6, A0 and A1 itself (checked in the
  disassembly), so `callUntil` from the middle of the routine is exact; a patched `rts` is not needed.
- A tempo change moves Y:$151 only when the delay no longer fits the ring (TIME x 82 688 / BPM > 96 987 samples); below that
  Y:$151 stays $4001.
- `EngineT` defaults the master section on; `md-hash` (except `MD_MASTER`), `md-kits` and the sweep build the engine without it. A
  plugin that constructs `Engine(fw, os)` now gets the second DSP (+90 MB, +55 ms, +12 us a block) whether it uses `Output::master` or not.

## What the engine gave the plugin side (written before steps 4-6; they are done, section 11)
- **Apply a kit's master bytes**: `applyKit` does not (it would move nothing dry, but the plugin has its own parameter path): per
  kit load `for i in 0..31: host.setMasterFx(i, kit.masterFx.bytes()[i])`, then `engine.primeMaster()` to start at the kit's
  settings without the glide (the hardware glides on a kit change, as it does on a knob turn; `primeMaster` also snaps the echo's
  delay offset). `Kit::masterFx.setBytes()` for the way back (`parametersToKit`). Order inside a kit in the host parameters: the
  dump's (reverb 0-7, echo 8-15, EQ 16-23, dynamix 24-31); the editor's pages are ECHO, REV, EQ, DYN (section 2).
- **Calls are the same as `setParam`/`trigger`**: `setMasterFx` and `primeMaster` between blocks, under the engine lock, never
  during a render; `process` runs inside `render()`. Nothing allocates on the audio path (the master adds none; run `plugin-rt`).
- **Default state of a new engine**: `master::kDefaultParams` (= the init kit of `OneProcessor.cpp`), settled, so the first
  block is at those settings. `HostModel::setTempo` reaches the echo through the group cadence (a tempo change glides the echo
  for at most 4 ticks + its own glide, as on the hardware); set the tempo before `primeMaster()` when the sound should start at it.
- **Master on/off (`master_fx`)**: `engine.masterBypass = true` skips the pass (12 us a block) and zeroes `Output::master`; the
  words keep flowing and the DSP stands, so switching back on continues. `EngineOptions{false}` builds no second DSP at all (RAM).
  The plugin hashes `da62047690565e38`, `a28b9dc178776939`, `14291ff9302212d4` are of `Output::mix.main` and do not move with the
  master built; with the master as the main bus they change by definition: record a second set (both note maps).
- **Main bus with the master on**: `Output::master` replaces `mix.main` (it contains the dry bus, the echo and the reverb, and
  tracks routed to outputs A/B: frame channels 2/5). It is not level-neutral (the spike measured: EQ GAIN 127 is +5.8 dB with the
  dynamix bypassed, about -2.6 dB for a quiet sine with the cleared kit's dynamix) and the dry path is `master::kDryLatency` = 18 frames behind: delay the track buses and
  the send buses by 18 engine frames (the resampler's latency stays on top), add them to the reported latency, and set
  `getTailLengthSeconds` to ~6 s while the master is on (echo ring 2.2 s with feedback, reverb decay up to 3 s). The reverb and
  delay send buses are the dry sends (before the master), as designed.
- **Warm-up**: the engine's constructor already compiled the master (`kWarmUpBlocks = 16` silent blocks in the processor's warm-up
  run through it harmlessly); no 13 ms stall on the first live block.
- **Not done / for later**: control machines (CTR-RE/GB/EQ/DX), the idle skip of the master pass (step 7, with 0013), B
  (a C++ translation; with `Dsp1Reference` as oracle it is still possible, and RAM is no longer the argument for it: the
  master DSP is 90 MB + its JIT chains), the x64 JIT on DSP1 (never run), the README's feature list.

## 11. Plugin side, implemented (steps 4-6 and 9b; 2026-10-12, branch `worktree-agent-a7625381f929937c1`)
Sections 6 (decisions 4-8) and 7 (steps 4-6) were done as specified, with increment 0019 (the machine parameters' meta
flag) alongside. The engine (`src/core`) was not changed: its API had everything (`setMasterFx`, `primeMaster`,
`masterBypass`, `Output::master`, `master::kDryLatency`, `compiledBlocks`, `library::MasterFx::bytes()/setBytes()`).

### What exists
- `OneParams.h`: after the 4 globals, group "Master FX" (id `MasterFX`): `master_fx`, then the 32 bytes in the kit
  dump's order in sub-groups REV, ECHO, EQ, DYN (ids `MasterFXREV` ...): `fx_rev_dvol fx_rev_pred fx_rev_dec fx_rev_damp
  fx_rev_hp fx_rev_lp fx_rev_gate fx_rev_lev`, `fx_echo_time fx_echo_mod fx_echo_mfrq fx_echo_fb fx_echo_filtf
  fx_echo_filtw fx_echo_mono fx_echo_lev`, `fx_eq_lf fx_eq_lg fx_eq_hf fx_eq_hg fx_eq_pf fx_eq_pg fx_eq_pq fx_eq_gain`,
  `fx_dyn_atck fx_dyn_rel fx_dyn_trhd fx_dyn_rtio fx_dyn_knee fx_dyn_hp fx_dyn_outg fx_dyn_mix`; names `REV DVOL` ..
  `DYN MIX`, `MASTER FX`. 533 parameters (index 500 = `master_fx`, 501-532 the bytes). `kMasterPageBlocks` = the
  editor's page order (echo, reverb, EQ, dynamix).
- `MdOneProcessor`: `masterParameter(i)`, `masterOnParameter()`, `setMasterFx(i, v)` / `masterFx(i)`, `setMasterOn` /
  `masterOn`, `masterActive()` (on and the engine has the section), `conversionLatency()` (now the whole reported
  latency of the current mode), `masterCompiledBlocks()` (tests), `kMasterLatency`, `kMasterTailSeconds` = 6,
  `kStateSchema` = 3. `defaultKit()` takes its master block from `master::kDefaultParams`.
  - Parameters -> engine: `applyParameters` passes changed bytes to `HostModel::setMasterFx` every block, with
    `master_fx` off too (the DSP stands, its words stay current, switching on plays what the knobs show).
  - Prime: `loadKit` (also `loadDefaultKit`), `setStateInformation` and `loadEngine` set an atomic flag after the
    parameters; `processBlock` consumes it after `applyParameters` and the tempo and calls `EngineT::primeMaster()`. On the
    audio thread: it allocates nothing (`plugin-rt`) and adds ~65 us to one callback (`plugin-master`).
  - Audio path: per host block `m_masterMode = master_fx && hasMaster()`, `masterBypass = !m_masterMode`. In
    `renderEngineBlock` every stream but the main pair (sends, track buses) goes through an 18-frame delay line, fed in
    both modes; in master mode those streams are played delayed and the main pair is `Output::master`. The resampler
    reads in master mode at engine time `(m - L) x 44100 / rate + 18` with `L = ceil((H + 1 + 18) x rate / 44100)` (H = 35,
    the kernel's half length), so the output is the sound exactly L host samples late: the reported latency is a whole
    number, 18 / 59 / 118 at 44.1 / 48 / 96 kHz (0 / 40 / 79 off). At 44.1 kHz nothing is converted and the latency is
    the 18 frames.
  - Latency reporting: `prepareToPlay`, `loadEngine`, a restored state and any message-thread change of `master_fx`
    (its own parameter class, `MasterOnParam::valueChanged`) call `updateLatency()`; a change from another thread (host
    automation) is picked up by the processor's 30 Hz timer. `getTailLengthSeconds()` is 6 s while the master is active.
  - Kits: `kitToParameters` sets the 32 bytes (`Kit::masterFx.bytes()`), `parametersToKit` writes them back
    (`setBytes`), keeping a kit's byte where the parameter holds its clamped value, as for the other fields.
  - State schema 3: the parameters (with `master_fx` as 0/1) and the KIT as before. A schema 2 state restores the init
    kit's master bytes and `master_fx` on (decision 7 taken literally; a schema 2 state's KIT does carry its kit's master
    bytes, which that session never heard: using those instead would be one line in `setStateInformation`).
  - The loadEngine warm-up also checks that `Output::master` is silent.
- Editor (`OneEditor.{h,cpp}`): a MASTER tab in the strip after GLOBAL (`TrackStrip::kMaster`); `OneView::showMaster()`,
  `masterShown()`, pages `Echo Rev Eq Dyn` (`OneView::kMasterPages`) placed in the track pages' 2x2: ECHO | REV over
  EQ | DYN, titled ECHO, REV, EQ, DYN, a header block MASTER; the cells are dials with the hardware's labels; the EQ's
  LG, HG and PG show raw - 64 (flat at 64, where the OS's parameter code branches, section 1.3), the rest raw. The titles
  carry an OFF badge while `master_fx` is off. GLOBAL's bottom row: MASTER (the OFF/ON switch icon, `Icons::Toggle`;
  a drag of 20 px flips it, a click lists OFF/ON, a double-click resets it to ON) in the last cell; INIT KIT over EXPORT
  across the other three, which share one frame (`KnobPage::joinLeft`). Strip: GLOBAL 39 px and MASTER 42 px (their
  names in bold: 35 and 39 px), the 16 track keys in the remaining 204 px, 13 px for tracks 10-16 (a two-digit number
  is 9 px in bold), 12-13 for 1-9 (was GLOBAL 45 and 15 px per track).
- Checks (`src/plugin/Check.cpp`): new `--check-master` = ctest `plugin-master` (below); `--check-render` and
  `--check-state` run with `master_fx` off (`RenderOptions::masterFx`, default false) and check the master layout, the
  kits' master bytes, schema 3 and schema 2; `--check-rt` adds master edits, a kit load (prime) and master off/on;
  `--bench` runs the processor with the master on at the three rates and off at 44.1 kHz; `--check-editor` and
  `--snapshot` cover the MASTER tab.

### Acceptance, as recorded (OS 1.63, kits `MD_presets_1_50.syx`, M-series Mac, JIT build unless said)
Step 4:
- `plugin-state`: 533 parameters, ids in order (`master_fx`, `fx_rev_dvol` .. `fx_dyn_mix`), names, ranges 0-127, defaults
  = the init kit's bytes, groups T1..T16, Global, Master FX (MASTER FX + REV, ECHO, EQ, DYN of 8); every kit's 32 master
  bytes are in the parameters after `loadKit` and back in `parametersToKit` (64 kits, 16 different master settings); all
  64 kits still export byte for byte; an edit of echo MFRQ shows in the export, the other blocks unchanged; a schema 3
  session (master_fx off, echo MOD 5, dynamix MIX 64) restores every parameter and the same state bytes; the same state
  as schema 2 (500 PARAMs) restores the init kit's master bytes and master_fx on. **`da62047690565e38` holds** with the
  master off (= `applyKit` on the engine after 16 warm-up blocks; `f5c618b7df5550fa` without them), also after the state
  round trip.
Step 5 (`plugin-master`; `plugin-render` keeps **`a28b9dc178776939`** and **`14291ff9302212d4`** with the master off):
- master_fx on by default: latency 18 at 44.1 kHz, tail 6 s; off: 0 and 0; 48 kHz 59 (off 40), 96 kHz 118 (off 79).
- The first live callback after an OS load (512 samples, 16 engine blocks, nothing playing): 244 us (median of the next
  64: 241 us); the master DSP's 672 compiled JIT blocks unchanged, and unchanged over the whole regression render: the
  engine's warm-up compiled them; the processor's 16 warm-up blocks run the master too (and its output is silent).
- **Master-on regression hashes: chromatic `ece6201f17cf1b81`, white keys `7fd32e31f89ea574`** (peak -5.47 dBFS; the dry
  render peaks at -2.86); 61-sample host blocks give the same samples. Identical on the interpreter build
  (`-DMD_USE_JIT=OFF`), with every other `plugin-master` check.
- Impulse alignment: "an impulse triggered at frame 1007 lands at frame 1029 (engine block boundary + the voice) on the
  main bus L/R (1029/1029), track 2's bus (1029), the reverb send (1029) and the delay send (1029) with the master on, and
  on the dry main bus with it off (1029)" (GND-IM on tracks 1 and 2, DEL and REV 127, track 2 on its own bus).
- With every bus on (tracks 1 and 10 on their own): the 16 track buses and both send buses with the master on are the
  master-off render's sample for sample (20 channels) once the reported latency is taken off.
- 48 and 96 kHz with the master on: the 44.1 kHz master render through the converter, max difference 0.000000000 over
  215 666 / 431 332 samples, latency 59 / 118 taken off.
- Reverb LEV 0 and EQ GAIN 64 change the master render (`cf4637dbb16fdcd6`) and not the dry one (`a28b9dc178776939`).
- master_fx switched 7 times while playing: the reported latency follows (0 18 0 18 0 18 0), no fault; a kit load primes
  the master in the next callback: at most ~454 us over 4 loads against a 389 us median before (256 samples, 5805 us of
  budget; the interpreter build 1862 against 1790 us).
- **Kit 0 (TRX) with the master on: `288d02a442ac6b0b`**, the same as the engine's master output with `applyKit`, the
  kit's master bytes and `primeMaster` after 16 warm-up blocks, the sends 18 frames behind it (`directKitTestMaster`).
- `plugin-rt`: 0 allocations outside the JIT at 44.1/64, 48/480 (every bus), 96/256, 44.1/1024 (every bus), with the
  master edits, a kit load (prime) and master off/on in the sequence; the second pass compiles nothing.
- Bench (`md-plugin-check --bench 20`, three runs, load average 3.5-5, another process spinning one core in the second):

  | | run 1 | run 2 | run 3 |
  |---|---|---|---|
  | engine alone (with the master) | 8.8 % | 9.0 % | 8.9 % |
  | processor 44.1 kHz, master on | 8.8 %, 0 of 13091 over | 9.0 %, 1 of 13091 over (max 1619 us, 99.9 % 280 us) | 8.9 %, 0 over |
  | processor 48 kHz, master on | 9.1 %, 0 over | 9.1 %, 0 over | 9.1 %, 0 over |
  | processor 96 kHz, master on | 9.4 %, 0 over | 9.2 %, 0 over | 9.3 %, 0 over |
  | processor 44.1 kHz, master off | - | 7.2 %, 0 over | 7.2 %, 0 over |

  99.9th percentiles 150-330 us against budgets of 1451 / 1333 / 667 us. The one callback over budget came with the
  engine-alone reference's worst block at 549 us in the same run (it reads no parameter): preemption, not the master
  (its live block is 10-14 us at p99, section 10).
Step 6 (`plugin-editor`, `--snapshot`):
- The MASTER key shows ECHO | REV over EQ | DYN with the hardware's labels (TIME MOD MFRQ FB FILTF FILTW MONO LEV; DVOL
  PRED DEC DAMP HP LP GATE LEV; LF LG HF HG PF PG PQ GAIN; ATCK REL TRHD RTIO KNEE HP OUTG MIX), every one of the 32
  cells turns its own byte and no other (a 20 px drag: +-16); a host change moves a knob; LG 64 shows 0, PG 70 +6;
  GLOBAL's MASTER switch: OFF (the master pages say OFF), dragged up ON in one gesture (latency 18, badges gone), down off
  (latency 0), a double-click back to ON; a track's key leaves the tab; every page renders.
- Snapshots reviewed (100 %, 200 %, and 50 %): master, master-off, kit-master (FRANSISCO: echo TIME 16, FB 28, a gated
  reverb GATE 0 / LP 57, the compressor ATCK 49 REL 105 TRHD 66 RTIO 99 KNEE 30 HP 71 OUTG 14), global, track,
  unconfigured (the stand-in face). Fixed after looking: MASTER fell back to the small face in a 38 px key (now 42 px);
  the two-digit track numbers were cramped on 12 px keys (now 13 px for 10-16); the cell dividers showed through the
  gaps between INIT KIT and EXPORT (the three cells now share one frame); EXPORT sat a pixel low. The stand-in face's
  two-digit numbers use the small face, as before. No PNG committed.
Other:
- ctest: 16 of 16 (the 15 before + `plugin-master`); `md-hash` `04ef1db4fe767721`.
- Standalone (scratch `MD_SETTINGS_DIR`): started with the OS, ran 28 s (4 % CPU at 14 s, RSS 400 MB), quit on SIGTERM; its
  settings file unchanged. The window was not looked at; the snapshots stand in.
- auval (increment 0019, on a scratch copy `aumu Mdz2 Mdrm` with this tree's 533 parameters): `-v`, `-oop -v`, `-strict -v`,
  `-oop -strict -v` all AU VALIDATION SUCCEEDED; the copy was removed again.

### For a human, by ear
- The master section against the dry path (`master_fx` off): it is not level-neutral (the cleared kit's dynamix, a 0 dB
  limiter with OUTG 0, takes quiet material down ~2.6 dB; EQ GAIN 127 alone is +5.8 dB), so A/B at matched loudness.
- The preset kits' master settings: their reverb (DVOL, GATE, LEV 96-111), echo (TIME, FB, FILTF/FILTW) and the kits with
  a working compressor (e.g. FRANSISCO, BOOGIE): do they sound like the hardware's?
- An echo TIME or tempo change glides (pitch sweep) as on the MD; a kit load does not (primed); turning a master knob
  glides in over a few ticks.
- Switching `master_fx` while playing: an audible jump (the output moves by 18 frames and the main bus changes source);
  a host may only apply the latency change at the next stop.
- A bounce with the reverb or echo ringing: the 6 s tail.

### Not done / for later
- Control machines (CTR-RE, CTR-GB, CTR-EQ, CTR-DX) are not modelled (they are not pickable).
- Step 7b, the master's idle skip, goes with 0013 (the master runs every block: ~2 % of a core even at rest).
- Step 8 (a C++ translation) stays optional.
- The x64 JIT has never run DSP1's program; the universal build's master is untested.

## Tasks
- [x] Read HANDOFF.md/PROTOCOL.md on DSP1's master section
- [x] Locate the master code and parameters in section 2 and the OS; decode the 38 words and their sources
- [x] Boot DSP1 on dsp56300 and drive it from a harness; verify against the native chain and against the whole loop
- [x] Measure cost (instructions, JIT, interpreter, RAM), compare approaches, recommend
- [x] Confirm the kit's master parameter order
- [x] Step 1: `MasterFx` engine stage and its tests (section 10)
- [x] Step 2: master parameters in `HostModel` (OS pipeline, priming)
- [x] Step 3: engine integration, `golden-master` hash
- [x] Step 4: kits and host parameters (section 11)
- [x] Step 5: plugin audio path (bypass, alignment, latency, tail)
- [x] Step 6: editor MASTER pages
- [x] Step 7a: RAM breakdown and trim (192 MB of mlock and 80 MB of ring buffers per DSP removed; the JIT's block chains are the lead left, section 10)
- [ ] Step 7b: idle skip of the master pass (moved to 0013)
- [ ] Step 8 (optional): C++ translation with `Dsp1Reference` as oracle
- [x] Step 9a: docs: `docs/DEVELOPMENT.md`, the `MdSysex.h` and `KitApply.h` comments
- [x] Step 9b: the README's feature list, once the plugin has the master (and `docs/DEVELOPMENT.md`)
