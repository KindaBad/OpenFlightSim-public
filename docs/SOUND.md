# Sound

Everything heard in the game is synthesised while it plays, from filtered
noise and a few oscillators. There are no audio files, so nothing to license
and nothing to download. The sounds are designed to be convincing and to read
clearly in a fight; they are not recordings of, or models of, any real engine,
gun or warning system.

## How it is put together

| Part | File | Runs on | Does |
|---|---|---|---|
| Mixer | `client/src/sound.cpp` (`SoundMixer`) | audio thread | turns a scene and a queue of events into 48 kHz stereo |
| Director | `client/src/sound.cpp` (`SoundDirector`) | game thread | works out, once a frame, what the camera would hear |
| Device | `client/src/audio_device.cpp` | - | opens the default output through miniaudio |

The director is told what is on screen: the camera, the pilot's own aircraft,
the other aircraft, missiles, and whatever happened this frame. It is
presentation only, like the visual effects, and feeds nothing back into the
simulation. The game thread hands the mixer a small scene structure under a
lock it holds only for the copy; the audio thread never waits for it.

A scripted run (`--frames`, `--seconds`, `--screenshot`, a visual scenario, a
smoke test) opens no sound card. `--no-sound` does the same for ordinary play.
A machine with no usable output runs on in silence and logs why.

## What is heard

**Engines.** Each pair of engines is a compressor whine that rises with engine
speed, an exhaust roar that rises much faster with power, a rumble under both
and, in reheat, a surging crackle; reheat lights with a thump. The two engines
are tuned slightly apart and beat. The whine carries forward and the roar aft,
so an aircraft sounds different coming and going. A stopped engine spins down.
The airliner, the two fighters and the Blackbird are voiced apart. The pilot's
own engines and the four nearest others are heard at once.

**Distance and motion.** Level falls with distance, air takes the top off
whatever has travelled far, and pitch shifts with closing speed, so a pass is
heard to drop. Anything further than 150 m is delayed by the time sound takes
to cover the rest: an explosion two kilometres off is seen, then heard.

**Air and airframe.** Wind rises with the speed of the air past the camera and
thins with altitude, made separately for each ear. The airframe shudders near
the stall, under heavy load and through the speed of sound; the airbrake and
lowered gear roar in the stream; the wheels rumble and knock on the runway;
the gear and flap motors run while they travel and the gear locks.

**Weapons.** Each cannon round, missile launch and burning motor, flare, chaff
bundle, hit, warhead and exploding aircraft has its own sound at the place it
happens. A missile is among the loudest things in a fight: the bang of its
motor lighting, then a tearing roar that carries eight kilometres. Rounds
through the pilot's own airframe ring.

**Coming apart.** An aircraft that flies into the ground is heard to hit it:
the blow, the airframe folding and tearing along the ground, the fuel going
up, wreckage coming down. One that blows up in the air does not make that
sound, but its wreck does when it reaches the ground. A wing or fin breaking
away cracks, tears and is heard to leave. An ejection is the canopy going and
the seat's rocket, and a parachute fills with a rustle and a crack.

**Flight deck.** A stall horn; a two-note missile warning that quickens as the
missile closes, announced by three beeps when a new one is fired; the growl of
a heat seeker rising to a steady tone when it is locked; counted beeps as a
radar missile builds its lock; a tick for own rounds striking and a chime for
a kill; a caution for a lost engine or heavy damage. From the flight deck the
world outside is muffled and the pilot's own aircraft is felt more than heard.
When the pilot is shot down everything dulls until the next life. Under heavy
load the world dulls by degrees and the pilot's own pulse is heard, faster and
louder as sight goes ([PILOT.md](PILOT.md)).

## Settings

Esc > Settings > Sound has an on/off switch, a master volume and four more:
engines, weapons and explosions, wind and airframe, warnings and interface.
The master volume is also on the Esc menu. Sound stops while another window
has the keyboard unless "Keep playing in the background" is ticked. All are
saved in `graphics.cfg` (`sound`, `soundVolume`, `engineVolume`,
`weaponVolume`, `airframeVolume`, `cockpitVolume`, `soundInBackground`).

A limiter sits on the output: anything louder than the ceiling pulls the whole
mix down at once and lets it back over about a fifth of a second.

## Checking it

`client.sound` (`tests/sound_tests.cpp`) plays scripted scenes into memory and
measures them: nothing clips or goes non-finite, power is heard, a pass moves
across the ears and drops in pitch, a distant explosion arrives late, quieter
and duller, every volume control does what it says, every one-off sound is
audible, a missile is clearly louder than a cannon round and is heard over the
pilot's own engines, a crash goes on after the blow, and strain brings on the
pulse and dulls the world. The same scenes can be written out to listen to:

```sh
./build/release/tests/ofs_sound_tests render /tmp/ofs-sound            # everything
./build/release/tests/ofs_sound_tests render /tmp/ofs-sound engines    # one volume control alone
```

A scripted client run can record what it would have played:

```sh
./build/release/client/ofs_client --aircraft typhoon --visual-scenario detonation \
  --frames 240 --screenshot /tmp/shot.ppm --sound-capture /tmp/shot.wav
```

Levels were set by measuring these renders, not by ear on reference speakers;
the volume controls are the place to correct that for a particular setup.
